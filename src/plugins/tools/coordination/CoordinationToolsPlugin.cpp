#include "CoordinationToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"
#include "core/tools/AgentToolNames.h"

namespace {

QList<HostedToolSpec> coordinationToolSpecs()
{
    QList<HostedToolSpec> specs;
    for (const QString& name : AgentToolNames::all()) {
        specs.append({ name, QStringLiteral("助手协作与委派工具") });
    }
    return specs;
}

} // namespace

ToolPluginDescriptor CoordinationToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("coordination_tools");
    descriptor.displayName = QStringLiteral("协作委派工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("coordination");
    descriptor.description = QStringLiteral("后台子任务委派、队友管理和队友间消息转发工具。");
    descriptor.toolNames = AgentToolNames::all();
    return descriptor;
}

IToolProvider* CoordinationToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, coordinationToolSpecs(), parent);
}
