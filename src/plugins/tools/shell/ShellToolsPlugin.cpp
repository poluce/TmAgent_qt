#include "ShellToolsPlugin.h"
#include "ShellToolProvider.h"
#include "ShellToolSchemas.h"

TmAgent::ToolPluginDescriptor ShellToolsPlugin::descriptor() const
{
    TmAgent::ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("shell_tools");
    descriptor.displayName = QStringLiteral("命令工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("shell");
    descriptor.description = QStringLiteral("命令执行与工作目录控制相关工具。");
    descriptor.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    descriptor.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    for (const TmAgent::Tool& tool : shellTools())
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

TmAgent::IToolProvider* ShellToolsPlugin::createProvider(TmAgent::IToolPluginHost* host, QObject* parent)
{
    Q_UNUSED(host);
    return new ShellToolProvider(parent);
}
