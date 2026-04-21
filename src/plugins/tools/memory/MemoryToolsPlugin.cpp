#include "MemoryToolsPlugin.h"

#include "MemoryToolProvider.h"

TmAgent::ToolPluginDescriptor MemoryToolsPlugin::descriptor() const
{
    TmAgent::ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("memory_tools");
    descriptor.displayName = QStringLiteral("记忆与检索工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("memory");
    descriptor.description = QStringLiteral("长期记忆、会话检索和事件日志查询工具。");
    descriptor.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    descriptor.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    const QList<TmAgent::Tool> tools = MemoryToolProvider::toolSchemas();
    for (const TmAgent::Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

TmAgent::IToolProvider* MemoryToolsPlugin::createProvider(TmAgent::IToolPluginHost* host, QObject* parent)
{
    return new MemoryToolProvider(host, parent);
}
