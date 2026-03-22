#include "MemoryToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"
#include "core/tools/ToolSchemaSupport.h"

namespace {

QList<Tool> memoryTools()
{
    return {
        makeToolSchema(
            QStringLiteral("memory_search"),
            QStringLiteral("搜索助手记忆文档（memory.md / user_view.md / daily memory）。"),
            QJsonObject {
                { QStringLiteral("query"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索关键词")) },
                { QStringLiteral("scope"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索范围：self(默认) / all")) },
                { QStringLiteral("agent_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定助手 ID")) },
                { QStringLiteral("include_daily"), makePropertySchema(QStringLiteral("boolean"), QStringLiteral("是否包含 daily 记忆日志（默认 true）")) },
                { QStringLiteral("max_results"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("最多返回命中条数（默认 10，范围 1-100）")) },
                { QStringLiteral("max_snippet_chars"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("每条命中摘要最大长度（默认 180）")) }
            },
            QStringList { QStringLiteral("query") }),
        makeToolSchema(
            QStringLiteral("memory_reindex"),
            QStringLiteral("重建助手记忆检索索引（SQLite FTS 派生索引）。"),
            QJsonObject {
                { QStringLiteral("scope"), makePropertySchema(QStringLiteral("string"), QStringLiteral("重建范围：self(默认) / all")) },
                { QStringLiteral("agent_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定助手 ID")) }
            }),
        makeToolSchema(
            QStringLiteral("memory_write"),
            QStringLiteral("主动写入当前助手的长期记忆（memory.md）。"),
            QJsonObject {
                { QStringLiteral("memory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要写入长期记忆的内容")) },
                { QStringLiteral("reason"), makePropertySchema(QStringLiteral("string"), QStringLiteral("写入原因（可选，用于审计说明）")) }
            },
            QStringList { QStringLiteral("memory") }),
        makeToolSchema(
            QStringLiteral("session_search"),
            QStringLiteral("检索会话历史（messages.jsonl）。"),
            QJsonObject {
                { QStringLiteral("query"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索关键词")) },
                { QStringLiteral("scope"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索范围：self(默认) / all")) },
                { QStringLiteral("agent_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定助手 ID")) },
                { QStringLiteral("session_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定会话 ID，仅在该会话中检索")) },
                { QStringLiteral("include_tool_messages"), makePropertySchema(QStringLiteral("boolean"), QStringLiteral("是否包含 tool_call/tool_result 消息（默认 false）")) },
                { QStringLiteral("max_results"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("最多返回命中条数（默认 20，范围 1-200）")) },
                { QStringLiteral("max_snippet_chars"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("每条命中摘要最大长度（默认 220）")) }
            },
            QStringList { QStringLiteral("query") }),
        makeToolSchema(
            QStringLiteral("event_log"),
            QStringLiteral("日志查询。通过 action 选择操作: search、sessions、agents。"),
            QJsonObject {
                { QStringLiteral("action"), makePropertySchema(QStringLiteral("string"), QStringLiteral("操作: search(默认) / sessions / agents")) }
            })
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
    const QList<Tool> tools = memoryTools();
    for (const Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* MemoryToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, memoryTools(), parent);
}
