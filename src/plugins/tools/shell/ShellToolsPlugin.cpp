#include "ShellToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"
#include "core/tools/ToolSchemaSupport.h"

namespace {

QList<Tool> shellTools()
{
    return {
        makeToolSchema(
            QStringLiteral("execute_command"),
            QStringLiteral("执行终端命令并返回结果。"),
            QJsonObject {
                { QStringLiteral("command"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要执行的命令，例如: dir, git status, qmake")) },
                { QStringLiteral("working_directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("工作目录（可选）")) }
            },
            QStringList { QStringLiteral("command") })
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
    for (const Tool& tool : shellTools())
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* ShellToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, shellTools(), parent);
}
