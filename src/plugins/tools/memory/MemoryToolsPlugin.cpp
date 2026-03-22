#include "MemoryToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"

namespace {

QList<HostedToolSpec> memoryToolSpecs()
{
    return {
        { QStringLiteral("memory_search"), QStringLiteral("检索助手记忆") },
        { QStringLiteral("memory_reindex"), QStringLiteral("重建助手记忆检索索引") },
        { QStringLiteral("memory_write"), QStringLiteral("主动写入当前助手的长期记忆") },
        { QStringLiteral("session_search"), QStringLiteral("检索会话历史") },
        { QStringLiteral("event_log"), QStringLiteral("事件日志查询工具") }
    };
}

} // namespace

ToolPluginDescriptor MemoryToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("memory_tools");
    descriptor.displayName = QStringLiteral("记忆与检索工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("memory");
    descriptor.description = QStringLiteral("长期记忆、会话检索和事件日志查询工具。");
    const QList<HostedToolSpec> specs = memoryToolSpecs();
    for (const HostedToolSpec& spec : specs)
        descriptor.toolNames.append(spec.name);
    return descriptor;
}

IToolProvider* MemoryToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, memoryToolSpecs(), parent);
}
