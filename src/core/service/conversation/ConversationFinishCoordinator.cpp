#include "ConversationFinishCoordinator.h"

#include "HeartbeatReplyUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

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
        || !m_dependencies.saveTaskContextSnapshot
        || !m_dependencies.saveContextCompressionCheckpoint
        || !m_dependencies.saveResumePacket
        || !m_dependencies.loadTaskContextSnapshot
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

    bool snapshotLoadOk = false;
    QJsonObject snapshotObj = m_dependencies.loadTaskContextSnapshot(sessionId, &snapshotLoadOk).toJson();
    snapshotObj.insert(QStringLiteral("schema_version"), ConversationContext::kSchemaVersion);
    snapshotObj.insert(QStringLiteral("kind"), QStringLiteral("task_snapshot"));
    snapshotObj.insert(QStringLiteral("session_id"), sessionId);
    snapshotObj.insert(QStringLiteral("snapshot_id"), QStringLiteral("snap_") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    snapshotObj.insert(QStringLiteral("updated_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
    snapshotObj.insert(QStringLiteral("current_phase"), QStringLiteral("completed"));
    snapshotObj.insert(QStringLiteral("goal"), existingTaskState.value(QStringLiteral("summary")).toString().trimmed().isEmpty()
                                                    ? result.finishedTurn.userContent
                                                    : existingTaskState.value(QStringLiteral("summary")).toString().trimmed());
    snapshotObj.insert(QStringLiteral("next_action"), QStringLiteral("等待下一轮用户输入或继续当前任务"));
    snapshotObj.insert(QStringLiteral("recent_decisions"), QJsonArray{ result.finishedTurn.assistantContent.left(220) });
    snapshotObj.insert(QStringLiteral("source_turn_ids"), QJsonArray{ result.finishedTurn.turnId });

    ConversationContext::TaskContextSnapshot snapshot;
    snapshot.payload = snapshotObj;
    const bool snapshotSaved = m_dependencies.saveTaskContextSnapshot(sessionId, snapshot);
    if (snapshotSaved) {
        QJsonObject snapshotExtra;
        snapshotExtra.insert(QStringLiteral("snapshot_id"), snapshotObj.value(QStringLiteral("snapshot_id")).toString());
        snapshotExtra.insert(QStringLiteral("phase"), snapshotObj.value(QStringLiteral("current_phase")).toString());
        m_dependencies.emitPipelineEvent(sessionId,
                                         QStringLiteral("context.snapshot.updated"),
                                         &result.finishedTurn,
                                         QString(),
                                         QString(),
                                         snapshotExtra,
                                         true);
    } else {
        QJsonObject errorExtra;
        errorExtra.insert(QStringLiteral("reason"), QStringLiteral("snapshot_save_failed"));
        m_dependencies.emitPipelineEvent(sessionId,
                                         QStringLiteral("context.compact.error"),
                                         &result.finishedTurn,
                                         QString(),
                                         QStringLiteral("snapshot_save_failed"),
                                         errorExtra,
                                         true);
    }

    const int contentLength = result.finishedTurn.assistantContent.size() + result.finishedTurn.userContent.size();
    if (contentLength > 1200) {
        QJsonObject checkpointObj;
        checkpointObj.insert(QStringLiteral("schema_version"), ConversationContext::kSchemaVersion);
        checkpointObj.insert(QStringLiteral("kind"), QStringLiteral("context_checkpoint"));
        checkpointObj.insert(QStringLiteral("session_id"), sessionId);
        checkpointObj.insert(QStringLiteral("checkpoint_id"), QStringLiteral("ckpt_") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        checkpointObj.insert(QStringLiteral("updated_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
        checkpointObj.insert(QStringLiteral("reason"), QStringLiteral("content_length_exceeded"));
        checkpointObj.insert(QStringLiteral("dropped_message_count"), 1);
        checkpointObj.insert(QStringLiteral("summary_text"), result.finishedTurn.assistantContent.left(220));
        ConversationContext::ContextCompressionCheckpoint checkpoint;
        checkpoint.payload = checkpointObj;
        if (m_dependencies.saveContextCompressionCheckpoint(sessionId, checkpoint)) {
            QJsonObject compactExtra;
            compactExtra.insert(QStringLiteral("checkpoint_id"), checkpointObj.value(QStringLiteral("checkpoint_id")).toString());
            compactExtra.insert(QStringLiteral("reason"), checkpointObj.value(QStringLiteral("reason")).toString());
            compactExtra.insert(QStringLiteral("dropped_message_count"), checkpointObj.value(QStringLiteral("dropped_message_count")).toInt());
            m_dependencies.emitPipelineEvent(sessionId,
                                             QStringLiteral("context.compacted"),
                                             &result.finishedTurn,
                                             QString(),
                                             QString(),
                                             compactExtra,
                                             true);
        }
    } else {
        QJsonObject skippedExtra;
        skippedExtra.insert(QStringLiteral("reason"), QStringLiteral("content_length_within_budget"));
        skippedExtra.insert(QStringLiteral("content_length"), contentLength);
        m_dependencies.emitPipelineEvent(sessionId,
                                         QStringLiteral("context.compact.skipped"),
                                         &result.finishedTurn,
                                         QString(),
                                         QString(),
                                         skippedExtra,
                                         true);
    }

    QJsonObject resumeObj;
    resumeObj.insert(QStringLiteral("schema_version"), ConversationContext::kSchemaVersion);
    resumeObj.insert(QStringLiteral("kind"), QStringLiteral("resume_packet"));
    resumeObj.insert(QStringLiteral("session_id"), sessionId);
    resumeObj.insert(QStringLiteral("updated_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
    resumeObj.insert(QStringLiteral("goal"), snapshotObj.value(QStringLiteral("goal")).toString());
    resumeObj.insert(QStringLiteral("current_phase"), snapshotObj.value(QStringLiteral("current_phase")).toString());
    resumeObj.insert(QStringLiteral("pending_items"), QJsonArray{ QStringLiteral("等待后续恢复或继续") });
    resumeObj.insert(QStringLiteral("resume_instruction"), QStringLiteral("基于当前 snapshot 与最近消息继续执行。"));
    ConversationContext::ResumePacket packet;
    packet.payload = resumeObj;
    if (m_dependencies.saveResumePacket(sessionId, packet)) {
        QJsonObject resumeExtra;
        resumeExtra.insert(QStringLiteral("phase"), resumeObj.value(QStringLiteral("current_phase")).toString());
        m_dependencies.emitPipelineEvent(sessionId,
                                         QStringLiteral("context.resume_packet.updated"),
                                         &result.finishedTurn,
                                         QString(),
                                         QString(),
                                         resumeExtra,
                                         true);
    }

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

