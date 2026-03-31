#include "ShellToolsPlugin.h"
#include "ShellToolProvider.h"
#include "ShellToolSchemas.h"

#include "core/tools/ToolSchemaSupport.h"

ToolPluginDescriptor ShellToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("shell_tools");
    descriptor.displayName = QStringLiteral("命令工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("shell");
    descriptor.description = QStringLiteral("命令执行与工作目录控制相关工具。");
    for (const Tool& tool : shellTools())
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* ShellToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    Q_UNUSED(host);
    return new ShellToolProvider(parent);
}
