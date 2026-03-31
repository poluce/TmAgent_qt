#include "WebToolsPlugin.h"

#include "WebToolProvider.h"

ToolPluginDescriptor WebToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("web_tools");
    descriptor.displayName = QStringLiteral("网络工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("web");
    descriptor.description = QStringLiteral("网页抓取和网页搜索相关工具。");
    const QList<Tool> tools = WebToolProvider::toolSchemas();
    for (const Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* WebToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    Q_UNUSED(host);
    return new WebToolProvider(parent);
}
