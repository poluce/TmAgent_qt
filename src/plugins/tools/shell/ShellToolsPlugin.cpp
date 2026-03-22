#include "ShellToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"

namespace {

QList<HostedToolSpec> shellToolSpecs()
{
    return {
        { QStringLiteral("execute_command"), QStringLiteral("执行终端命令") }
    };
}

} // namespace

ToolPluginDescriptor ShellToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("shell_tools");
    descriptor.displayName = QStringLiteral("命令工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("shell");
    descriptor.description = QStringLiteral("命令执行与工作目录控制相关工具。");
    descriptor.toolNames << QStringLiteral("execute_command");
    return descriptor;
}

IToolProvider* ShellToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, shellToolSpecs(), parent);
}
