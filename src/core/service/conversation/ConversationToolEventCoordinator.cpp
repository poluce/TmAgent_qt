#include "ConversationToolEventCoordinator.h"

#include <QDateTime>

ConversationToolEventCoordinator::ConversationToolEventCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void ConversationToolEventCoordinator::handleToolEvent(const QString& sessionId,
                                                       const TurnTask* activeTurn,
                                                       const ToolExecutionEvent& event)
{
    if (!activeTurn
        || !m_dependencies.agentIdentityIdForSession
        || !m_dependencies.reportPulseProgress
        || !m_dependencies.updateTaskStateForSession
        || !m_dependencies.taskStateTextPreview
        || !m_dependencies.suppressHeartbeat
        || !m_dependencies.unsuppressHeartbeat
        || !m_dependencies.emitPipelineEvent
        || !m_dependencies.takeDelegateStartMs
        || !m_dependencies.putDelegateStartMs
        || !m_dependencies.delegateStatsForSession
        || !m_dependencies.setDelegateStatsForSession) {
        return;
    }

    const QString agentId = m_dependencies.agentIdentityIdForSession(sessionId);
    m_dependencies.reportPulseProgress(agentId, QStringLiteral("tool_event"));

    const QString toolName = event.toolName.trimmed();
    const QString toolId = event.toolId.trimmed();
    const bool isDelegateTool = (toolName == QLatin1String("delegate_task"));
    if (!isDelegateTool)
        return;

    if (event.status == QLatin1String("started")) {
        QJsonObject taskExtra;
        taskExtra.insert(QStringLiteral("reason"), QStringLiteral("delegate_tool_started"));
        taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("delegate.tool_started"));
        taskExtra.insert(QStringLiteral("summary"), m_dependencies.taskStateTextPreview(activeTurn->userContent, 220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("正在委派后台子代理"));
        taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("等待后台任务提交结果"));
        m_dependencies.updateTaskStateForSession(
            sessionId,
            QStringLiteral("running"),
            activeTurn,
            taskExtra);

        if (!agentId.isEmpty())
            m_dependencies.suppressHeartbeat(agentId, QStringLiteral("delegate_running"));
        if (!toolId.isEmpty())
            m_dependencies.putDelegateStartMs(sessionId, toolId, QDateTime::currentMSecsSinceEpoch());

        QJsonObject delegateExtra;
        delegateExtra.insert(QStringLiteral("toolName"), toolName);
        delegateExtra.insert(QStringLiteral("toolId"), toolId);
        delegateExtra.insert(QStringLiteral("event"), QStringLiteral("started"));
        const QString taskPreview = event.data.value(QStringLiteral("task")).toString().trimmed();
        if (!taskPreview.isEmpty())
            delegateExtra.insert(QStringLiteral("task_preview"), taskPreview.left(200));
        const QString rolePrompt = event.data.value(QStringLiteral("role_prompt")).toString().trimmed();
        if (!rolePrompt.isEmpty())
            delegateExtra.insert(QStringLiteral("role_prompt_preview"), rolePrompt.left(120));
        const QString parentAgentId = event.data.value(QStringLiteral("_agent_id")).toString().trimmed();
        if (!parentAgentId.isEmpty())
            delegateExtra.insert(QStringLiteral("parent_agent_id"), parentAgentId);

        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("delegate.tool_started"),
            activeTurn,
            QString(),
            QString(),
            delegateExtra,
            true);
        return;
    }

    if (event.status != QLatin1String("completed"))
        return;

    const QString delegateStatus = event.data.value(QStringLiteral("status")).toString().trimmed().toLower();
    const QString jobId = event.data.value(QStringLiteral("job_id")).toString().trimmed();
    if (event.success && delegateStatus == QLatin1String("accepted") && !jobId.isEmpty()) {
        QJsonObject taskExtra;
        taskExtra.insert(QStringLiteral("reason"), QStringLiteral("delegate_running"));
        taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("delegate.tool_completed"));
        taskExtra.insert(QStringLiteral("summary"),
                         m_dependencies.taskStateTextPreview(
                             event.formattedResult.isEmpty() ? activeTurn->userContent : event.formattedResult,
                             220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("等待后台子代理任务完成"));
        taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("可查询进度、等待完成通知或取消任务"));
        taskExtra.insert(QStringLiteral("waiting_job_id"), jobId);
        taskExtra.insert(QStringLiteral("last_error"), QJsonValue::Null);
        m_dependencies.updateTaskStateForSession(
            sessionId,
            QStringLiteral("blocked"),
            activeTurn,
            taskExtra);
    }

    if (!agentId.isEmpty())
        m_dependencies.unsuppressHeartbeat(agentId);

    qint64 durationMs = -1;
    if (!toolId.isEmpty())
        durationMs = m_dependencies.takeDelegateStartMs(sessionId, toolId);

    DelegateStats stats = m_dependencies.delegateStatsForSession(sessionId);
    ++stats.totalCount;
    if (event.success)
        ++stats.successCount;
    else
        ++stats.failureCount;
    if (durationMs >= 0)
        stats.totalDurationMs += durationMs;
    m_dependencies.setDelegateStatsForSession(sessionId, stats);

    QJsonObject delegateExtra;
    delegateExtra.insert(QStringLiteral("toolName"), toolName);
    delegateExtra.insert(QStringLiteral("toolId"), toolId);
    delegateExtra.insert(QStringLiteral("event"), QStringLiteral("completed"));
    delegateExtra.insert(QStringLiteral("success"), event.success);
    if (durationMs >= 0)
        delegateExtra.insert(QStringLiteral("durationMs"), static_cast<double>(durationMs));
    delegateExtra.insert(QStringLiteral("delegate_total"), stats.totalCount);
    delegateExtra.insert(QStringLiteral("delegate_success"), stats.successCount);
    delegateExtra.insert(QStringLiteral("delegate_failed"), stats.failureCount);
    const int measuredCount = stats.successCount + stats.failureCount;
    if (measuredCount > 0 && stats.totalDurationMs > 0) {
        delegateExtra.insert(
            QStringLiteral("delegate_avg_duration_ms"),
            static_cast<double>(stats.totalDurationMs) / measuredCount);
    }
    if (!event.formattedResult.trimmed().isEmpty())
        delegateExtra.insert(QStringLiteral("summary"), event.formattedResult.trimmed());

    const auto copyDelegateMetric = [&](const QString& key) {
        if (event.data.contains(key))
            delegateExtra.insert(key, event.data.value(key));
    };

    const QString childRequestId = event.data.value(QStringLiteral("child_request_id")).toString().trimmed();
    if (!childRequestId.isEmpty())
        delegateExtra.insert(QStringLiteral("child_request_id"), childRequestId);
    const QString childTraceId = event.data.value(QStringLiteral("child_trace_id")).toString().trimmed();
    if (!childTraceId.isEmpty())
        delegateExtra.insert(QStringLiteral("child_trace_id"), childTraceId);
    const QString childAgentId = event.data.value(QStringLiteral("child_agent_id")).toString().trimmed();
    if (!childAgentId.isEmpty())
        delegateExtra.insert(QStringLiteral("child_agent_id"), childAgentId);
    const QString childModel = event.data.value(QStringLiteral("child_model")).toString().trimmed();
    if (!childModel.isEmpty())
        delegateExtra.insert(QStringLiteral("child_model"), childModel);
    const QString childFinishReason = event.data.value(QStringLiteral("child_finish_reason")).toString().trimmed();
    if (!childFinishReason.isEmpty())
        delegateExtra.insert(QStringLiteral("child_finish_reason"), childFinishReason);
    const QString failureReason = event.data.value(QStringLiteral("failure_reason")).toString().trimmed();
    if (!failureReason.isEmpty())
        delegateExtra.insert(QStringLiteral("failure_reason"), failureReason);

    copyDelegateMetric(QStringLiteral("child_duration_ms"));
    copyDelegateMetric(QStringLiteral("child_timeout_ms"));
    copyDelegateMetric(QStringLiteral("max_response_chars"));
    copyDelegateMetric(QStringLiteral("restrict_delegation"));
    copyDelegateMetric(QStringLiteral("inherited_allowed_tools_count"));
    copyDelegateMetric(QStringLiteral("child_io_entries"));
    copyDelegateMetric(QStringLiteral("child_last_request_messages_count"));
    copyDelegateMetric(QStringLiteral("child_finish_reason"));
    copyDelegateMetric(QStringLiteral("child_response_tool_call_batches"));
    copyDelegateMetric(QStringLiteral("child_response_tool_call_total"));
    copyDelegateMetric(QStringLiteral("child_tool_started_count"));
    copyDelegateMetric(QStringLiteral("child_tool_progress_count"));
    copyDelegateMetric(QStringLiteral("child_tool_completed_count"));
    copyDelegateMetric(QStringLiteral("child_tool_success_count"));
    copyDelegateMetric(QStringLiteral("child_tool_failure_count"));
    copyDelegateMetric(QStringLiteral("child_stream_chunk_count"));
    copyDelegateMetric(QStringLiteral("child_stream_chars"));
    copyDelegateMetric(QStringLiteral("child_timeline_dropped"));
    copyDelegateMetric(QStringLiteral("child_error"));
    copyDelegateMetric(QStringLiteral("child_tools"));
    copyDelegateMetric(QStringLiteral("child_timeline"));

    const QString eventType = event.success
        ? QStringLiteral("delegate.tool_completed")
        : QStringLiteral("delegate.tool_failed");
    m_dependencies.emitPipelineEvent(
        sessionId,
        eventType,
        activeTurn,
        QString(),
        event.success ? QString() : event.rawResult.left(300),
        delegateExtra,
        true);
}
