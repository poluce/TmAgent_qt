#include "WorkspaceToolsPlugin.h"
#include "WorkspaceToolProvider.h"
#include "WorkspaceToolSchemas.h"
#include <tmagent/version.h>

TmAgent::ToolPluginDescriptor WorkspaceToolsPlugin::descriptor() const
{
    TmAgent::ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("workspace_tools");
    descriptor.displayName = QStringLiteral("工作区工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("workspace");
    descriptor.description = QStringLiteral("文件读取、目录搜索、补丁与文件写入类工具集合。");
    
    // Add SDK version fields
    descriptor.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    descriptor.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    const QList<TmAgent::Tool> tools = workspaceTools();
    for (const TmAgent::Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

TmAgent::IToolProvider* WorkspaceToolsPlugin::createProvider(TmAgent::IToolPluginHost* host, QObject* parent)
{
    Q_UNUSED(host);
    return new WorkspaceToolProvider(parent);
}
