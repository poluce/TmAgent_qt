#include "AgentTool.h"
#include "core/agent/AgentEventBus.h"
#include "core/agent/ToolDispatcher.h"
#include "core/utils/DefaultPrompts.h"
#include "newCore/ModelFactory.h"
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QJsonArray>
#include <QTimer>
#include <QUuid>

namespace {
ModelId safeModelIdFromInt(int value)
{
    const int minValue = static_cast<int>(ModelId::Unknown);
    const int maxValue = static_cast<int>(ModelId::Custom);
    if (value < minValue || value > maxValue)
        return ModelId::Unknown;
    return static_cast<ModelId>(value);
}

LLMConfig resolveParentConfigFromArgs(const LLMConfig& fallback, const QJsonObject& args)
{
    LLMConfig cfg = fallback;

    const QString agentId = args.value(QStringLiteral("_agent_id")).toString().trimmed();
    if (!agentId.isEmpty())
        cfg.uuid = agentId;

    const int recursionDepth = args.value(QStringLiteral("_agent_recursion_depth"))
                                   .toInt(cfg.recursionDepth);
    cfg.recursionDepth = qMax(0, recursionDepth);

    if (args.contains(QStringLiteral("_agent_model"))) {
        const ModelId parsed = safeModelIdFromInt(
            args.value(QStringLiteral("_agent_model")).toInt(static_cast<int>(cfg.model)));
        if (parsed != ModelId::Unknown)
            cfg.model = parsed;
    }

    const QString customModelId = args.value(QStringLiteral("_agent_custom_model_id")).toString().trimmed();
    if (!customModelId.isEmpty())
        cfg.customModelId = customModelId;

    const QString workspace = args.value(QStringLiteral("_agent_workspace")).toString().trimmed();
    if (!workspace.isEmpty())
        cfg.workspaceDir = workspace;

    return cfg;
}

QJsonObject collectChildRunData(LLMAgent* childAgent)
{
    QJsonObject out;
    if (!childAgent)
        return out;

    const QJsonArray io = childAgent->getIoHistory();
    if (io.isEmpty())
        return out;

    QString latestRequestId;
    QString finishReason;
    QString errorMessage;
    for (const QJsonValue& value : io) {
        const QJsonObject entry = value.toObject();
        const QString requestId = entry.value(QStringLiteral("request_id")).toString().trimmed();
        if (!requestId.isEmpty())
            latestRequestId = requestId;

        const QJsonObject response = entry.value(QStringLiteral("response")).toObject();
        const QJsonArray choices = response.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QString reason = choices.at(0)
                                       .toObject()
                                       .value(QStringLiteral("finish_reason"))
                                       .toString()
                                       .trimmed();
            if (!reason.isEmpty())
                finishReason = reason;
        }

        const QString err = entry.value(QStringLiteral("error"))
                                .toObject()
                                .value(QStringLiteral("message"))
                                .toString()
                                .trimmed();
        if (!err.isEmpty())
            errorMessage = err;
    }

    if (!latestRequestId.isEmpty()) {
        out.insert(QStringLiteral("child_request_id"), latestRequestId);
        out.insert(QStringLiteral("child_trace_id"), latestRequestId);
    }
    if (!finishReason.isEmpty())
        out.insert(QStringLiteral("child_finish_reason"), finishReason);
    if (!errorMessage.isEmpty())
        out.insert(QStringLiteral("child_error"), errorMessage);
    out.insert(QStringLiteral("child_io_entries"), io.size());
    return out;
}
} // namespace

AgentTool::AgentTool(const LLMConfig& parentConfig, ToolDispatcher* toolDispatcher, const QString& toolName, const QString& toolDesc, QObject* parent)
    : QObject(parent)
    , m_parentConfig(parentConfig)
    , m_toolDispatcher(toolDispatcher)
{
    // 1. 构建 Schema
    m_schema.name = toolName.isEmpty() ? "delegate_task" : toolName;
    m_schema.description = toolDesc;

    QJsonObject props;
    props["task"] = QJsonObject {
        { "type", "string" },
        { "description", "需要委派给子智能体的具体任务描述，请包含所有必要的上下文信息。" }
    };
    props["role_prompt"] = QJsonObject {
        { "type", "string" },
        { "description", "子智能体的角色设定（System Prompt）。例如：'你是一个资深 Qt/C++ 开发专家，请帮我审查代码...'。请详细描述子智能体的能力和职责。" }
    };
    props["restrict_delegation"] = QJsonObject {
        { "type", "boolean" },
        { "description", "是否强制剥夺子智能体的进一步委派能力 (设为 true 则子 Agent 无法再创建下级 Agent)" }
    };
    props["timeout_ms"] = QJsonObject {
        { "type", "integer" },
        { "description", QString("子智能体执行超时（毫秒，范围 %1-%2）").arg(kMinDelegateTimeoutMs).arg(kMaxDelegateTimeoutMs) }
    };
    props["max_response_chars"] = QJsonObject {
        { "type", "integer" },
        { "description", "返回给主智能体的最大字符数，超长会自动截断" }
    };

    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] = props;
    schema["required"] = QJsonArray { "task" };
    m_schema.inputSchema = schema;
}

AgentTool::~AgentTool()
{
    if (m_childAgent) {
        m_childAgent->deleteLater();
    }
}

void AgentTool::setOverrideConfig(const LLMConfig& config)
{
    m_overrideConfig = config;
    m_useOverrideConfig = true;
}

Tool AgentTool::getSchema() const
{
    return m_schema;
}

ToolResult AgentTool::execute(const QJsonObject& args)
{
    QString task = args["task"].toString().trimmed();
    QString rolePrompt = args["role_prompt"].toString().trimmed();
    bool restrictDelegation = args["restrict_delegation"].toBool(false);
    int timeoutMs = args["timeout_ms"].toInt(kDefaultDelegateTimeoutMs);
    int maxResponseChars = args["max_response_chars"].toInt(kDefaultMaxResponseChars);

    QJsonObject delegateData;
    delegateData.insert(QStringLiteral("delegate_tool"), m_schema.name);
    delegateData.insert(QStringLiteral("requested_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    if (task.isEmpty()) {
        delegateData.insert(QStringLiteral("status"), QStringLiteral("failed"));
        delegateData.insert(QStringLiteral("failure_reason"), QStringLiteral("missing_task"));
        return ToolResult(
            QStringLiteral("错误: task 不能为空"),
            QStringLiteral("子智能体执行失败：缺少 task"),
            false,
            delegateData);
    }
    if (task.size() > kMaxTaskChars) {
        task = task.left(kMaxTaskChars) + QStringLiteral("\n...[task truncated]...");
    }
    timeoutMs = qBound(kMinDelegateTimeoutMs, timeoutMs, kMaxDelegateTimeoutMs);
    maxResponseChars = qBound(500, maxResponseChars, 20000);

    if (rolePrompt.isEmpty()) {
        rolePrompt = DefaultPrompts::codingAssistantSystemPrompt();
    }
    rolePrompt = DefaultPrompts::ensureExecutionDiscipline(rolePrompt);

    const LLMConfig parentConfig = resolveParentConfigFromArgs(m_parentConfig, args);

    if (parentConfig.recursionDepth <= 0) {
        delegateData.insert(QStringLiteral("status"), QStringLiteral("failed"));
        delegateData.insert(QStringLiteral("failure_reason"), QStringLiteral("recursion_depth_exhausted"));
        return ToolResult(
            QStringLiteral("错误: 当前递归深度已耗尽，不能继续委派"),
            QStringLiteral("子智能体执行失败：递归深度不足"),
            false,
            delegateData);
    }

    qDebug() << "AgentTool [" << m_schema.name << "] starting. Role:" << rolePrompt.left(30) << "... Task:" << task.left(30);

    // 1. 配置子 Agent
    // 如果启用了 Override，则以 Override Config 为基础，否则以 Parent Config 为基础
    LLMConfig childConfig = m_useOverrideConfig ? m_overrideConfig : parentConfig;
    if (childConfig.workspaceDir.trimmed().isEmpty())
        childConfig.workspaceDir = parentConfig.workspaceDir;
    if (childConfig.customModelId.trimmed().isEmpty())
        childConfig.customModelId = parentConfig.customModelId;
    if (childConfig.model == ModelId::Unknown)
        childConfig.model = parentConfig.model;

    // === 核心逻辑: 深度递减控制 ===
    int currentDepth = parentConfig.recursionDepth;
    childConfig.recursionDepth = restrictDelegation ? 0 : qMax(0, currentDepth - 1);

    // 设置子 Agent 的角色
    childConfig.systemPrompt = rolePrompt;
    childConfig.userName = m_schema.name; // 用工具名作为 Agent 名
    childConfig.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    delegateData.insert(QStringLiteral("child_agent_id"), childConfig.uuid);
    delegateData.insert(
        QStringLiteral("child_model"),
        ModelFactory::resolveModelKey(childConfig.model, childConfig.customModelId));
    if (!childConfig.workspaceDir.trimmed().isEmpty())
        delegateData.insert(QStringLiteral("child_workspace"), childConfig.workspaceDir.trimmed());
    delegateData.insert(QStringLiteral("child_timeout_ms"), timeoutMs);
    delegateData.insert(QStringLiteral("max_response_chars"), maxResponseChars);

    // 2. 实例化子 Agent (如果尚未创建或复用策略需要)
    // 这里选择每次 execute 创建新 Agent 以保证状态隔离
    // 如果要支持多轮对话，可以用 m_childAgent 缓存，但 AgentTool 目前设计为一次性任务委派
    if (m_childAgent) {
        m_childAgent->deleteLater();
    }
    m_childAgent = new LLMAgent(this);
    m_childAgent->setModelFactory(ModelFactory::instance());
    m_childAgent->setConfig(childConfig);
    if (m_toolDispatcher)
        m_childAgent->setToolDispatcher(m_toolDispatcher);

    // 3. 异步转同步执行
    QEventLoop loop;
    QString finalResult;
    QString errorMsg;
    bool success = true;
    bool settled = false;
    const qint64 startedAtMs = QDateTime::currentMSecsSinceEpoch();
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    // 转发子智能体的工具执行状态到全局总线 (作为当前工具的进度)
    connect(m_childAgent, &LLMAgent::toolEvent, [this](const ToolExecutionEvent& subEvent) {
        ToolExecutionEvent progressEvent;
        progressEvent.toolName = m_schema.name;
        progressEvent.status = "progress";

        if (subEvent.status == "started") {
            progressEvent.formattedResult = QString("子智能体正在执行: %1").arg(subEvent.toolName);
        } else if (subEvent.status == "completed") {
            progressEvent.formattedResult = QString("子智能体完成: %1").arg(subEvent.toolName);
        } else {
            progressEvent.formattedResult = subEvent.formattedResult;
        }

        AgentEventBus::instance()->postToolEvent(progressEvent);
    });

    // 转发流式输入进度
    m_progressAccumulator.clear();
    connect(m_childAgent, &LLMAgent::streamDataReceived, [this](const QString& data) {
        m_progressAccumulator += data;
        if (m_progressAccumulator.length() > 20) { // 减少频繁发送
            ToolExecutionEvent progressEvent;
            progressEvent.toolName = m_schema.name;
            progressEvent.status = "progress";
            progressEvent.formattedResult = QString("子智能体正在输出: %1...").arg(m_progressAccumulator.right(20));
            AgentEventBus::instance()->postToolEvent(progressEvent);
            m_progressAccumulator.clear();
        }
    });

    connect(m_childAgent, &LLMAgent::finished, [&](QString res) {
        if (settled)
            return;
        settled = true;
        finalResult = res;
        timeoutTimer.stop();
        loop.quit();
    });

    connect(m_childAgent, &LLMAgent::errorOccurred, [&](QString err) {
        if (settled)
            return;
        settled = true;
        errorMsg = err;
        success = false;
        timeoutTimer.stop();
        loop.quit();
    });

    connect(&timeoutTimer, &QTimer::timeout, [&]() {
        if (settled)
            return;
        settled = true;
        success = false;
        errorMsg = QStringLiteral("sub-agent timeout");
        if (m_childAgent)
            m_childAgent->abort();
        loop.quit();
    });

    // 发送任务 (askOnce 不保存上下文，符合 Tool 语义)
    m_childAgent->askOnce(task);
    timeoutTimer.start(timeoutMs);

    loop.exec(); // 阻塞等待完成
    const qint64 durationMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAtMs);
    delegateData.insert(QStringLiteral("child_duration_ms"), static_cast<double>(durationMs));
    const QJsonObject childRunData = collectChildRunData(m_childAgent);
    for (auto it = childRunData.constBegin(); it != childRunData.constEnd(); ++it)
        delegateData.insert(it.key(), it.value());

    if (!success) {
        delegateData.insert(QStringLiteral("status"), QStringLiteral("failed"));
        delegateData.insert(QStringLiteral("failure_reason"), errorMsg);
        return ToolResult(
            QStringLiteral("Sub-agent error: ") + errorMsg,
            QStringLiteral("子智能体执行出错"),
            false,
            delegateData);
    }

    if (finalResult.size() > maxResponseChars) {
        finalResult = finalResult.left(maxResponseChars)
            + QStringLiteral("\n...[delegate response truncated]...");
        delegateData.insert(QStringLiteral("truncated"), true);
    }

    delegateData.insert(QStringLiteral("status"), QStringLiteral("completed"));
    return ToolResult(finalResult, QStringLiteral("子智能体任务完成"), true, delegateData);
}
