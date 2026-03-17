#include "ConversationFinishCoordinator.h"

#include "HeartbeatReplyUtils.h"

ConversationFinishCoordinator::ConversationFinishCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

ConversationFinishCoordinator::Result ConversationFinishCoordinator::onRuntimeFinished(const QString& sessionId,
                                                                                       const QString& fullContent)
{
    Result result;
    if (!m_dependencies.finalizeTurn
        || !m_dependencies.agentIdentityIdForSession
        || !m_dependencies.isBackgroundClientMessage
        || !m_dependencies.isHeartbeatClientMessage
        || !m_dependencies.isManualHeartbeatClientMessage
        || !m_dependencies.reportPulseProgress
        || !m_dependencies.taskStateForSession
        || !m_dependencies.updateTaskStateForSession
        || !m_dependencies.taskStateTextPreview
        || !m_dependencies.heartbeatDuplicateWindowMs
        || !m_dependencies.heartbeatLastDeliveredAt
        || !m_dependencies.heartbeatLastDeliveredDigest
        || !m_dependencies.recordHeartbeatSuppressed
        || !m_dependencies.recordHeartbeatDelivered
        || !m_dependencies.recordHeartbeatManualSuppress
        || !m_dependencies.postMessage
        || !m_dependencies.emitFinished
        || !m_dependencies.emitPipelineEvent) {
        return result;
    }

    m_dependencies.finalizeTurn(sessionId, &result.finishedTurn);
    if (!fullContent.isEmpty())
        result.finishedTurn.assistantContent = fullContent;

    result.agentId = m_dependencies.agentIdentityIdForSession(sessionId);
    const bool backgroundHeartbeat =
        m_dependencies.isBackgroundClientMessage(result.finishedTurn.clientMessageId);
    result.heartbeatTurn =
        m_dependencies.isHeartbeatClientMessage(result.finishedTurn.clientMessageId);
    const bool manualHeartbeat =
        m_dependencies.isManualHeartbeatClientMessage(result.finishedTurn.clientMessageId);
    const bool skipPersistForBackgroundHeartbeat = backgroundHeartbeat;
    result.skipMemoryForHeartbeat = skipPersistForBackgroundHeartbeat;

    m_dependencies.reportPulseProgress(result.agentId, QStringLiteral("finished"));

    const QJsonObject existingTaskState = m_dependencies.taskStateForSession(sessionId);
    const bool blockedBySameTurn =
        existingTaskState.value(QStringLiteral("state")).toString() == QLatin1String("blocked")
        && existingTaskState.value(QStringLiteral("turn_id")).toString().trimmed()
            == result.finishedTurn.turnId.trimmed();
    if (!result.heartbeatTurn && !blockedBySameTurn) {
        QJsonObject taskExtra;
        taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_completed"));
        taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_completed"));
        taskExtra.insert(QStringLiteral("summary"),
                         m_dependencies.taskStateTextPreview(
                             fullContent.isEmpty() ? result.finishedTurn.assistantContent : fullContent,
                             220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("本轮执行已完成"));
        taskExtra.insert(QStringLiteral("next_step"), QJsonValue::Null);
        taskExtra.insert(QStringLiteral("last_error"), QJsonValue::Null);
        taskExtra.insert(QStringLiteral("waiting_job_id"), QJsonValue::Null);
        m_dependencies.updateTaskStateForSession(
            sessionId,
            QStringLiteral("done"),
            &result.finishedTurn,
            taskExtra);
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const HeartbeatReplyUtils::DeliveryDecision heartbeatDelivery =
        HeartbeatReplyUtils::evaluateReplyDelivery(
            result.finishedTurn.assistantContent,
            backgroundHeartbeat,
            m_dependencies.heartbeatDuplicateWindowMs(result.agentId),
            result.heartbeatTurn ? m_dependencies.heartbeatLastDeliveredAt(result.agentId) : QDateTime(),
            result.heartbeatTurn ? m_dependencies.heartbeatLastDeliveredDigest(result.agentId) : QString(),
            nowUtc);

    const QString normalizedAssistantContent = heartbeatDelivery.normalizedText;
    const bool heartbeatNoChangeReply =
        result.heartbeatTurn && heartbeatDelivery.reason == QLatin1String("no_change_reply");
    bool suppressHeartbeatReply = false;
    QString heartbeatReplySkipReason;
    const QString heartbeatReplyDigestValue = heartbeatDelivery.digest;

    if (result.heartbeatTurn && !result.agentId.isEmpty()) {
        if (heartbeatDelivery.suppress && backgroundHeartbeat) {
            suppressHeartbeatReply = true;
            heartbeatReplySkipReason = heartbeatDelivery.reason;
            m_dependencies.recordHeartbeatSuppressed(
                result.agentId,
                heartbeatReplyDigestValue,
                heartbeatReplySkipReason,
                nowUtc);
        } else if (!normalizedAssistantContent.isEmpty() && !heartbeatReplyDigestValue.isEmpty()) {
            m_dependencies.recordHeartbeatDelivered(
                result.agentId,
                heartbeatReplyDigestValue,
                normalizedAssistantContent.left(160),
                nowUtc);
        } else if (heartbeatDelivery.suppress && !backgroundHeartbeat && !heartbeatReplyDigestValue.isEmpty()) {
            m_dependencies.recordHeartbeatManualSuppress(
                result.agentId,
                heartbeatReplyDigestValue,
                heartbeatDelivery.reason,
                nowUtc);
        }
    }

    if (!skipPersistForBackgroundHeartbeat
        && !result.finishedTurn.assistantContent.trimmed().isEmpty()
        && !result.agentId.isEmpty()) {
        Message assistantMsg = Message::createText(sessionId, result.agentId, result.finishedTurn.assistantContent);
        assistantMsg.traceId = result.finishedTurn.requestTraceId;
        assistantMsg.turnId = result.finishedTurn.turnId;
        assistantMsg.status = Message::Status::Completed;
        m_dependencies.postMessage(sessionId, assistantMsg);
    }

    const bool shouldSurfaceBackgroundHeartbeat =
        backgroundHeartbeat
        && !suppressHeartbeatReply
        && !heartbeatNoChangeReply
        && !result.finishedTurn.assistantContent.trimmed().isEmpty()
        && !result.agentId.isEmpty();
    if (shouldSurfaceBackgroundHeartbeat) {
        Message assistantMsg = Message::createText(sessionId, result.agentId, result.finishedTurn.assistantContent);
        assistantMsg.traceId = result.finishedTurn.requestTraceId;
        assistantMsg.turnId = result.finishedTurn.turnId;
        assistantMsg.status = Message::Status::Completed;
        m_dependencies.postMessage(sessionId, assistantMsg);
    }

    const QString deliveredContent =
        shouldSurfaceBackgroundHeartbeat
            ? result.finishedTurn.assistantContent
            : (backgroundHeartbeat ? QString() : result.finishedTurn.assistantContent);

    QJsonObject extra;
    extra.insert(QStringLiteral("fullContent"), deliveredContent);
    if (!skipPersistForBackgroundHeartbeat || shouldSurfaceBackgroundHeartbeat || manualHeartbeat)
        m_dependencies.emitFinished(sessionId, deliveredContent);
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_completed"),
        &result.finishedTurn,
        QString(),
        QString(),
        extra,
        true);

    if (backgroundHeartbeat && suppressHeartbeatReply) {
        QJsonObject hbExtra;
        hbExtra.insert(QStringLiteral("agent_id"), result.agentId);
        hbExtra.insert(QStringLiteral("session_id"), sessionId);
        hbExtra.insert(QStringLiteral("reason"), heartbeatReplySkipReason);
        if (!heartbeatReplyDigestValue.isEmpty())
            hbExtra.insert(QStringLiteral("reply_digest"), heartbeatReplyDigestValue);
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("heartbeat.skipped"),
            &result.finishedTurn,
            QString(),
            heartbeatReplySkipReason,
            hbExtra,
            true);
    }

    result.valid = true;
    return result;
}

