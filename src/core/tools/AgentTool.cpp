#include "AgentTool.h"
#include "TeammateManager.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/agent/ToolDispatcher.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>

namespace {
LLMConfig resolveParentConfigFromArgs(const LLMConfig& fallback, const QJsonObject& args)
{
    LLMConfig cfg = fallback;

    const QString agentId = args.value(QStringLiteral("_agent_id")).toString().trimmed();
    if (!agentId.isEmpty())
        cfg.uuid = agentId;

    const int recursionDepth = args.value(QStringLiteral("_agent_recursion_depth"))
                                   .toInt(cfg.recursionDepth);
    cfg.recursionDepth = qMax(0, recursionDepth);

    const QString configId = args.value(QStringLiteral("_agent_config_id")).toString().trimmed();
    if (!configId.isEmpty())
        cfg.configId = configId;

    const QString workspace = args.value(QStringLiteral("_agent_workspace")).toString().trimmed();
    if (!workspace.isEmpty())
        cfg.workspaceDir = workspace;

    return cfg;
}

QStringList jsonArrayToStringList(const QJsonValue& value)
{
    QStringList out;
    const QJsonArray arr = value.toArray();
    out.reserve(arr.size());
    for (const QJsonValue& item : arr) {
        const QString toolName = item.toString().trimmed();
        if (!toolName.isEmpty())
            out.append(toolName);
    }
    out.removeDuplicates();
    return out;
}

QString formatJobInfoText(const DelegateTaskScheduler::JobInfo& job)
{
    QStringList lines;
    lines << QStringLiteral("job_id: %1").arg(job.jobId);
    lines << QStringLiteral("status: %1").arg(job.status);
    if (!job.backend.trimmed().isEmpty())
        lines << QStringLiteral("backend: %1").arg(job.backend.trimmed());
    if (!job.summary.trimmed().isEmpty())
        lines << QStringLiteral("summary: %1").arg(job.summary.trimmed());
    if (!job.failureReason.trimmed().isEmpty())
        lines << QStringLiteral("failure_reason: %1").arg(job.failureReason.trimmed());
    if (!job.backendThreadId.trimmed().isEmpty())
        lines << QStringLiteral("backend_thread_id: %1").arg(job.backendThreadId.trimmed());
    if (!job.backendTurnId.trimmed().isEmpty())
        lines << QStringLiteral("backend_turn_id: %1").arg(job.backendTurnId.trimmed());
    if (!job.backendProgram.trimmed().isEmpty())
        lines << QStringLiteral("backend_program: %1").arg(job.backendProgram.trimmed());
    lines << QStringLiteral("created_at_ms: %1").arg(job.createdAtMs);
    if (job.startedAtMs > 0)
        lines << QStringLiteral("started_at_ms: %1").arg(job.startedAtMs);
    if (job.finishedAtMs > 0)
        lines << QStringLiteral("finished_at_ms: %1").arg(job.finishedAtMs);
    if (!job.task.trimmed().isEmpty())
        lines << QStringLiteral("task: %1").arg(job.task.trimmed());
    if (!job.result.trimmed().isEmpty())
        lines << QStringLiteral("result: %1").arg(job.result.trimmed());
    return lines.join(QStringLiteral("\n"));
}

QJsonObject jobInfoToJson(const DelegateTaskScheduler::JobInfo& job)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("job_id"), job.jobId);
    obj.insert(QStringLiteral("owner_agent_id"), job.ownerAgentId);
    obj.insert(QStringLiteral("status"), job.status);
    obj.insert(QStringLiteral("backend"), job.backend);
    obj.insert(QStringLiteral("summary"), job.summary);
    obj.insert(QStringLiteral("failure_reason"), job.failureReason);
    obj.insert(QStringLiteral("task"), job.task);
    obj.insert(QStringLiteral("result"), job.result);
    obj.insert(QStringLiteral("backend_thread_id"), job.backendThreadId);
    obj.insert(QStringLiteral("backend_turn_id"), job.backendTurnId);
    obj.insert(QStringLiteral("backend_program"), job.backendProgram);
    obj.insert(QStringLiteral("created_at_ms"), static_cast<double>(job.createdAtMs));
    obj.insert(QStringLiteral("started_at_ms"), static_cast<double>(job.startedAtMs));
    obj.insert(QStringLiteral("last_progress_at_ms"), static_cast<double>(job.lastProgressAtMs));
    obj.insert(QStringLiteral("finished_at_ms"), static_cast<double>(job.finishedAtMs));
    obj.insert(QStringLiteral("expected_timeout_ms"), job.expectedTimeoutMs);
    obj.insert(QStringLiteral("hard_timeout_ms"), job.hardTimeoutMs);
    obj.insert(QStringLiteral("stall_no_progress_ms"), job.stallNoProgressMs);
    obj.insert(QStringLiteral("child_tool_started_count"), job.childToolStartedCount);
    obj.insert(QStringLiteral("child_tool_progress_count"), job.childToolProgressCount);
    obj.insert(QStringLiteral("child_tool_completed_count"), job.childToolCompletedCount);
    obj.insert(QStringLiteral("child_tool_success_count"), job.childToolSuccessCount);
    obj.insert(QStringLiteral("child_tool_failure_count"), job.childToolFailureCount);
    obj.insert(QStringLiteral("child_stream_chunk_count"), job.childStreamChunkCount);
    obj.insert(QStringLiteral("child_stream_chars"), job.childStreamChars);
    obj.insert(QStringLiteral("child_tools"), QJsonArray::fromStringList(job.childTools));
    return obj;
}
} // namespace

AgentTool::AgentTool(const LLMConfig& parentConfig, ToolDispatcher* toolDispatcher, const QString& toolName, const QString& toolDesc, QObject* parent)
    : QObject(parent)
    , m_parentConfig(parentConfig)
    , m_toolDispatcher(toolDispatcher)
{
    m_schema.name = toolName.isEmpty() ? QStringLiteral("delegate_task") : toolName;
    m_schema.description = toolDesc;

    QJsonObject props;
    QJsonArray required;
    if (m_schema.name == QLatin1String("delegate_task")) {
        props[QStringLiteral("task")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。需要委派给后台子智能体的具体任务描述。") }
        };
        props[QStringLiteral("role_prompt")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("子智能体角色设定（可选）。") }
        };
        props[QStringLiteral("backend")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("enum"), QJsonArray { QStringLiteral("tmagent"), QStringLiteral("codex") } },
            { QStringLiteral("description"), QStringLiteral("委派后端。默认 tmagent；指定 codex 时交给 Codex app-server 子代理执行。") }
        };
        props[QStringLiteral("restrict_delegation")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("boolean") },
            { QStringLiteral("description"), QStringLiteral("是否禁止子智能体继续委派。") }
        };
        props[QStringLiteral("timeout_ms")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("integer") },
            { QStringLiteral("description"), QString("预计执行时长（毫秒，范围 %1-%2）。").arg(kMinDelegateTimeoutMs).arg(kMaxDelegateTimeoutMs) }
        };
        props[QStringLiteral("max_response_chars")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("integer") },
            { QStringLiteral("description"), QStringLiteral("后台结果最大字符数，超长会截断。") }
        };
        required.append(QStringLiteral("task"));
    } else if (m_schema.name == QLatin1String("delegate_status")) {
        props[QStringLiteral("job_id")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("任务 ID。为空时默认返回最近活跃任务。") }
        };
    } else if (m_schema.name == QLatin1String("delegate_cancel")) {
        props[QStringLiteral("job_id")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。要取消的后台任务 ID。") }
        };
        required.append(QStringLiteral("job_id"));
    } else if (m_schema.name == QLatin1String("delegate_list_active")) {
        props[QStringLiteral("limit")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("integer") },
            { QStringLiteral("description"), QStringLiteral("返回条数上限，默认 20。") }
        };
    } else if (m_schema.name == QLatin1String("create_teammate")) {
        props[QStringLiteral("name")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("必填。队友名称，用于后续引用。") }
        };
        props[QStringLiteral("backend")] = QJsonObject {
            { QStringLiteral("type"), QStringLiteral("string") },
            { QStringLiteral("description"), QStringLiteral("后端类型（默认 codex）。可选值取决于已注册的后端。") }
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
    }

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    m_schema.inputSchema = schema;
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

    if (m_schema.name == QLatin1String("delegate_status")) {
        DelegateTaskScheduler::JobInfo info;
        const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
        if (jobId.isEmpty()) {
            const QList<DelegateTaskScheduler::JobInfo> jobs =
                DelegateTaskScheduler::instance()->listJobs(ownerAgentId, true, 1);
            if (jobs.isEmpty()) {
                return ToolResult(
                    QStringLiteral("未找到运行中的后台子代理任务。"),
                    QStringLiteral("暂无后台任务"),
                    true);
            }
            info = jobs.first();
        } else if (!DelegateTaskScheduler::instance()->queryJob(jobId, ownerAgentId, &info)) {
            return ToolResult(
                QStringLiteral("错误: 未找到 job_id=%1 对应任务，或无权限访问。").arg(jobId),
                QStringLiteral("未找到后台任务"),
                false);
        }

        return ToolResult(
            formatJobInfoText(info),
            QStringLiteral("已返回后台任务状态"),
            true,
            jobInfoToJson(info));
    }

    if (m_schema.name == QLatin1String("delegate_list_active")) {
        const int limit = qBound(1, args.value(QStringLiteral("limit")).toInt(20), 100);
        const QList<DelegateTaskScheduler::JobInfo> jobs =
            DelegateTaskScheduler::instance()->listJobs(ownerAgentId, true, limit);
        QJsonArray arr;
        QStringList lines;
        for (const DelegateTaskScheduler::JobInfo& job : jobs) {
            arr.append(jobInfoToJson(job));
            lines.append(QStringLiteral("- %1 | %2 | %3").arg(job.jobId, job.status, job.summary));
        }
        const QString raw = jobs.isEmpty()
            ? QStringLiteral("无运行中的后台子代理任务。")
            : QStringLiteral("运行中任务:\n%1").arg(lines.join(QStringLiteral("\n")));
        QJsonObject data;
        data.insert(QStringLiteral("jobs"), arr);
        data.insert(QStringLiteral("count"), jobs.size());
        return ToolResult(raw, QStringLiteral("已返回运行中任务列表"), true, data);
    }

    if (m_schema.name == QLatin1String("delegate_cancel")) {
        const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
        if (jobId.isEmpty()) {
            return ToolResult(
                QStringLiteral("错误: delegate_cancel 必须提供非空 job_id"),
                QStringLiteral("取消失败：缺少 job_id"),
                false);
        }
        QString error;
        const bool ok = DelegateTaskScheduler::instance()->cancelJob(jobId, ownerAgentId, &error);
        if (!ok) {
            return ToolResult(
                QStringLiteral("错误: 取消后台任务失败(%1)").arg(error),
                QStringLiteral("后台任务取消失败"),
                false);
        }
        QJsonObject data;
        data.insert(QStringLiteral("job_id"), jobId);
        data.insert(QStringLiteral("status"), QStringLiteral("cancelled"));
        return ToolResult(
            QStringLiteral("已取消后台任务 job_id=%1").arg(jobId),
            QStringLiteral("后台任务已取消"),
            true,
            data);
    }

    // ── Codex 队友工具 ──

    if (m_schema.name == QLatin1String("create_teammate")) {
        Teammate::Config config;
        config.name = args.value(QStringLiteral("name")).toString().trimmed();
        config.role = args.value(QStringLiteral("role")).toString().trimmed();
        config.backend = args.value(QStringLiteral("backend")).toString().trimmed();
        config.ownerAgentId = ownerAgentId;
        config.workingDirectory = args.value(QStringLiteral("working_directory")).toString().trimmed();
        // 相对路径基于助手 workspace 解析为绝对路径
        if (!config.workingDirectory.isEmpty() && QDir::isRelativePath(config.workingDirectory)) {
            const QString agentWorkspace = args.value(QStringLiteral("_agent_workspace")).toString().trimmed();
            if (!agentWorkspace.isEmpty())
                config.workingDirectory = QDir::cleanPath(agentWorkspace + QStringLiteral("/") + config.workingDirectory);
        }
        config.turnIdleTimeoutMs = args.value(QStringLiteral("turn_idle_timeout_ms")).toInt(0);

        if (config.name.isEmpty()) {
            return ToolResult(
                QStringLiteral("错误: 必须提供队友名称"),
                QStringLiteral("创建失败：缺少名称"),
                false);
        }

        static constexpr int kMaxTeammatesPerAgent = 10;
        if (TeammateManager::instance()->teammatesForOwner(ownerAgentId).size() >= kMaxTeammatesPerAgent) {
            return ToolResult(
                QStringLiteral("错误: 每个助手最多创建 %1 个队友").arg(kMaxTeammatesPerAgent),
                QStringLiteral("创建失败：已达上限"),
                false);
        }

        const auto result = TeammateManager::instance()->createTeammate(config);
        if (!result.success) {
            return ToolResult(
                QStringLiteral("错误: 创建队友失败 - %1").arg(result.error),
                QStringLiteral("创建队友失败"),
                false);
        }

        QJsonObject data;
        data.insert(QStringLiteral("teammate_id"), result.teammateId);
        data.insert(QStringLiteral("thread_id"), result.threadId);
        data.insert(QStringLiteral("name"), config.name);
        return ToolResult(
            QStringLiteral("已创建 Codex 队友 \"%1\"\nteammate_id: %2\nthread_id: %3")
                .arg(config.name, result.teammateId, result.threadId),
            QStringLiteral("Codex 队友已创建"),
            true,
            data);
    }

    if (m_schema.name == QLatin1String("message_teammate")) {
        const QString teammateRef = args.value(QStringLiteral("teammate")).toString().trimmed();
        const QString text = args.value(QStringLiteral("text")).toString().trimmed();

        if (teammateRef.isEmpty() || text.isEmpty()) {
            return ToolResult(
                QStringLiteral("错误: teammate 和 text 均不能为空"),
                QStringLiteral("发送失败：参数缺失"),
                false);
        }

        auto* mgr = TeammateManager::instance();
        Teammate* mate = mgr->teammate(teammateRef);
        if (!mate)
            mate = mgr->findByNameForOwner(teammateRef, ownerAgentId);
        if (!mate || mate->ownerAgentId() != ownerAgentId) {
            return ToolResult(
                QStringLiteral("错误: 未找到队友 \"%1\"").arg(teammateRef),
                QStringLiteral("发送失败：队友不存在"),
                false);
        }

        const auto msgResult = mgr->sendMessage(mate->id(), text);
        if (!msgResult.success) {
            return ToolResult(
                QStringLiteral("错误: %1").arg(msgResult.error),
                QStringLiteral("发送失败"),
                false);
        }

        // 异步模式：立即返回，队友回复后通过系统消息推送到会话
        QJsonObject data;
        data.insert(QStringLiteral("teammate_id"), mate->id());
        data.insert(QStringLiteral("teammate_name"), mate->name());
        data.insert(QStringLiteral("status"), QStringLiteral("sent"));
        return ToolResult(
            QStringLiteral("消息已发送给队友 \"%1\"，队友回复后会自动推送到当前会话。").arg(mate->name()),
            QStringLiteral("消息已发送"),
            true,
            data);
    }

    if (m_schema.name == QLatin1String("list_teammates")) {
        const auto mates = TeammateManager::instance()->teammatesForOwner(ownerAgentId);
        QJsonArray arr;
        QStringList lines;
        for (const auto* mate : mates) {
            arr.append(mate->toJson());
            const QString statusStr =
                mate->status() == Teammate::Status::Idle ? QStringLiteral("idle")
                : mate->status() == Teammate::Status::Busy ? QStringLiteral("busy")
                : mate->status() == Teammate::Status::Error ? QStringLiteral("error")
                : QStringLiteral("shutdown");
            lines.append(QStringLiteral("- %1 | %2 | thread=%3 | turns=%4")
                .arg(mate->name(), statusStr, mate->threadId().left(8), QString::number(mate->turnCount())));
        }
        QJsonObject data;
        data.insert(QStringLiteral("teammates"), arr);
        data.insert(QStringLiteral("count"), mates.size());
        const QString raw = mates.isEmpty()
            ? QStringLiteral("当前没有队友。")
            : QStringLiteral("队友列表:\n%1").arg(lines.join(QStringLiteral("\n")));
        return ToolResult(raw, QStringLiteral("已返回队友列表"), true, data);
    }

    if (m_schema.name == QLatin1String("remove_teammate")) {
        const QString teammateRef = args.value(QStringLiteral("teammate")).toString().trimmed();
        if (teammateRef.isEmpty()) {
            return ToolResult(
                QStringLiteral("错误: 必须提供队友名称或 ID"),
                QStringLiteral("移除失败：参数缺失"),
                false);
        }

        auto* mgr = TeammateManager::instance();
        Teammate* mate = mgr->teammate(teammateRef);
        if (!mate)
            mate = mgr->findByNameForOwner(teammateRef, ownerAgentId);
        if (!mate || mate->ownerAgentId() != ownerAgentId) {
            return ToolResult(
                QStringLiteral("错误: 未找到队友 \"%1\"").arg(teammateRef),
                QStringLiteral("移除失败：队友不存在"),
                false);
        }

        const QString mateName = mate->name();
        const QString mateId = mate->id();
        QString error;
        if (!mgr->removeTeammate(mateId, &error)) {
            return ToolResult(
                QStringLiteral("错误: %1").arg(error),
                QStringLiteral("移除失败"),
                false);
        }

        QJsonObject data;
        data.insert(QStringLiteral("teammate_id"), mateId);
        data.insert(QStringLiteral("name"), mateName);
        return ToolResult(
            QStringLiteral("已移除队友 \"%1\"").arg(mateName),
            QStringLiteral("队友已移除"),
            true,
            data);
    }

    if (m_schema.name == QLatin1String("rename_teammate")) {
        const QString teammateRef = args.value(QStringLiteral("teammate")).toString().trimmed();
        const QString newName = args.value(QStringLiteral("new_name")).toString().trimmed();
        if (teammateRef.isEmpty() || newName.isEmpty()) {
            return ToolResult(
                QStringLiteral("错误: teammate 和 new_name 均不能为空"),
                QStringLiteral("重命名失败：参数缺失"),
                false);
        }

        auto* mgr = TeammateManager::instance();
        Teammate* mate = mgr->teammate(teammateRef);
        if (!mate)
            mate = mgr->findByNameForOwner(teammateRef, ownerAgentId);
        if (!mate || mate->ownerAgentId() != ownerAgentId) {
            return ToolResult(
                QStringLiteral("错误: 未找到队友 \"%1\"").arg(teammateRef),
                QStringLiteral("重命名失败：队友不存在"),
                false);
        }

        // 检查新名称是否冲突
        Teammate* existing = mgr->findByNameForOwner(newName, ownerAgentId);
        if (existing && existing != mate) {
            return ToolResult(
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
        return ToolResult(
            QStringLiteral("已将队友 \"%1\" 重命名为 \"%2\"").arg(oldName, newName),
            QStringLiteral("队友已重命名"),
            true,
            data);
    }

    if (m_schema.name == QLatin1String("get_teammate_status")) {
        const QString teammateRef = args.value(QStringLiteral("teammate")).toString().trimmed();
        if (teammateRef.isEmpty()) {
            return ToolResult(
                QStringLiteral("错误: 必须提供队友名称或 ID"),
                QStringLiteral("查询失败：参数缺失"),
                false);
        }

        auto* mgr = TeammateManager::instance();
        Teammate* mate = mgr->teammate(teammateRef);
        if (!mate)
            mate = mgr->findByNameForOwner(teammateRef, ownerAgentId);
        if (!mate || mate->ownerAgentId() != ownerAgentId) {
            return ToolResult(
                QStringLiteral("错误: 未找到队友 \"%1\"").arg(teammateRef),
                QStringLiteral("查询失败：队友不存在"),
                false);
        }

        const QString statusStr =
            mate->status() == Teammate::Status::Idle ? QStringLiteral("idle")
            : mate->status() == Teammate::Status::Busy ? QStringLiteral("busy")
            : mate->status() == Teammate::Status::Error ? QStringLiteral("error")
            : QStringLiteral("shutdown");

        QJsonObject data = mate->toJson();
        const QString raw = QStringLiteral(
            "队友: %1\n"
            "ID: %2\n"
            "状态: %3\n"
            "后端: %4\n"
            "Thread: %5\n"
            "Turn 计数: %6\n"
            "工作目录: %7\n"
            "最后错误: %8\n"
            "最后活跃: %9")
            .arg(mate->name(),
                 mate->id(),
                 statusStr,
                 mate->backend(),
                 mate->threadId(),
                 QString::number(mate->turnCount()),
                 mate->workingDirectory().isEmpty() ? QStringLiteral("(默认)") : mate->workingDirectory(),
                 mate->lastError().isEmpty() ? QStringLiteral("(无)") : mate->lastError(),
                 QDateTime::fromMSecsSinceEpoch(mate->lastActiveAtMs()).toString(Qt::ISODate));
        return ToolResult(raw, QStringLiteral("已返回队友状态"), true, data);
    }

    // ── delegate_task（原有逻辑）──
    QString task = args.value(QStringLiteral("task")).toString().trimmed();
    if (task.size() > kMaxTaskChars)
        task = task.left(kMaxTaskChars) + QStringLiteral("\n...[task truncated]...");

    const QString rolePrompt = args.value(QStringLiteral("role_prompt")).toString().trimmed();
    const QString backend = args.value(QStringLiteral("backend")).toString().trimmed();
    const bool restrictDelegation = args.value(QStringLiteral("restrict_delegation")).toBool(false);
    int timeoutMs = args.value(QStringLiteral("timeout_ms")).toInt(kDefaultDelegateTimeoutMs);
    int maxResponseChars = args.value(QStringLiteral("max_response_chars")).toInt(kDefaultMaxResponseChars);
    timeoutMs = qBound(kMinDelegateTimeoutMs, timeoutMs, kMaxDelegateTimeoutMs);
    maxResponseChars = qBound(500, maxResponseChars, 20000);

    const QStringList inheritedAllowedTools = jsonArrayToStringList(
        args.value(QStringLiteral("_agent_allowed_tools")));
    const LLMConfig parentConfig = resolveParentConfigFromArgs(m_parentConfig, args);

    DelegateTaskScheduler::Request request;
    request.delegateToolName = m_schema.name;
    request.task = task;
    request.rolePrompt = rolePrompt;
    request.backend = backend;
    request.restrictDelegation = restrictDelegation;
    request.expectedTimeoutMs = timeoutMs;
    request.maxResponseChars = maxResponseChars;
    request.inheritedAllowedTools = inheritedAllowedTools;
    request.parentConfig = parentConfig;
    request.useOverrideConfig = m_useOverrideConfig;
    request.overrideConfig = m_overrideConfig;
    request.toolDispatcher = m_toolDispatcher;

    const DelegateTaskScheduler::Result delegateResult =
        DelegateTaskScheduler::instance()->submitAsync(request, ownerAgentId);
    return ToolResult(
        delegateResult.rawResult,
        delegateResult.userSummary,
        delegateResult.success,
        delegateResult.data);
}
