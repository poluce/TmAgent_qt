#include "MemoryToolsPlugin.h"

#include "MemoryToolProvider.h"

ToolPluginDescriptor MemoryToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("memory_tools");
    descriptor.displayName = QStringLiteral("记忆与检索工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("memory");
    descriptor.description = QStringLiteral("长期记忆、会话检索和事件日志查询工具。");
    const QList<Tool> tools = MemoryToolProvider::toolSchemas();
    for (const Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* MemoryToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    Q_UNUSED(host);
    return new MemoryToolProvider(parent);
}
