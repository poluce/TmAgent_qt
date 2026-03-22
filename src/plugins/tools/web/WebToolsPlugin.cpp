#include "WebToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"

namespace {

QList<HostedToolSpec> webToolSpecs()
{
    return {
        { QStringLiteral("web_fetch"), QStringLiteral("抓取网页内容") },
        { QStringLiteral("websearch"), QStringLiteral("网页搜索") }
    };
}

} // namespace

ToolPluginDescriptor WebToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("web_tools");
    descriptor.displayName = QStringLiteral("网络工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("web");
    descriptor.description = QStringLiteral("网页抓取和网页搜索相关工具。");
    descriptor.toolNames << QStringLiteral("web_fetch") << QStringLiteral("websearch");
    return descriptor;
}

IToolProvider* WebToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, webToolSpecs(), parent);
}
