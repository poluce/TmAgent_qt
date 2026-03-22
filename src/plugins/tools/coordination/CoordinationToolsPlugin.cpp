#include "CoordinationToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"
#include "core/tools/AgentToolNames.h"
#include "core/tools/ToolSchemaSupport.h"

namespace {

QJsonObject enumProperty(const QStringList& values,
                         const QString& description)
{
    QJsonObject extra;
    if (!values.isEmpty())
        extra = stringListEnum(values);
    return makePropertySchema(QStringLiteral("string"), description, extra);
}

QList<Tool> coordinationTools(IToolPluginHost* host)
{
    const QStringList teammateBackends = host ? host->availableTeammateBackendIds() : QStringList();
    return {
        makeToolSchema(
            QStringLiteral("create_teammate"),
            QStringLiteral("创建一个新的队友。"),
            QJsonObject {
                { QStringLiteral("name"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。队友名称，用于后续引用。")) },
                { QStringLiteral("backend"), enumProperty(teammateBackends, QStringLiteral("后端类型（默认 codex）。可选值取决于已注册的后端。")) },
                { QStringLiteral("persistence"), makePropertySchema(QStringLiteral("string"), QStringLiteral("生命周期：persistent(默认) 或 temporary。"), stringListEnum(QStringList { QStringLiteral("persistent"), QStringLiteral("temporary") })) },
                { QStringLiteral("role"), makePropertySchema(QStringLiteral("string"), QStringLiteral("队友角色设定 / system prompt（可选）。")) },
                { QStringLiteral("working_directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("队友的工作目录（可选）。")) },
                { QStringLiteral("turn_idle_timeout_ms"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("Turn 级空闲超时（毫秒），0 表示不超时（可选）。")) }
            },
            QStringList { QStringLiteral("name") }),
        makeToolSchema(
            QStringLiteral("message_teammate"),
            QStringLiteral("向指定队友发送消息。"),
            QJsonObject {
                { QStringLiteral("teammate"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。队友名称或 ID。")) },
                { QStringLiteral("text"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。要发送给队友的消息内容。")) },
                { QStringLiteral("wait"), makePropertySchema(QStringLiteral("boolean"), QStringLiteral("是否同步等待队友回复（默认 true）。")) },
                { QStringLiteral("timeout_ms"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("等待回复的超时时间（毫秒），仅 wait=true 时生效，默认 120000。")) }
            },
            QStringList { QStringLiteral("teammate"), QStringLiteral("text") }),
        makeToolSchema(
            QStringLiteral("list_teammates"),
            QStringLiteral("列出当前助手已创建的队友。"),
            QJsonObject {
                { QStringLiteral("include_idle"), makePropertySchema(QStringLiteral("boolean"), QStringLiteral("是否包含空闲队友（默认 true）。")) },
                { QStringLiteral("persistence"), makePropertySchema(QStringLiteral("string"), QStringLiteral("按生命周期过滤：persistent / temporary（可选）。"), stringListEnum(QStringList { QStringLiteral("persistent"), QStringLiteral("temporary") })) },
                { QStringLiteral("status"), makePropertySchema(QStringLiteral("string"), QStringLiteral("按状态过滤：idle / busy / error / shutdown（可选）。"), stringListEnum(QStringList { QStringLiteral("idle"), QStringLiteral("busy"), QStringLiteral("error"), QStringLiteral("shutdown") })) }
            }),
        makeToolSchema(
            QStringLiteral("cancel_teammate_turn"),
            QStringLiteral("取消指定队友当前正在执行的任务，但不删除队友。"),
            QJsonObject {
                { QStringLiteral("teammate"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。要取消当前任务的队友名称或 ID。")) }
            },
            QStringList { QStringLiteral("teammate") }),
        makeToolSchema(
            QStringLiteral("remove_teammate"),
            QStringLiteral("移除指定队友。"),
            QJsonObject {
                { QStringLiteral("teammate"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。要移除的队友名称或 ID。")) }
            },
            QStringList { QStringLiteral("teammate") }),
        makeToolSchema(
            QStringLiteral("rename_teammate"),
            QStringLiteral("重命名指定队友。"),
            QJsonObject {
                { QStringLiteral("teammate"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。要重命名的队友名称或 ID。")) },
                { QStringLiteral("new_name"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。新名称。")) }
            },
            QStringList { QStringLiteral("teammate"), QStringLiteral("new_name") }),
        makeToolSchema(
            QStringLiteral("get_teammate_status"),
            QStringLiteral("查询指定队友状态。"),
            QJsonObject {
                { QStringLiteral("teammate"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。要查询的队友名称或 ID。")) }
            },
            QStringList { QStringLiteral("teammate") }),
        makeToolSchema(
            QStringLiteral("message_between_teammates"),
            QStringLiteral("在两个队友之间转发消息。"),
            QJsonObject {
                { QStringLiteral("from"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。发送方队友名称或 ID。")) },
                { QStringLiteral("to"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。接收方队友名称或 ID。")) },
                { QStringLiteral("text"), makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。要转发的消息内容。")) }
            },
            QStringList { QStringLiteral("from"), QStringLiteral("to"), QStringLiteral("text") })
    };
}

} // namespace

ToolPluginDescriptor CoordinationToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("coordination_tools");
    descriptor.displayName = QStringLiteral("团队协作工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("coordination");
    descriptor.description = QStringLiteral("团队协作、队友管理和队友间消息转发工具。");
    descriptor.toolNames = AgentToolNames::all();
    return descriptor;
}

IToolProvider* CoordinationToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, coordinationTools(host), parent);
}
