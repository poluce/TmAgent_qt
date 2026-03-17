#include "ConversationErrorCoordinator.h"

#include <QJsonArray>

ConversationErrorCoordinator::ConversationErrorCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void ConversationErrorCoordinator::onRuntimeError(const QString& sessionId, const QString& errorMsg)
{
    if (!m_dependencies.finalizeTurn
        || !m_dependencies.isBackgroundClientMessage
        || !m_dependencies.isHeartbeatClientMessage
        || !m_dependencies.agentIdentityIdForSession
        || !m_dependencies.reportPulseProgress
        || !m_dependencies.isTransientUpstreamError
        || !m_dependencies.listActiveJobs
        || !m_dependencies.buildDelegateRecoveryReply
        || !m_dependencies.postMessage
        || !m_dependencies.updateTaskStateForSession
        || !m_dependencies.taskStateTextPreview
        || !m_dependencies.emitFinished
        || !m_dependencies.emitError
        || !m_dependencies.emitPipelineEvent) {
        return;
    }

    TurnTask failedTurn;
    m_dependencies.finalizeTurn(sessionId, &failedTurn);

    const bool skipNotifyForBackgroundHeartbeat =
        m_dependencies.isBackgroundClientMessage(failedTurn.clientMessageId);
    const bool heartbeatTurn =
        m_dependencies.isHeartbeatClientMessage(failedTurn.clientMessageId);

    const QString agentId = m_dependencies.agentIdentityIdForSession(sessionId);
    m_dependencies.reportPulseProgress(agentId, QStringLiteral("error"));

    if (m_dependencies.isTransientUpstreamError(errorMsg) && !agentId.isEmpty()) {
        const QList<ActiveJobInfo> activeJobs = m_dependencies.listActiveJobs(agentId, 5);
        if (!activeJobs.isEmpty()) {
            const QString fallbackReply = m_dependencies.buildDelegateRecoveryReply(activeJobs);

            Message assistantMsg = Message::createText(sessionId, agentId, fallbackReply);
            assistantMsg.traceId = failedTurn.requestTraceId;
            assistantMsg.turnId = failedTurn.turnId;
            assistantMsg.status = Message::Status::Completed;
            m_dependencies.postMessage(sessionId, assistantMsg);

            QJsonObject extra;
            extra.insert(QStringLiteral("recovered"), true);
            extra.insert(QStringLiteral("reason"), QStringLiteral("transient_upstream_error_with_active_delegate_jobs"));
            extra.insert(QStringLiteral("active_delegate_jobs"), activeJobs.size());
            QJsonArray jobIds;
            for (const ActiveJobInfo& job : activeJobs) {
                const QString jobId = job.jobId.trimmed();
                if (!jobId.isEmpty())
                    jobIds.append(jobId);
            }
            extra.insert(QStringLiteral("job_ids"), jobIds);
            extra.insert(QStringLiteral("error"), errorMsg);

            if (!heartbeatTurn) {
                QJsonObject taskExtra;
                taskExtra.insert(QStringLiteral("reason"), QStringLiteral("transient_upstream_error_with_active_delegate_jobs"));
                taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_recovered"));
                taskExtra.insert(QStringLiteral("summary"), m_dependencies.taskStateTextPreview(fallbackReply, 220));
                taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("等待后台子代理任务完成"));
                taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("可继续跟进后台任务进度或等待完成通知"));
                taskExtra.insert(QStringLiteral("last_error"), m_dependencies.taskStateTextPreview(errorMsg, 160));
                m_dependencies.updateTaskStateForSession(
                    sessionId,
                    QStringLiteral("blocked"),
                    &failedTurn,
                    taskExtra);
            }

            m_dependencies.emitFinished(sessionId, fallbackReply);
            m_dependencies.emitPipelineEvent(
                sessionId,
                QStringLiteral("turn_recovered"),
                &failedTurn,
                QString(),
                errorMsg,
                extra,
                true);
            m_dependencies.emitPipelineEvent(
                sessionId,
                QStringLiteral("turn_failed"),
                &failedTurn,
                QString(),
                errorMsg,
                extra,
                true);
            return;
        }
    }

    if (!heartbeatTurn) {
        QJsonObject taskExtra;
        taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_failed"));
        taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_failed"));
        taskExtra.insert(QStringLiteral("summary"), m_dependencies.taskStateTextPreview(failedTurn.userContent, 220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("执行失败"));
        taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("等待重试或调整任务方向"));
        taskExtra.insert(QStringLiteral("last_error"), m_dependencies.taskStateTextPreview(errorMsg, 160));
        m_dependencies.updateTaskStateForSession(
            sessionId,
            QStringLiteral("failed"),
            &failedTurn,
            taskExtra);
    }

    if (!skipNotifyForBackgroundHeartbeat)
        m_dependencies.emitError(sessionId, errorMsg);
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_failed"),
        &failedTurn,
        QString(),
        errorMsg,
        QJsonObject(),
        true);
}
