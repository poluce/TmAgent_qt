#include "ToolDispatcher.h"
#include "LocalToolProvider.h"
#include "ToolRegistry.h"
#include "core/tools/AgentTool.h"
#include "core/tools/BuiltinTools.h"
#include "core/tools/FileOperationTools.h"
#include "core/utils/ToolSchemaLoader.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>

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
    if (schema.description.isEmpty()) {
        schema.description = description;
    }
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
            m_toolDescriptions.remove(toolName);
        }
    }

    indexProviderTools(m_providers.value(name), name);
}

void ToolDispatcher::registerDefaultTools()
{
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
        Tool schema = tool->getSchema();
        // 如果 schema 中没有描述，则使用名称作为默认描述
        QString desc = schema.description.isEmpty() ? schema.name : schema.description;
        registerTool(tool, desc);
    }

    qDebug() << "[ToolDispatcher] 自动加载并注册了" << automaticTools.size() << "个工具接口";
}

QList<Tool> ToolDispatcher::getAllToolSchemas() const
{
    QList<Tool> schemas;
    schemas = m_toolSchemas.values();
    return schemas;
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

    const QString desc = m_toolDescriptions.value(toolName, toolName);
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

    // 注册: 通用任务委派
    // 允许主 Agent 动态指定子 Agent 的角色
    registerTool(new AgentTool(config, "delegate_task", "将任务委派给一个专门的子智能体。你可以指定子智能体的角色（role_prompt）和具体任务（task）。"), "委派任务给子智能体");
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
            const QString desc = tool.description.isEmpty() ? tool.name : tool.description;
            m_toolDescriptions.insert(tool.name, desc);
            return;
        }
        qWarning() << "[ToolDispatcher] 工具名冲突:" << tool.name << "provider:"
                   << providerName << "existing:" << existingOwner;
        return;
    }

    m_toolIndex.insert(tool.name, provider);
    m_toolSchemas.insert(tool.name, tool);
    m_toolOwners.insert(tool.name, providerName);
    const QString desc = tool.description.isEmpty() ? tool.name : tool.description;
    m_toolDescriptions.insert(tool.name, desc);
}
