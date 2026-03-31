#include "ToolDispatcher.h"
#include "ToolFailureSupport.h"
#include "core/backend/BackendPluginManager.h"
#include <QJsonDocument>
#include <QDebug>

ToolDispatcher* ToolDispatcher::instance()
{
    static ToolDispatcher dispatcher;
    return &dispatcher;
}

ToolDispatcher::ToolDispatcher(QObject* parent)
    : QObject(parent)
{
}

ToolDispatcher::~ToolDispatcher() = default;

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

QList<Tool> ToolDispatcher::getAllToolSchemas() const
{
    return m_toolSchemas.values();
}

QStringList ToolDispatcher::toolNamesForProvider(const QString& providerName) const
{
    QStringList names;
    for (auto it = m_toolOwners.constBegin(); it != m_toolOwners.constEnd(); ++it) {
        if (it.value() == providerName)
            names.append(it.key());
    }
    names.sort(Qt::CaseInsensitive);
    return names;
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
    const ToolResult result = m_toolIndex.value(toolName)->execute(enriched);
    return ToolFailureSupport::enrichFailureResult(toolName, call.input, result);
}

void ToolDispatcher::registerAgentTools(const LLMConfig& config)
{
    Q_UNUSED(config);
}

void ToolDispatcher::clearProviders()
{
    m_providers.clear();
    m_toolIndex.clear();
    m_toolSchemas.clear();
    m_toolOwners.clear();
}

void ToolDispatcher::setDefaultAgentConfig(const LLMConfig& config)
{
    m_defaultAgentConfig = config;
}

QStringList ToolDispatcher::availableTeammateBackendIds() const
{
    return BackendPluginManager::instance()->teammateBackendIds();
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
