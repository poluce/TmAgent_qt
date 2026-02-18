#include "LocalToolProvider.h"
#include <QDebug>

LocalToolProvider::~LocalToolProvider()
{
    for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
        delete it->toolImpl;
    m_registry.clear();
}

void LocalToolProvider::registerTool(ITool* tool, const QString& description)
{
    if (!tool)
        return;

    const Tool schema = tool->getSchema();
    const QString name = schema.name;
    if (name.isEmpty())
        return;

    ToolEntry entry;
    entry.toolImpl = tool;
    entry.description = description;
    if (m_registry.contains(name) && m_registry.value(name).toolImpl != tool)
        delete m_registry.value(name).toolImpl;
    m_registry[name] = entry;

    qDebug() << "[LocalToolProvider] 注册接口工具:" << name << "-" << description;
}

QList<Tool> LocalToolProvider::listTools() const
{
    QList<Tool> schemas;
    for (const ToolEntry& entry : m_registry) {
        Tool schema = entry.toolImpl->getSchema();
        if (schema.description.isEmpty())
            schema.description = entry.description.isEmpty() ? schema.name : entry.description;
        schemas.append(schema);
    }
    return schemas;
}

ToolResult LocalToolProvider::execute(const ToolCall& call)
{
    const QString& toolName = call.name;
    if (m_registry.contains(toolName)) {
        const ToolEntry& entry = m_registry[toolName];
        return entry.toolImpl->execute(call.input);
    }
    return ToolResult(QString("错误: 未知的工具 %1").arg(toolName), "执行失败", false);
}

QString LocalToolProvider::descriptionFor(const QString& toolName) const
{
    if (m_registry.contains(toolName)) {
        return m_registry.value(toolName).description;
    }
    return QString();
}
