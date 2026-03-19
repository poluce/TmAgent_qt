#include "TurnCompletionCoordinator.h"

#include "HeartbeatReplyUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

TurnCompletionCoordinator::TurnCompletionCoordinator(const Dependencies& deps)
    : m_deps(deps)
{
}

// ── finalizeTurnInternal (原 ConversationFinalizeCoordinator::finalizeTurn) ──

void TurnCompletionCoordinator::finalizeTurnInternal(const QString& sessionId, TurnTask* outTurn)
{
    if (!m_deps.findPipeline
        || !m_deps.activeTurn
        || !m_deps.flushPendingDeltaLog
        || !m_deps.turnManager
        || !m_deps.clearDelegateStartsForSession
        || !m_deps.clearToolProgressCacheForSession
        || !m_deps.ctx.agentIdentityIdForSession
        || !m_deps.activeSessionForAgent
        || !m_deps.clearActiveSessionForAgent
        || !m_deps.resetSessionStreamState
        || !m_deps.tryStartNextTurn
        || !m_deps.tryStartNextTurnForAgent) {
        return;
    }

    SessionPipeline* pipeline = m_deps.findPipeline(sessionId);
    TurnTask* activeTurn = m_deps.activeTurn(sessionId);
    if (pipeline && activeTurn)
        m_deps.flushPendingDeltaLog(sessionId, pipeline, activeTurn, true);

    m_deps.turnManager->clearActiveTurn(sessionId, outTurn);
    m_deps.clearDelegateStartsForSession(sessionId);
    m_deps.clearToolProgressCacheForSession(sessionId);

    const QString agentId = m_deps.ctx.agentIdentityIdForSession(sessionId);
    if (!agentId.isEmpty() && m_deps.activeSessionForAgent(agentId) == sessionId)
        m_deps.clearActiveSessionForAgent(agentId);
    m_deps.resetSessionStreamState(sessionId);

    m_deps.tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        m_deps.tryStartNextTurnForAgent(agentId);
}

// ── onRuntimeFinished (原 ConversationFinishCoordinator::onRuntimeFinished) ──

TurnCompletionCoordinator::Result TurnCompletionCoordinator::onRuntimeFinished(const QString& sessionId,
                                                                                const QString& fullContent)
{
    Result result;
    if (!m_deps.ctx.agentIdentityIdForSession
        || !m_deps.ctx.isBackgroundClientMessage
        || !m_deps.ctx.isHeartbeatClientMessage
        || !m_deps.isManualHeartbeatClientMessage
        || !m_deps.ctx.reportPulseProgress
        || !m_deps.taskStateForSession
        || !m_deps.ctx.updateTaskState
        || !m_deps.ctx.taskStateTextPreview
        || !m_deps.heartbeatDuplicateWindowMs
        || !m_deps.heartbeatLastDeliveredAt
        || !m_deps.heartbeatLastDeliveredDigest
        || !m_deps.recordHeartbeatSuppressed
        || !m_deps.recordHeartbeatDelivered
        || !m_deps.recordHeartbeatManualSuppress
        || !m_deps.ctx.postMessage
        || !m_deps.saveTaskContextSnapshot
        || !m_deps.saveContextCompressionCheckpoint
        || !m_deps.saveResumePacket
        || !m_deps.loadTaskContextSnapshot
        || !m_deps.emitFinished
        || !m_deps.ctx.emitPipelineEvent) {
        return result;
    }

    finalizeTurnInternal(sessionId, &result.finishedTurn);
    if (!fullContent.isEmpty())
        result.finishedTurn.assistantContent = fullContent;

    result.agentId = m_deps.ctx.agentIdentityIdForSession(sessionId);
    const bool backgroundHeartbeat =
        m_deps.ctx.isBackgroundClientMessage(result.finishedTurn.clientMessageId);
    result.heartbeatTurn =
        m_deps.ctx.isHeartbeatClientMessage(result.finishedTurn.clientMessageId);
    const bool manualHeartbeat =
        m_deps.isManualHeartbeatClientMessage(result.finishedTurn.clientMessageId);
    const bool skipPersistForBackgroundHeartbeat = backgroundHeartbeat;
    result.skipMemoryForHeartbeat = skipPersistForBackgroundHeartbeat;

    m_deps.ctx.reportPulseProgress(result.agentId, QStringLiteral("finished"));

    const QJsonObject existingTaskState = m_deps.taskStateForSession(sessionId);
    const bool blockedBySameTurn =
        existingTaskState.value(QStringLiteral("state")).toString() == QLatin1String("blocked")
        && existingTaskState.value(QStringLiteral("turn_id")).toString().trimmed()
            == result.finishedTurn.turnId.trimmed();
    if (!result.heartbeatTurn && !blockedBySameTurn) {
        QJsonObject taskExtra;
        taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_completed"));
        taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_completed"));
        taskExtra.insert(QStringLiteral("summary"),
                         m_deps.ctx.taskStateTextPreview(
                             fullContent.isEmpty() ? result.finishedTurn.assistantContent : fullContent,
                             220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("本轮执行已完成"));
        taskExtra.insert(QStringLiteral("next_step"), QJsonValue::Null);
        taskExtra.insert(QStringLiteral("last_error"), QJsonValue::Null);
        taskExtra.insert(QStringLiteral("waiting_job_id"), QJsonValue::Null);
        m_deps.ctx.updateTaskState(
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
            m_deps.heartbeatDuplicateWindowMs(result.agentId),
            result.heartbeatTurn ? m_deps.heartbeatLastDeliveredAt(result.agentId) : QDateTime(),
            result.heartbeatTurn ? m_deps.heartbeatLastDeliveredDigest(result.agentId) : QString(),
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
            m_deps.recordHeartbeatSuppressed(
                result.agentId,
                heartbeatReplyDigestValue,
                heartbeatReplySkipReason,
                nowUtc);
        } else if (!normalizedAssistantContent.isEmpty() && !heartbeatReplyDigestValue.isEmpty()) {
            m_deps.recordHeartbeatDelivered(
                result.agentId,
                heartbeatReplyDigestValue,
                normalizedAssistantContent.left(160),
                nowUtc);
        } else if (heartbeatDelivery.suppress && !backgroundHeartbeat && !heartbeatReplyDigestValue.isEmpty()) {
            m_deps.recordHeartbeatManualSuppress(
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
        m_deps.ctx.postMessage(sessionId, assistantMsg);
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
        m_deps.ctx.postMessage(sessionId, assistantMsg);
    }

    const QString deliveredContent =
        shouldSurfaceBackgroundHeartbeat
            ? result.finishedTurn.assistantContent
            : (backgroundHeartbeat ? QString() : result.finishedTurn.assistantContent);

    QJsonObject extra;
    extra.insert(QStringLiteral("fullContent"), deliveredContent);
    if (!skipPersistForBackgroundHeartbeat || shouldSurfaceBackgroundHeartbeat || manualHeartbeat)
        m_deps.emitFinished(sessionId, deliveredContent);
    m_deps.ctx.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_completed"),
        &result.finishedTurn,
        QString(),
        QString(),
        extra,
        true);

    bool snapshotLoadOk = false;
    QJsonObject snapshotObj = m_deps.loadTaskContextSnapshot(sessionId, &snapshotLoadOk).toJson();
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
    const bool snapshotSaved = m_deps.saveTaskContextSnapshot(sessionId, snapshot);
    if (snapshotSaved) {
        QJsonObject snapshotExtra;
        snapshotExtra.insert(QStringLiteral("snapshot_id"), snapshotObj.value(QStringLiteral("snapshot_id")).toString());
        snapshotExtra.insert(QStringLiteral("phase"), snapshotObj.value(QStringLiteral("current_phase")).toString());
        m_deps.ctx.emitPipelineEvent(sessionId,
                                     QStringLiteral("context.snapshot.updated"),
                                     &result.finishedTurn,
                                     QString(),
                                     QString(),
                                     snapshotExtra,
                                     true);
    } else {
        QJsonObject errorExtra;
        errorExtra.insert(QStringLiteral("reason"), QStringLiteral("snapshot_save_failed"));
        m_deps.ctx.emitPipelineEvent(sessionId,
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
        if (m_deps.saveContextCompressionCheckpoint(sessionId, checkpoint)) {
            QJsonObject compactExtra;
            compactExtra.insert(QStringLiteral("checkpoint_id"), checkpointObj.value(QStringLiteral("checkpoint_id")).toString());
            compactExtra.insert(QStringLiteral("reason"), checkpointObj.value(QStringLiteral("reason")).toString());
            compactExtra.insert(QStringLiteral("dropped_message_count"), checkpointObj.value(QStringLiteral("dropped_message_count")).toInt());
            m_deps.ctx.emitPipelineEvent(sessionId,
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
        m_deps.ctx.emitPipelineEvent(sessionId,
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
    if (m_deps.saveResumePacket(sessionId, packet)) {
        QJsonObject resumeExtra;
        resumeExtra.insert(QStringLiteral("phase"), resumeObj.value(QStringLiteral("current_phase")).toString());
        m_deps.ctx.emitPipelineEvent(sessionId,
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
        m_deps.ctx.emitPipelineEvent(
            sessionId,
            QStringLiteral("heartbeat.skipped"),
            &result.finishedTurn,
            QString(),
            heartbeatReplySkipReason,
            hbExtra,
            true);
    }

    handleFinishMemory(sessionId, result.agentId, result.finishedTurn,
                       result.skipMemoryForHeartbeat, result.heartbeatTurn);

    result.valid = true;
    return result;
}

// ── onRuntimeError (原 ConversationErrorCoordinator::onRuntimeError) ──

void TurnCompletionCoordinator::onRuntimeError(const QString& sessionId, const QString& errorMsg)
{
    if (!m_deps.ctx.isBackgroundClientMessage
        || !m_deps.ctx.isHeartbeatClientMessage
        || !m_deps.ctx.agentIdentityIdForSession
        || !m_deps.ctx.reportPulseProgress
        || !m_deps.isTransientUpstreamError
        || !m_deps.listActiveJobs
        || !m_deps.buildDelegateRecoveryReply
        || !m_deps.ctx.postMessage
        || !m_deps.ctx.updateTaskState
        || !m_deps.ctx.taskStateTextPreview
        || !m_deps.emitFinished
        || !m_deps.emitError
        || !m_deps.ctx.emitPipelineEvent) {
        return;
    }

    TurnTask failedTurn;
    finalizeTurnInternal(sessionId, &failedTurn);

    const bool skipNotifyForBackgroundHeartbeat =
        m_deps.ctx.isBackgroundClientMessage(failedTurn.clientMessageId);
    const bool heartbeatTurn =
        m_deps.ctx.isHeartbeatClientMessage(failedTurn.clientMessageId);

    const QString agentId = m_deps.ctx.agentIdentityIdForSession(sessionId);
    m_deps.ctx.reportPulseProgress(agentId, QStringLiteral("error"));

    if (m_deps.isTransientUpstreamError(errorMsg) && !agentId.isEmpty()) {
        const QList<ActiveJobInfo> activeJobs = m_deps.listActiveJobs(agentId, 5);
        if (!activeJobs.isEmpty()) {
            const QString fallbackReply = m_deps.buildDelegateRecoveryReply(activeJobs);

            Message assistantMsg = Message::createText(sessionId, agentId, fallbackReply);
            assistantMsg.traceId = failedTurn.requestTraceId;
            assistantMsg.turnId = failedTurn.turnId;
            assistantMsg.status = Message::Status::Completed;
            m_deps.ctx.postMessage(sessionId, assistantMsg);

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
                taskExtra.insert(QStringLiteral("summary"), m_deps.ctx.taskStateTextPreview(fallbackReply, 220));
                taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("等待后台子代理任务完成"));
                taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("可继续跟进后台任务进度或等待完成通知"));
                taskExtra.insert(QStringLiteral("last_error"), m_deps.ctx.taskStateTextPreview(errorMsg, 160));
                m_deps.ctx.updateTaskState(
                    sessionId,
                    QStringLiteral("blocked"),
                    &failedTurn,
                    taskExtra);
            }

            m_deps.emitFinished(sessionId, fallbackReply);
            m_deps.ctx.emitPipelineEvent(
                sessionId,
                QStringLiteral("turn_recovered"),
                &failedTurn,
                QString(),
                errorMsg,
                extra,
                true);
            m_deps.ctx.emitPipelineEvent(
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
        taskExtra.insert(QStringLiteral("summary"), m_deps.ctx.taskStateTextPreview(failedTurn.userContent, 220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("执行失败"));
        taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("等待重试或调整任务方向"));
        taskExtra.insert(QStringLiteral("last_error"), m_deps.ctx.taskStateTextPreview(errorMsg, 160));
        m_deps.ctx.updateTaskState(
            sessionId,
            QStringLiteral("failed"),
            &failedTurn,
            taskExtra);
    }

    if (!skipNotifyForBackgroundHeartbeat)
        m_deps.emitError(sessionId, errorMsg);
    m_deps.ctx.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_failed"),
        &failedTurn,
        QString(),
        errorMsg,
        QJsonObject(),
        true);
}

// ── handleFinishMemory (原 ConversationMemoryFinishCoordinator::handleFinishMemory) ──

void TurnCompletionCoordinator::handleFinishMemory(const QString& sessionId,
                                                    const QString& agentId,
                                                    const TurnTask& finishedTurn,
                                                    bool skipMemoryForHeartbeat,
                                                    bool heartbeatTurn)
{
    if (!m_deps.ctx.emitPipelineEvent
        || !m_deps.maybeReflectMemoryAndEmit
        || !m_deps.refreshMemoryIndexAndEmit) {
        return;
    }

    if (skipMemoryForHeartbeat) {
        QJsonObject memoryExtra;
        memoryExtra.insert(QStringLiteral("reason"), QStringLiteral("heartbeat_turn"));
        memoryExtra.insert(QStringLiteral("reflection_triggered"),
                           m_deps.reflectionEnabled ? m_deps.reflectionEnabled() : false);
        m_deps.ctx.emitPipelineEvent(
            sessionId,
            QStringLiteral("memory.skipped"),
            &finishedTurn,
            QString(),
            QString(),
            memoryExtra,
            true);
        if (!agentId.isEmpty()) {
            m_deps.maybeReflectMemoryAndEmit(
                sessionId,
                agentId,
                finishedTurn,
                true,
                QStringLiteral("heartbeat_turn"));
        }
        return;
    }

    if (!m_deps.retainTurn || agentId.isEmpty())
        return;

    QString memorySummary;
    QString memoryPath;
    QJsonObject memoryMetadata;
    QString memoryError;
    const bool retained = m_deps.retainTurn(
        agentId,
        sessionId,
        finishedTurn,
        &memorySummary,
        &memoryPath,
        &memoryMetadata,
        &memoryError);

    if (retained) {
        if (!memorySummary.trimmed().isEmpty()) {
            QJsonObject memoryExtra;
            memoryExtra.insert(QStringLiteral("doc_type"), QStringLiteral("daily"));
            memoryExtra.insert(QStringLiteral("summary"), memorySummary);
            memoryExtra.insert(QStringLiteral("path"), memoryPath);
            for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
                memoryExtra.insert(it.key(), it.value());
            m_deps.ctx.emitPipelineEvent(
                sessionId,
                QStringLiteral("memory.updated"),
                &finishedTurn,
                QString(),
                QString(),
                memoryExtra,
                true);
        }

        const int compactedCount = memoryMetadata.value(QStringLiteral("compacted_count")).toInt();
        if (compactedCount > 0) {
            QJsonObject compactExtra;
            compactExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
            compactExtra.insert(QStringLiteral("summary"), memorySummary);
            compactExtra.insert(QStringLiteral("compacted_count"), compactedCount);
            compactExtra.insert(QStringLiteral("path"), memoryMetadata.value(QStringLiteral("longMemoryPath")).toString());
            compactExtra.insert(QStringLiteral("longMemoryAdded"), memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt());
            compactExtra.insert(QStringLiteral("longMemoryDuplicate"), memoryMetadata.value(QStringLiteral("longMemoryDuplicate")).toInt());
            compactExtra.insert(QStringLiteral("manualRemember"), memoryMetadata.value(QStringLiteral("manualRemember")).toBool());
            for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
                compactExtra.insert(it.key(), it.value());
            m_deps.ctx.emitPipelineEvent(
                sessionId,
                QStringLiteral("memory.compacted"),
                &finishedTurn,
                QString(),
                QString(),
                compactExtra,
                true);
        }

        m_deps.refreshMemoryIndexAndEmit(
            sessionId,
            agentId,
            &finishedTurn,
            QStringLiteral("retain_turn"),
            memoryPath,
            memoryMetadata);

        m_deps.maybeReflectMemoryAndEmit(
            sessionId,
            agentId,
            finishedTurn,
            heartbeatTurn,
            heartbeatTurn ? QStringLiteral("heartbeat_turn") : QString());
        return;
    }

    QJsonObject memoryExtra;
    memoryExtra.insert(QStringLiteral("doc_type"), QStringLiteral("daily"));
    memoryExtra.insert(QStringLiteral("path"), memoryPath);
    m_deps.ctx.emitPipelineEvent(
        sessionId,
        QStringLiteral("memory.error"),
        &finishedTurn,
        QString(),
        memoryError.isEmpty() ? QStringLiteral("memory retain failed") : memoryError,
        memoryExtra,
        true);
}