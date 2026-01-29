#include "AgentTool.h"
#include "core/agent/AgentEventBus.h"
#include <QDebug>
#include <QEventLoop>

AgentTool::AgentTool(const LLMConfig& parentConfig, const QString& toolName, const QString& toolDesc, QObject* parent)
    : QObject(parent)
    , m_parentConfig(parentConfig)
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

    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] = props;
    schema["required"] = QJsonArray { "task", "role_prompt" };
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
    QString task = args["task"].toString();
    QString rolePrompt = args["role_prompt"].toString();
    bool restrictDelegation = args["restrict_delegation"].toBool(false);

    if (rolePrompt.isEmpty()) {
        rolePrompt = "你是一个专业的 AI 助手。";
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
    m_childAgent->setConfig(childConfig);

    // TODO: 可以在这里为子 Agent 注册它能使用的工具
    // 目前暂且让它也使用默认工具 (在 AgentChatWidget 里可能会自动配置)
    // 但作为纯逻辑组件，我们需要一种机制给它注入工具。
    // *重要*: LLMAgent 默认是没有任何工具的。
    // 理想情况下，我们应该通过 ToolDispatcher 或某种 Registry 给子 Agent 注入基础工具。
    // 暂时先留空，后续步骤在 ToolDispatcher 中完善“默认工具注入”逻辑。

    // 3. 异步转同步执行
    QEventLoop loop;
    QString finalResult;
    QString errorMsg;
    bool success = true;

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
        finalResult = res;
        loop.quit();
    });

    connect(m_childAgent, &LLMAgent::errorOccurred, [&](QString err) {
        errorMsg = err;
        success = false;
        loop.quit();
    });

    // 发送任务 (askOnce 不保存上下文，符合 Tool 语义)
    m_childAgent->askOnce(task);

    loop.exec(); // 阻塞等待完成

    if (!success) {
        return ToolResult("Sub-agent error: " + errorMsg, "子智能体执行出错", false);
    }

    return ToolResult(finalResult, "子智能体任务完成");
}
