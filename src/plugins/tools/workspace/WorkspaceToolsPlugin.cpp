#include "WorkspaceToolsPlugin.h"
#include "WorkspaceToolProvider.h"
#include "WorkspaceToolSchemas.h"

ToolPluginDescriptor WorkspaceToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("workspace_tools");
    descriptor.displayName = QStringLiteral("工作区工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("workspace");
    descriptor.description = QStringLiteral("文件读取、目录搜索、补丁与文件写入类工具集合。");
    const QList<Tool> tools = workspaceTools();
    for (const Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* WorkspaceToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    Q_UNUSED(host);
    return new WorkspaceToolProvider(parent);
}
