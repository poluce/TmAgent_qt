#include "ToolDispatcher.h"
#include "LocalToolProvider.h"
#include "ToolRegistry.h"
#include "core/tools/AgentTool.h"
#include "core/tools/BuiltinTools.h"
#include "core/tools/FileOperationTools.h"
#include "core/utils/ToolSchemaLoader.h"
#include <QCoreApplication>
#include <QDebug>

ToolDispatcher* ToolDispatcher::instance()
{
    static ToolDispatcher dispatcher;
    return &dispatcher;
}

ToolDispatcher::ToolDispatcher(QObject* parent)
    : QObject(parent)
    , m_localProvider(std::make_unique<LocalToolProvider>())
{
    m_providers.insert("local", m_localProvider.get());
}

ToolDispatcher::~ToolDispatcher() = default;

void ToolDispatcher::registerTool(ITool* tool, const QString& description)
{
    if (!tool)
        return;
    m_localProvider->registerTool(tool, description);
    Tool schema = tool->getSchema();
    if (schema.description.isEmpty())
        schema.description = description;
    indexToolSchema(schema, m_localProvider.get(), "local");
}

void ToolDispatcher::registerProvider(IToolProvider* provider, const QString& name)
{
    if (!provider)
        return;

    const QString providerName = name.isEmpty() ? QString("provider") : name;
    if (m_providers.contains(providerName)) {
        qWarning() << "[ToolDispatcher] provider 名称冲突:" << providerName;
        return;
    }

    m_providers.insert(providerName, provider);
    indexProviderTools(provider, providerName);
}

void ToolDispatcher::refreshProvider(const QString& name)
{
    if (!m_providers.contains(name))
        return;

    const QList<QString> toolNames = m_toolOwners.keys();
    for (const QString& toolName : toolNames) {
        if (m_toolOwners.value(toolName) == name) {
            m_toolOwners.remove(toolName);
            m_toolIndex.remove(toolName);
            m_toolSchemas.remove(toolName);
        }
    }

    indexProviderTools(m_providers.value(name), name);
}

void ToolDispatcher::registerDefaultTools()
{
    if (m_defaultToolsRegistered) {
        return;
    }
    static bool schemaLoaded = false;
    if (!schemaLoaded) {
        QString toolsPath = QCoreApplication::applicationDirPath() + "/resources/tools.yaml";
        ToolSchemaLoader::loadFromFile(toolsPath);
        schemaLoaded = true;
    }

    // 1. 获取所有通过静态注册宏注册的工具实例
    QList<ITool*> automaticTools = ToolRegistry::instance()->createAllTools();

    // 2. 依次注册到 Dispatcher
    for (ITool* tool : automaticTools) {
        const Tool schema = tool->getSchema();
        registerTool(tool, schema.description.isEmpty() ? schema.name : schema.description);
    }

    qDebug() << "[ToolDispatcher] 自动加载并注册了" << automaticTools.size() << "个工具接口";
    m_defaultToolsRegistered = true;
}

QList<Tool> ToolDispatcher::getAllToolSchemas() const
{
    return m_toolSchemas.values();
}

bool ToolDispatcher::hasToolSchema(const QString& name) const
{
    return m_toolSchemas.contains(name);
}

ToolResult ToolDispatcher::dispatch(const ToolCall& call)
{
    const QString& toolName = call.name;
    QJsonObject input = call.input;
    input["_tool_call_id"] = call.id;
    QString inputStr = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));

    qDebug() << "[ToolDispatcher] 分发接口工具调用:" << toolName;

    if (!m_toolIndex.contains(toolName)) {
        return ToolResult(QString("错误: 未知的工具 %1").arg(toolName), "执行失败", false);
    }

    const Tool& schema = m_toolSchemas[toolName];
    const QString desc = schema.description.isEmpty() ? toolName : schema.description;
    emit toolStarted(desc, inputStr);

    ToolCall enriched = call;
    enriched.input = input;
    return m_toolIndex.value(toolName)->execute(enriched);
}

void ToolDispatcher::registerAgentTools(const LLMConfig& config)
{
    // 如果递归深度已为 0，则禁止注册任何委派工具
    if (!config.canDelegate()) {
        qDebug() << "[ToolDispatcher] Recursion depth reached 0, agent delegation disabled.";
        return;
    }

    qDebug() << "[ToolDispatcher] Registering agent tools. Remaining depth:" << config.recursionDepth;

    auto ensureDelegateTool = [this, &config](const QString& toolName, const QString& description) {
        if (m_toolSchemas.contains(toolName))
            return;
        registerTool(new AgentTool(config, this, toolName, description, this), description);
    };

    ensureDelegateTool(
        QStringLiteral("delegate_task"),
        QStringLiteral("将任务委派给后台子智能体并立即返回 job_id。"));
    ensureDelegateTool(
        QStringLiteral("delegate_status"),
        QStringLiteral("查询后台子智能体任务状态。"));
    ensureDelegateTool(
        QStringLiteral("delegate_cancel"),
        QStringLiteral("取消后台子智能体任务。"));
    ensureDelegateTool(
        QStringLiteral("delegate_list_active"),
        QStringLiteral("列出当前运行中的后台子智能体任务。"));

    // 队友工具（通用，后端可插拔）
    ensureDelegateTool(
        QStringLiteral("create_teammate"),
        QStringLiteral("创建一个持久化的队友。队友拥有独立名字和角色，可随时多轮对话。通过 backend 参数指定后端（如 codex、claude-code）。"));
    ensureDelegateTool(
        QStringLiteral("message_teammate"),
        QStringLiteral("向指定的队友发送消息并等待回复。可按名称或 ID 指定队友。"));
    ensureDelegateTool(
        QStringLiteral("list_teammates"),
        QStringLiteral("列出所有已创建的队友及其状态。"));
    ensureDelegateTool(
        QStringLiteral("remove_teammate"),
        QStringLiteral("移除/关闭指定的队友。可按名称或 ID 指定。"));
    ensureDelegateTool(
        QStringLiteral("rename_teammate"),
        QStringLiteral("重命名指定的队友。可按名称或 ID 指定。"));
    ensureDelegateTool(
        QStringLiteral("get_teammate_status"),
        QStringLiteral("查询指定队友的详细状态，包括当前状态、最后错误、Turn 计数、工作目录等。"));
    ensureDelegateTool(
        QStringLiteral("message_between_teammates"),
        QStringLiteral("让一个队友直接给另一个队友发消息。指定发送方和接收方（名称或 ID），系统将发送方的消息转发给接收方，接收方回复后自动推送到当前会话。"));
}

void ToolDispatcher::indexProviderTools(IToolProvider* provider, const QString& providerName)
{
    const QList<Tool> tools = provider->listTools();
    for (const Tool& tool : tools) {
        indexToolSchema(tool, provider, providerName);
    }
}

void ToolDispatcher::indexToolSchema(const Tool& tool, IToolProvider* provider, const QString& providerName)
{
    if (!provider || tool.name.isEmpty())
        return;

    if (m_toolIndex.contains(tool.name)) {
        const QString existingOwner = m_toolOwners.value(tool.name);
        if (existingOwner == providerName) {
            m_toolSchemas.insert(tool.name, tool);
            return;
        }
        qWarning() << "[ToolDispatcher] 工具名冲突:" << tool.name << "provider:"
                   << providerName << "existing:" << existingOwner;
        return;
    }

    m_toolIndex.insert(tool.name, provider);
    m_toolSchemas.insert(tool.name, tool);
    m_toolOwners.insert(tool.name, providerName);
}
