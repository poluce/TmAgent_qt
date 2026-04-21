#include "WebToolsPlugin.h"

#include "WebToolProvider.h"
#include <tmagent/version.h>

TmAgent::ToolPluginDescriptor WebToolsPlugin::descriptor() const
{
    TmAgent::ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("web_tools");
    descriptor.displayName = QStringLiteral("网络工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("web");
    descriptor.description = QStringLiteral("网页抓取和网页搜索相关工具。");
    descriptor.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    descriptor.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    const QList<TmAgent::Tool> tools = WebToolProvider::toolSchemas();
    for (const TmAgent::Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

TmAgent::IToolProvider* WebToolsPlugin::createProvider(TmAgent::IToolPluginHost* host, QObject* parent)
{
    Q_UNUSED(host);
    return new WebToolProvider(parent);
}
