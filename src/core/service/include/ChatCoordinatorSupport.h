#ifndef CHATCOORDINATORSUPPORT_H
#define CHATCOORDINATORSUPPORT_H

#include "core/agent/DelegateTaskScheduler.h"
#include "core/agent/ToolTypes.h"
#include "AgentPulse.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace ChatCoordinatorSupport {

namespace detail {

inline bool isDelegateStatusLikeTool(const QString& toolName)
{
    return toolName == QLatin1String("delegate_status")
        || toolName == QLatin1String("delegate_list_active");
}

inline int estimateHistoryMessageChars(const QJsonObject& msg)
{
    const QString role = msg.value(QStringLiteral("role")).toString();
    const QString content = msg.value(QStringLiteral("content")).toString();
    int size = role.size() + content.size();
    if (msg.contains(QStringLiteral("tool_call_id")))
        size += msg.value(QStringLiteral("tool_call_id")).toString().size();
    if (msg.contains(QStringLiteral("tool_calls"))) {
        const QJsonArray toolCalls = msg.value(QStringLiteral("tool_calls")).toArray();
        const QByteArray toolCallsJson = QJsonDocument(toolCalls).toJson(QJsonDocument::Compact);
        size += qMin(toolCallsJson.size(), 4096);
    }
    return size;
}

} // namespace detail

inline QJsonObject toolEventToJson(const ToolExecutionEvent& event)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("toolName"), event.toolName);
    obj.insert(QStringLiteral("toolId"), event.toolId);
    obj.insert(QStringLiteral("status"), event.status);
    obj.insert(QStringLiteral("success"), event.success);
    obj.insert(QStringLiteral("data"), event.data);
    obj.insert(QStringLiteral("rawResult"), event.rawResult);
    obj.insert(QStringLiteral("formattedResult"), event.formattedResult);
    return obj;
}

inline QString pulseStateToString(AgentPulse::State state)
{
    switch (state) {
    case AgentPulse::Healthy:
        return QStringLiteral("healthy");
    case AgentPulse::SoftTimeout:
        return QStringLiteral("soft_timeout");
    case AgentPulse::Stalled:
        return QStringLiteral("stalled");
    case AgentPulse::HardTimeout:
        return QStringLiteral("hard_timeout");
    case AgentPulse::Dead:
        return QStringLiteral("dead");
    }
    return QStringLiteral("unknown");
}

inline bool isTransientUpstreamError(const QString& errorMsg)
{
    const QString e = errorMsg.trimmed().toLower();
    if (e.isEmpty())
        return false;

    if (e.contains(QStringLiteral("bad request"))
        || e.contains(QStringLiteral("unauthorized"))
        || e.contains(QStringLiteral("forbidden"))
        || e.contains(QStringLiteral("not found"))
        || e.contains(QStringLiteral("invalid"))
        || e.contains(QStringLiteral("unprocessable"))) {
        return false;
    }

    return e.contains(QStringLiteral("internal server error"))
        || e.contains(QStringLiteral("bad gateway"))
        || e.contains(QStringLiteral("gateway timeout"))
        || e.contains(QStringLiteral("service unavailable"))
        || e.contains(QStringLiteral("server replied: 5"))
        || e.contains(QStringLiteral("connection reset"))
        || e.contains(QStringLiteral("connection closed"))
        || e.contains(QStringLiteral("temporarily unavailable"))
        || e.contains(QStringLiteral("network timeout"))
        || e.contains(QStringLiteral("timed out"));
}

inline QString buildDelegateRecoveryReply(const QList<DelegateTaskScheduler::JobInfo>& jobs)
{
    QStringList lines;
    lines << QStringLiteral("我这轮遇到了上游网络临时故障，但你发起的子代理任务还在后台继续执行。");
    lines << QStringLiteral("当前可跟踪任务：");

    const int maxPreview = qMin(3, jobs.size());
    for (int i = 0; i < maxPreview; ++i) {
        const DelegateTaskScheduler::JobInfo& job = jobs.at(i);
        const QString id = job.jobId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : job.jobId.trimmed();
        const QString status = job.status.trimmed().isEmpty() ? QStringLiteral("running") : job.status.trimmed();
        QString line = QStringLiteral("- job_id=%1 status=%2").arg(id, status);
        if (!job.backend.trimmed().isEmpty())
            line += QStringLiteral(" backend=%1").arg(job.backend.trimmed());
        if (!job.backendThreadId.trimmed().isEmpty())
            line += QStringLiteral(" thread=%1").arg(job.backendThreadId.left(12));
        lines << line;
    }
    if (jobs.size() > maxPreview)
        lines << QStringLiteral("- ... 还有 %1 个任务在运行").arg(jobs.size() - maxPreview);

    lines << QStringLiteral("你可以直接说“查看子代理进度”，我会立即汇报；也可以说“取消 job_id=xxx”。");
    return lines.join(QStringLiteral("\n"));
}

inline QString delegateToolKey(const QString& sessionId, const QString& toolId)
{
    return sessionId.trimmed() + QStringLiteral("|") + toolId.trimmed();
}

inline QJsonObject sanitizePersistedToolArguments(const QString& toolName, const QJsonObject& args)
{
    if (toolName == QLatin1String("delegate_status")) {
        QJsonObject compact;
        const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
        if (!jobId.isEmpty())
            compact.insert(QStringLiteral("job_id"), jobId);
        return compact;
    }

    if (toolName == QLatin1String("delegate_list_active")) {
        QJsonObject compact;
        if (args.contains(QStringLiteral("limit")))
            compact.insert(QStringLiteral("limit"), args.value(QStringLiteral("limit")));
        return compact;
    }

    if (toolName == QLatin1String("delegate_cancel")) {
        QJsonObject compact;
        const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
        if (!jobId.isEmpty())
            compact.insert(QStringLiteral("job_id"), jobId);
        return compact;
    }

    return args;
}

inline QJsonObject sanitizePersistedToolEventData(const QString& toolName, const QJsonObject& data)
{
    if (!detail::isDelegateStatusLikeTool(toolName))
        return data;

    QJsonObject compact;
    const auto copyField = [&](const QString& key) {
        if (data.contains(key))
            compact.insert(key, data.value(key));
    };

    copyField(QStringLiteral("job_id"));
    copyField(QStringLiteral("owner_agent_id"));
    copyField(QStringLiteral("status"));
    copyField(QStringLiteral("summary"));
    copyField(QStringLiteral("failure_reason"));
    copyField(QStringLiteral("backend"));
    copyField(QStringLiteral("backend_thread_id"));
    copyField(QStringLiteral("backend_turn_id"));
    copyField(QStringLiteral("backend_program"));
    copyField(QStringLiteral("created_at_ms"));
    copyField(QStringLiteral("started_at_ms"));
    copyField(QStringLiteral("last_progress_at_ms"));
    copyField(QStringLiteral("finished_at_ms"));
    copyField(QStringLiteral("expected_timeout_ms"));
    copyField(QStringLiteral("hard_timeout_ms"));
    copyField(QStringLiteral("stall_no_progress_ms"));
    copyField(QStringLiteral("child_tool_completed_count"));
    copyField(QStringLiteral("child_tool_failure_count"));
    copyField(QStringLiteral("child_tool_success_count"));
    copyField(QStringLiteral("child_stream_chunk_count"));
    copyField(QStringLiteral("child_stream_chars"));

    QString task = data.value(QStringLiteral("task")).toString().trimmed();
    if (!task.isEmpty()) {
        if (task.size() > 240)
            task = task.left(240) + QStringLiteral("...");
        compact.insert(QStringLiteral("task"), task);
    }

    return compact;
}

inline QString sanitizePersistedToolRawResult(const QString& toolName, const QString& rawResult)
{
    QString out = rawResult.trimmed().isEmpty()
        ? QStringLiteral("[工具执行完成，无输出]")
        : rawResult;

    if (detail::isDelegateStatusLikeTool(toolName) && out.size() > 900)
        out = out.left(900) + QStringLiteral("\n...[status truncated]...");

    return out;
}

inline QString taskStateTextPreview(const QString& input, int maxChars = 220)
{
    QString out = input;
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    out = out.simplified();
    if (maxChars > 0 && out.size() > maxChars)
        out = out.left(maxChars) + QStringLiteral("...");
    return out;
}

inline int estimateHistoryChars(const QJsonArray& history)
{
    int total = 0;
    for (const QJsonValue& value : history)
        total += detail::estimateHistoryMessageChars(value.toObject());
    return total;
}

inline bool isBackgroundHeartbeatClientMessageId(const QString& clientMessageId)
{
    return clientMessageId.trimmed().startsWith(QStringLiteral("heartbeat-bg-"), Qt::CaseInsensitive);
}

inline bool isManualHeartbeatClientMessageId(const QString& clientMessageId)
{
    return clientMessageId.trimmed().startsWith(QStringLiteral("heartbeat-manual-"), Qt::CaseInsensitive);
}

inline bool isHeartbeatClientMessageId(const QString& clientMessageId)
{
    return isBackgroundHeartbeatClientMessageId(clientMessageId)
        || isManualHeartbeatClientMessageId(clientMessageId);
}

} // namespace ChatCoordinatorSupport

#endif // CHATCOORDINATORSUPPORT_H

