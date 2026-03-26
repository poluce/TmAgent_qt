#include "SchedulerToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"
#include "core/tools/SchedulerTool.h"

ToolPluginDescriptor SchedulerToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("scheduler_tools");
    descriptor.displayName = QStringLiteral("定时任务工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("scheduler");
    descriptor.description = QStringLiteral("为当前助手创建、管理和触发定时任务的工具。");
    const QList<Tool> tools = SchedulerTool::toolSchemas();
    for (const Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* SchedulerToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, SchedulerTool::toolSchemas(), parent);
}
