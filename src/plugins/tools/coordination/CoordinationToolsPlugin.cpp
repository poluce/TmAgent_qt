#include "CoordinationToolsPlugin.h"

#include "core/tools/AgentToolNames.h"
#include "CoordinationToolProvider.h"
#include <tmagent/version.h>

TmAgent::ToolPluginDescriptor CoordinationToolsPlugin::descriptor() const
{
    TmAgent::ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("coordination_tools");
    descriptor.displayName = QStringLiteral("团队协作工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("coordination");
    descriptor.description = QStringLiteral("团队协作、队友管理和队友间消息转发工具。");
    descriptor.toolNames = AgentToolNames::all();
    descriptor.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    descriptor.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    return descriptor;
}

TmAgent::IToolProvider* CoordinationToolsPlugin::createProvider(TmAgent::IToolPluginHost* host, QObject* parent)
{
    return new CoordinationToolProvider(host, parent);
}
