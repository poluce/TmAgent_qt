#include "CoordinationToolsPlugin.h"

#include "core/tools/AgentToolNames.h"
#include "CoordinationToolProvider.h"

ToolPluginDescriptor CoordinationToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("coordination_tools");
    descriptor.displayName = QStringLiteral("团队协作工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("coordination");
    descriptor.description = QStringLiteral("团队协作、队友管理和队友间消息转发工具。");
    descriptor.toolNames = AgentToolNames::all();
    return descriptor;
}

IToolProvider* CoordinationToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    Q_UNUSED(host);
    return new CoordinationToolProvider(parent);
}
