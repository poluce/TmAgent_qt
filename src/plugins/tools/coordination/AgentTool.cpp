#include "AgentTool.h"
#include "TeammateManager.h"
#include "core/agent/ToolDispatcher.h"
#include "core/backend/BackendPluginManager.h"
#include "core/tools/AgentToolNames.h"
#include <tmagent/plugin/IToolPluginHost.h>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QTimer>

namespace {
QJsonArray stringListToJsonArray(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values)
        array.append(value);
    return array;
}

QStringList availableTeammateBackendIds(TmAgent::IToolPluginHost* host)
{
    // 优先使用 IToolPluginHost 提供的后端列表
    if (host) {
        QStringList ids = host->availableTeammateBackendIds();
        if (!ids.isEmpty())
            return ids;
    }
    
    // 回退到 TeammateManager
    QStringList ids = TeammateManager::instance()->registeredBackendIds();
    if (!ids.isEmpty())
        return ids;
    return BackendPluginManager::instance()->teammateBackendIds();
}

Teammate* resolveTeammate(const QString& ref, const QString& ownerAgentId)
{
    auto* mgr = TeammateManager::instance();
    Teammate* mate = mgr->teammate(ref);
    if (!mate)
        mate = mgr->findByNameForOwner(ref, ownerAgentId);
    if (mate && mate->ownerAgentId() != ownerAgentId)
        return nullptr;
    return mate;
}

} // namespace

AgentTool::AgentTool(TmAgent::IToolPluginHost* host, const LLMConfig& parentConfig, ToolDispatcher* toolDispatcher, const QString& toolName, const QString& toolDesc, QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_parentConfig(parentConfig)
    , m_toolDispatcher(toolDispatcher)
{
    m_schema.name = toolName.trimmed();
    m_schema.description = toolDesc;

    QJsonObject props;
    QJsonArray required;
    if (m_schema.name == QLatin1String("create_teammate")) {
        props[QStringLiteral("name")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。队友名称，用于后续引用。") }
        };
        QJsonObject backendProp {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("后端类型（默认 codex）。可选值取决于已注册的后端。") }
        };
        const QStringList backendIds = availableTeammateBackendIds(m_host);
        if (!backendIds.isEmpty())
            backendProp.insert(QStringLiteral("enum"), stringListToJsonArray(backendIds));
        props[QStringLiteral("backend")] = backendProp;
        props[QStringLiteral("persistence")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("生命周期：persistent(默认) 或 temporary。") },
            { QStringLiteral("enum"), stringListToJsonArray(QStringList { QStringLiteral("persistent"), QStringLiteral("temporary") }) }
        };
        props[QStringLiteral("role")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("队友角色设定 / system prompt（可选）。") }
        };
        props[QStringLiteral("working_directory")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("队友的工作目录（可选）。") }
        };
        props[QStringLiteral("turn_idle_timeout_ms")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("integer") },
            { QStringLiteral("description"), QStringLiteral("Turn 级空闲超时（毫秒），0 表示不超时（可选）。") }
        };
        required.append(QStringLiteral("name"));
    } else if (m_schema.name == QLatin1String("message_teammate")) {
        props[QStringLiteral("teammate")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。队友名称或 ID。") }
        };
        props[QStringLiteral("text")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。要发送给队友的消息内容。") }
        };
        props[QStringLiteral("wait")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("boolean") },
            { QStringLiteral("description"), QStringLiteral("是否同步等待队友回复（默认 true）。") }
        };
        props[QStringLiteral("timeout_ms")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("integer") },
            { QStringLiteral("description"), QStringLiteral("等待回复的超时时间（毫秒），仅 wait=true 时生效，默认 120000。") }
        };
        required.append(QStringLiteral("teammate"));
        required.append(QStringLiteral("text"));
    } else if (m_schema.name == QLatin1String("list_teammates")) {
        props[QStringLiteral("include_idle")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("boolean") },
            { QStringLiteral("description"), QStringLiteral("是否包含空闲队友（默认 true）。") }
        };
        props[QStringLiteral("persistence")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("按生命周期过滤：persistent / temporary（可选）。") },
            { QStringLiteral("enum"), stringListToJsonArray(QStringList { QStringLiteral("persistent"), QStringLiteral("temporary") }) }
        };
        props[QStringLiteral("status")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("按状态过滤：idle / busy / error / shutdown（可选）。") },
            { QStringLiteral("enum"), stringListToJsonArray(QStringList { QStringLiteral("idle"), QStringLiteral("busy"), QStringLiteral("error"), QStringLiteral("shutdown") }) }
        };
    } else if (m_schema.name == QLatin1String("cancel_teammate_turn")) {
        props[QStringLiteral("teammate")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。要取消当前任务的队友名称或 ID。") }
        };
        required.append(QStringLiteral("teammate"));
    } else if (m_schema.name == QLatin1String("remove_teammate")) {
        props[QStringLiteral("teammate")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。要移除的队友名称或 ID。") }
        };
        required.append(QStringLiteral("teammate"));
    } else if (m_schema.name == QLatin1String("rename_teammate")) {
        props[QStringLiteral("teammate")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。要重命名的队友名称或 ID。") }
        };
        props[QStringLiteral("new_name")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。新名称。") }
        };
        required.append(QStringLiteral("teammate"));
        required.append(QStringLiteral("new_name"));
    } else if (m_schema.name == QLatin1String("get_teammate_status")) {
        props[QStringLiteral("teammate")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。要查询的队友名称或 ID。") }
        };
        required.append(QStringLiteral("teammate"));
    } else if (m_schema.name == QLatin1String("message_between_teammates")) {
        props[QStringLiteral("from")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。发送方队友名称或 ID。") }
        };
        props[QStringLiteral("to")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。接收方队友名称或 ID。") }
        };
        props[QStringLiteral("text")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。要转发的消息内容。") }
        };
        required.append(QStringLiteral("from"));
        required.append(QStringLiteral("to"));
        required.append(QStringLiteral("text"));
    }

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    m_schema.inputSchema = schema;
}

TmAgent::Tool AgentTool::buildSchema(const QString& toolName, const QString& toolDesc)
{
    AgentTool tool(nullptr, LLMConfig {}, nullptr, toolName, toolDesc);
    return tool.getSchema();
}

QList<TmAgent::Tool> AgentTool::toolSchemas()
{
    QList<TmAgent::Tool> tools;
    const QStringList names = AgentToolNames::all();
    tools.reserve(names.size());
    for (const QString& name : names)
        tools.append(buildSchema(name, QStringLiteral("团队协作工具")));
    return tools;
}

AgentTool::~AgentTool() = default;

void AgentTool::setOverrideConfig(const LLMConfig& config)
{
    m_overrideConfig = config;
    m_useOverrideConfig = true;
}

Tool AgentTool::getSchema() const
{
    return m_schema;
}

ToolResult AgentTool::execute(const QJsonObject& args)
{
    const QString ownerAgentId = args.value(QStringLiteral("_agent_id")).toString().trimmed();

    if (m_schema.name == QLatin1String("create_teammate")) {
        Teammate::Config config;
        config.name = args.value(QStringLiteral("name")).toString().trimmed();
        config.role = args.value(QStringLiteral("role")).toString().trimmed();
        config.backend = args.value(QStringLiteral("backend")).toString().trimmed();
        config.persistence = args.value(QStringLiteral("persistence")).toString().trimmed();
        config.ownerAgentId = ownerAgentId;
        config.workingDirectory = args.value(QStringLiteral("working_directory")).toString().trimmed();
        // 相对路径基于助手 workspace 解析为绝对路径
        if (!config.workingDirectory.isEmpty() && QDir::isRelativePath(config.workingDirectory)) {
            const QString agentWorkspace = args.value(QStringLiteral("_agent_workspace")).toString().trimmed();
            if (!agentWorkspace.isEmpty())
                config.workingDirectory = QDir::cleanPath(agentWorkspace + QStringLiteral("/") + config.workingDirectory);
        }
        config.turnIdleTimeoutMs = args.value(QStringLiteral("turn_idle_timeout_ms")).toInt(0);
        config.autoCleanup =
            config.persistence.compare(QStringLiteral("temporary"), Qt::CaseInsensitive) == 0;
        config.ephemeralOwnerTurnId = args.value(QStringLiteral("_tool_call_id")).toString().trimmed();

        if (config.name.isEmpty()) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 必须提供队友名称"),
                QStringLiteral("创建失败：缺少名称"),
                false);
        }

        static constexpr int kMaxTeammatesPerAgent = 10;
        if (TeammateManager::instance()->teammatesForOwner(ownerAgentId).size() >= kMaxTeammatesPerAgent) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 每个助手最多创建 %1 个队友").arg(kMaxTeammatesPerAgent),
                QStringLiteral("创建失败：已达上限"),
                false);
        }

        const auto result = TeammateManager::instance()->createTeammate(config);
        if (!result.success) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 创建队友失败 - %1").arg(result.error),
                QStringLiteral("创建队友失败"),
                false);
        }

        Teammate* createdMate = TeammateManager::instance()->teammate(result.teammateId);
        const QString createdBackend = createdMate
            ? createdMate->backend()
            : (config.backend.isEmpty() ? QStringLiteral("codex") : config.backend);
        const QString createdPersistence = createdMate
            ? createdMate->persistence()
            : (config.persistence.compare(QStringLiteral("temporary"), Qt::CaseInsensitive) == 0
                   ? QStringLiteral("temporary")
                   : QStringLiteral("persistent"));
        const QString createdWorkingDirectory = createdMate
            ? createdMate->workingDirectory()
            : config.workingDirectory;

        QJsonObject data;
        data.insert(QStringLiteral("teammate_id"), result.teammateId);
        data.insert(QStringLiteral("thread_id"), result.threadId);
        data.insert(QStringLiteral("name"), config.name);
        data.insert(QStringLiteral("backend"), createdBackend);
        data.insert(QStringLiteral("persistence"), createdPersistence);
        data.insert(QStringLiteral("working_directory"), createdWorkingDirectory);
        return TmAgent::ToolResult(
            QStringLiteral("已创建队友 \"%1\"\nteammate_id: %2\nthread_id: %3\nbackend: %4\npersistence: %5\nworking_directory: %6")
                .arg(config.name,
                     result.teammateId,
                     result.threadId,
                     createdBackend,
                     createdPersistence,
                     createdWorkingDirectory.isEmpty() ? QStringLiteral("(默认)") : createdWorkingDirectory),
            QStringLiteral("队友已创建"),
            true,
            data);
    }

    if (m_schema.name == QLatin1String("message_teammate")) {
        const QString teammateRef = args.value(QStringLiteral("teammate")).toString().trimmed();
        const QString text = args.value(QStringLiteral("text")).toString().trimmed();

        if (teammateRef.isEmpty() || text.isEmpty()) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: teammate 和 text 均不能为空"),
                QStringLiteral("发送失败：参数缺失"),
                false);
        }

        auto* mgr = TeammateManager::instance();
        Teammate* mate = resolveTeammate(teammateRef, ownerAgentId);
        if (!mate) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 未找到队友 \"%1\"").arg(teammateRef),
                QStringLiteral("发送失败：队友不存在"),
                false);
        }

        const auto msgResult = mgr->sendMessage(mate->id(), text);
        if (!msgResult.success) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: %1").arg(msgResult.error),
                QStringLiteral("发送失败"),
                false);
        }

        const bool waitForReply = args.value(QStringLiteral("wait")).toBool(true);
        const int timeoutMs = qBound(
            kMinWaitTimeoutMs,
            args.value(QStringLiteral("timeout_ms")).toInt(kDefaultWaitTimeoutMs),
            kMaxWaitTimeoutMs);

        if (waitForReply) {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);

            bool completed = false;
            bool turnSuccess = false;
            QString turnResult;
            const QMetaObject::Connection conn = QObject::connect(
                mate,
                &Teammate::turnCompleted,
                &loop,
                [&](const QString&, bool success, const QString& result) {
                    completed = true;
                    turnSuccess = success;
                    turnResult = result;
                    loop.quit();
                });
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(timeoutMs);
            loop.exec();
            QObject::disconnect(conn);

            if (!completed) {
                return TmAgent::ToolResult(
                    QStringLiteral("错误: 等待队友 \"%1\" 回复超时（%2ms）").arg(mate->name()).arg(timeoutMs),
                    QStringLiteral("等待队友超时"),
                    false);
            }

            QJsonObject data;
            data.insert(QStringLiteral("teammate_id"), mate->id());
            data.insert(QStringLiteral("teammate_name"), mate->name());
            data.insert(QStringLiteral("status"), turnSuccess ? QStringLiteral("completed")
                                                             : QStringLiteral("failed"));
            data.insert(QStringLiteral("turn_id"), msgResult.turnId);
            return TmAgent::ToolResult(
                turnResult,
                turnSuccess ? QStringLiteral("队友已完成") : QStringLiteral("队友执行失败"),
                turnSuccess,
                data);
        }

        // 异步模式：立即返回，队友回复后通过系统消息推送到会话
        QJsonObject data;
        data.insert(QStringLiteral("teammate_id"), mate->id());
        data.insert(QStringLiteral("teammate_name"), mate->name());
        data.insert(QStringLiteral("status"), QStringLiteral("sent"));
        return TmAgent::ToolResult(
            QStringLiteral("消息已发送给队友 \"%1\"，队友回复后会自动推送到当前会话。").arg(mate->name()),
            QStringLiteral("消息已发送"),
            true,
            data);
    }

    if (m_schema.name == QLatin1String("list_teammates")) {
        const bool includeIdle = args.value(QStringLiteral("include_idle")).toBool(true);
        const QString persistenceFilter =
            args.value(QStringLiteral("persistence")).toString().trimmed().toLower();
        const QString statusFilter =
            args.value(QStringLiteral("status")).toString().trimmed().toLower();
        const auto mates = TeammateManager::instance()->teammatesForOwner(ownerAgentId);
        QJsonArray arr;
        QStringList lines;
        for (const auto* mate : mates) {
            const QString statusStr = Teammate::statusToString(mate->status());
            if (!includeIdle && mate->status() == Teammate::Status::Idle)
                continue;
            if (!persistenceFilter.isEmpty() && mate->persistence() != persistenceFilter)
                continue;
            if (!statusFilter.isEmpty() && statusStr != statusFilter)
                continue;
            arr.append(mate->toJson());
            lines.append(QStringLiteral("- %1 | %2 | %3 | backend=%4 | thread=%5 | turns=%6")
                .arg(mate->name(),
                     statusStr,
                     mate->persistence(),
                     mate->backend(),
                     mate->threadId().left(8),
                     QString::number(mate->turnCount())));
        }
        QJsonObject data;
        data.insert(QStringLiteral("teammates"), arr);
        data.insert(QStringLiteral("count"), arr.size());
        const QString raw = arr.isEmpty()
            ? QStringLiteral("当前没有队友。")
            : QStringLiteral("队友列表:\n%1").arg(lines.join(QStringLiteral("\n")));
        return TmAgent::ToolResult(raw, QStringLiteral("已返回队友列表"), true, data);
    }

    if (m_schema.name == QLatin1String("remove_teammate")) {
        const QString teammateRef = args.value(QStringLiteral("teammate")).toString().trimmed();
        if (teammateRef.isEmpty()) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 必须提供队友名称或 ID"),
                QStringLiteral("移除失败：参数缺失"),
                false);
        }

        auto* mgr = TeammateManager::instance();
        Teammate* mate = resolveTeammate(teammateRef, ownerAgentId);
        if (!mate) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 未找到队友 \"%1\"").arg(teammateRef),
                QStringLiteral("移除失败：队友不存在"),
                false);
        }

        const QString mateName = mate->name();
        const QString mateId = mate->id();
        QString error;
        if (!mgr->removeTeammate(mateId, &error)) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: %1").arg(error),
                QStringLiteral("移除失败"),
                false);
        }

        QJsonObject data;
        data.insert(QStringLiteral("teammate_id"), mateId);
        data.insert(QStringLiteral("name"), mateName);
        return TmAgent::ToolResult(
            QStringLiteral("已移除队友 \"%1\"").arg(mateName),
            QStringLiteral("队友已移除"),
            true,
            data);
    }

    if (m_schema.name == QLatin1String("rename_teammate")) {
        const QString teammateRef = args.value(QStringLiteral("teammate")).toString().trimmed();
        const QString newName = args.value(QStringLiteral("new_name")).toString().trimmed();
        if (teammateRef.isEmpty() || newName.isEmpty()) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: teammate 和 new_name 均不能为空"),
                QStringLiteral("重命名失败：参数缺失"),
                false);
        }

        auto* mgr = TeammateManager::instance();
        Teammate* mate = resolveTeammate(teammateRef, ownerAgentId);
        if (!mate) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 未找到队友 \"%1\"").arg(teammateRef),
                QStringLiteral("重命名失败：队友不存在"),
                false);
        }

        // 检查新名称是否冲突
        Teammate* existing = mgr->findByNameForOwner(newName, ownerAgentId);
        if (existing && existing != mate) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 已存在同名队友 \"%1\"").arg(newName),
                QStringLiteral("重命名失败：名称冲突"),
                false);
        }

        const QString oldName = mate->name();
        mate->setName(newName);

        QJsonObject data;
        data.insert(QStringLiteral("teammate_id"), mate->id());
        data.insert(QStringLiteral("old_name"), oldName);
        data.insert(QStringLiteral("new_name"), newName);
        return TmAgent::ToolResult(
            QStringLiteral("已将队友 \"%1\" 重命名为 \"%2\"").arg(oldName, newName),
            QStringLiteral("队友已重命名"),
            true,
            data);
    }

    if (m_schema.name == QLatin1String("get_teammate_status")) {
        const QString teammateRef = args.value(QStringLiteral("teammate")).toString().trimmed();
        if (teammateRef.isEmpty()) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 必须提供队友名称或 ID"),
                QStringLiteral("查询失败：参数缺失"),
                false);
        }

        Teammate* mate = resolveTeammate(teammateRef, ownerAgentId);
        if (!mate) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 未找到队友 \"%1\"").arg(teammateRef),
                QStringLiteral("查询失败：队友不存在"),
                false);
        }

        const QString statusStr = Teammate::statusToString(mate->status());

        QJsonObject data = mate->toJson();
        const QString raw = QStringLiteral(
            "队友: %1\n"
            "ID: %2\n"
            "状态: %3\n"
            "后端: %4\n"
            "生命周期: %5\n"
            "自动回收: %6\n"
            "Thread: %7\n"
            "活动 Turn: %8\n"
            "Turn 计数: %9\n"
            "工作目录: %10\n"
            "最后错误: %11\n"
            "最后活跃: %12")
            .arg(mate->name(),
                 mate->id(),
                 statusStr,
                 mate->backend(),
                 mate->persistence(),
                 mate->autoCleanup() ? QStringLiteral("yes") : QStringLiteral("no"),
                 mate->threadId().isEmpty() ? QStringLiteral("(未建立)") : mate->threadId(),
                 mate->activeTurnId().isEmpty() ? QStringLiteral("(无)") : mate->activeTurnId(),
                 QString::number(mate->turnCount()),
                 mate->workingDirectory().isEmpty() ? QStringLiteral("(默认)") : mate->workingDirectory(),
                 mate->lastError().isEmpty() ? QStringLiteral("(无)") : mate->lastError(),
                 QDateTime::fromMSecsSinceEpoch(mate->lastActiveAtMs()).toString(Qt::ISODate));
        return TmAgent::ToolResult(raw, QStringLiteral("已返回队友状态"), true, data);
    }

    if (m_schema.name == QLatin1String("cancel_teammate_turn")) {
        const QString teammateRef = args.value(QStringLiteral("teammate")).toString().trimmed();
        if (teammateRef.isEmpty()) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 必须提供队友名称或 ID"),
                QStringLiteral("取消失败：参数缺失"),
                false);
        }

        auto* mgr = TeammateManager::instance();
        Teammate* mate = resolveTeammate(teammateRef, ownerAgentId);
        if (!mate) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 未找到队友 \"%1\"").arg(teammateRef),
                QStringLiteral("取消失败：队友不存在"),
                false);
        }

        QString error;
        if (!mgr->cancelTeammateTurn(mate->id(), &error)) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: %1").arg(error),
                QStringLiteral("取消失败"),
                false);
        }

        QJsonObject data;
        data.insert(QStringLiteral("teammate_id"), mate->id());
        data.insert(QStringLiteral("name"), mate->name());
        data.insert(QStringLiteral("status"), QStringLiteral("cancel_requested"));
        return TmAgent::ToolResult(
            QStringLiteral("已请求取消队友 \"%1\" 的当前任务").arg(mate->name()),
            QStringLiteral("队友任务取消中"),
            true,
            data);
    }

    if (m_schema.name == QLatin1String("message_between_teammates")) {
        const QString fromRef = args.value(QStringLiteral("from")).toString().trimmed();
        const QString toRef = args.value(QStringLiteral("to")).toString().trimmed();
        const QString text = args.value(QStringLiteral("text")).toString().trimmed();
        if (fromRef.isEmpty() || toRef.isEmpty() || text.isEmpty()) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: from、to、text 均不能为空"),
                QStringLiteral("转发失败：参数缺失"),
                false);
        }

        auto* mgr = TeammateManager::instance();
        Teammate* fromMate = resolveTeammate(fromRef, ownerAgentId);
        if (!fromMate) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 未找到发送方队友 \"%1\"").arg(fromRef),
                QStringLiteral("转发失败：发送方不存在"),
                false);
        }

        Teammate* toMate = resolveTeammate(toRef, ownerAgentId);
        if (!toMate) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: 未找到接收方队友 \"%1\"").arg(toRef),
                QStringLiteral("转发失败：接收方不存在"),
                false);
        }

        // 包装消息：标注来源队友
        const QString wrappedText = QStringLiteral("<from-teammate name=\"%1\" id=\"%2\">\n%3\n</from-teammate>")
            .arg(fromMate->name(), fromMate->id(), text);

        const auto msgResult = mgr->sendMessage(toMate->id(), wrappedText);
        if (!msgResult.success) {
            return TmAgent::ToolResult(
                QStringLiteral("错误: %1").arg(msgResult.error),
                QStringLiteral("转发失败"),
                false);
        }

        QJsonObject data;
        data.insert(QStringLiteral("from_id"), fromMate->id());
        data.insert(QStringLiteral("from_name"), fromMate->name());
        data.insert(QStringLiteral("to_id"), toMate->id());
        data.insert(QStringLiteral("to_name"), toMate->name());
        data.insert(QStringLiteral("status"), QStringLiteral("sent"));
        return TmAgent::ToolResult(
            QStringLiteral("已将消息从队友 \"%1\" 转发给队友 \"%2\"，回复后会自动推送到当前会话。")
                .arg(fromMate->name(), toMate->name()),
            QStringLiteral("队友间消息已转发"),
            true,
            data);
    }

    return TmAgent::ToolResult(
        QStringLiteral("错误: 未知的团队协作工具 %1").arg(m_schema.name),
        QStringLiteral("执行失败"),
        false);
}
