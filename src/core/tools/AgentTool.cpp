#include "AgentTool.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/agent/ToolDispatcher.h"
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
    if (!job.summary.trimmed().isEmpty())
        lines << QStringLiteral("summary: %1").arg(job.summary.trimmed());
    if (!job.failureReason.trimmed().isEmpty())
        lines << QStringLiteral("failure_reason: %1").arg(job.failureReason.trimmed());
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
    obj.insert(QStringLiteral("summary"), job.summary);
    obj.insert(QStringLiteral("failure_reason"), job.failureReason);
    obj.insert(QStringLiteral("task"), job.task);
    obj.insert(QStringLiteral("result"), job.result);
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

    QString task = args.value(QStringLiteral("task")).toString().trimmed();
    if (task.size() > kMaxTaskChars)
        task = task.left(kMaxTaskChars) + QStringLiteral("\n...[task truncated]...");

    const QString rolePrompt = args.value(QStringLiteral("role_prompt")).toString().trimmed();
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
