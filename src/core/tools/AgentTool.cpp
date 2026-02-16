#include "AgentTool.h"
#include "core/agent/AgentEventBus.h"
#include "core/agent/ToolDispatcher.h"
#include "core/utils/DefaultPrompts.h"
#include "newCore/ModelFactory.h"
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

AgentTool::AgentTool(const LLMConfig& parentConfig,
                     ToolDispatcher* toolDispatcher,
                     const QString& toolName,
                     const QString& toolDesc,
                     QObject* parent)
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
        { "description", QString("子智能体执行超时（毫秒，范围 %1-%2）")
                .arg(kMinDelegateTimeoutMs)
                .arg(kMaxDelegateTimeoutMs) }
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

    if (task.isEmpty()) {
        return ToolResult(QStringLiteral("错误: task 不能为空"),
                          QStringLiteral("子智能体执行失败：缺少 task"),
                          false);
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

    if (m_parentConfig.recursionDepth <= 0) {
        return ToolResult(QStringLiteral("错误: 当前递归深度已耗尽，不能继续委派"),
                          QStringLiteral("子智能体执行失败：递归深度不足"),
                          false);
    }

    qDebug() << "AgentTool [" << m_schema.name << "] starting. Role:" << rolePrompt.left(30) << "... Task:" << task.left(30);

    // 1. 配置子 Agent
    // 如果启用了 Override，则以 Override Config 为基础，否则以 Parent Config 为基础
    LLMConfig childConfig = m_useOverrideConfig ? m_overrideConfig : m_parentConfig;

    // === 核心逻辑: 深度递减控制 ===
    // 注意：无论是否 Override，递归深度必须由当前链条决定，不能被 Override Config 随意重置
    // 因此这里我们需要强制覆盖 Override Config 中的 recursionDepth

    int currentDepth = m_parentConfig.recursionDepth; // 总是以父级深度为准

    if (restrictDelegation) {
        childConfig.recursionDepth = 0;
    } else {
        childConfig.recursionDepth = currentDepth - 1;
    }

    // 防御性检查
    if (childConfig.recursionDepth < 0)
        childConfig.recursionDepth = 0;

    // 设置子 Agent 的角色
    childConfig.systemPrompt = rolePrompt;
    childConfig.userName = m_schema.name; // 用工具名作为 Agent 名

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

    if (!success) {
        return ToolResult("Sub-agent error: " + errorMsg, "子智能体执行出错", false);
    }

    if (finalResult.size() > maxResponseChars) {
        finalResult = finalResult.left(maxResponseChars)
                    + QStringLiteral("\n...[delegate response truncated]...");
    }

    return ToolResult(finalResult, "子智能体任务完成");
}
