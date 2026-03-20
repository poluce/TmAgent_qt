#include "ConversationService.h"

#include "ApplicationServices.h"
#include "ConversationContextService.h"
#include "MemoryService.h"
#include "AgentRuntime.h"
#include "ChatCoordinatorSupport.h"
#include "HeartbeatReplyUtils.h"
#include "HeartbeatStateStore.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/memory/MemoryManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Message.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

struct ConversationCompletionAccess {
    static HeartbeatStateStore makeHeartbeatStateStore(ApplicationServices& app);
    static HeartbeatRuntimeState& ensureHeartbeatRuntimeState(ConversationService& service,
                                                              const QString& agentId,
                                                              const HeartbeatStateStore& stateStore);
    static void persistCompletionContext(ConversationService& service,
                                         const QString& sessionId,
                                         const TurnTask& finishedTurn,
                                         const QJsonObject& existingTaskState,
                                         const QDateTime& nowUtc);
    static void handleFinishMemory(ConversationService& service,
                                   const QString& sessionId,
                                   const QString& agentId,
                                   const TurnTask& finishedTurn,
                                   bool skipMemoryForHeartbeat,
                                   bool heartbeatTurn);
    static void finalizeTurnInternal(ConversationService& service,
                                     const QString& sessionId,
                                     TurnTask* outTurn);
};

namespace {
using ChatCoordinatorSupport::buildDelegateRecoveryReply;
using ChatCoordinatorSupport::isBackgroundHeartbeatClientMessageId;
using ChatCoordinatorSupport::isHeartbeatClientMessageId;
using ChatCoordinatorSupport::isManualHeartbeatClientMessageId;
using ChatCoordinatorSupport::isTransientUpstreamError;
using ChatCoordinatorSupport::taskStateTextPreview;

struct CompletionResult {
    TurnTask finishedTurn;
    QString agentId;
    bool skipMemoryForHeartbeat = false;
    bool heartbeatTurn = false;
};
} // namespace

void ConversationService::abortCurrent(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !m_turnManager.hasActiveTurn(sessionId)) {
        resetSessionStreamState(sessionId);
        return;
    }

    if (TurnTask* active = m_turnManager.activeTurn(sessionId))
        flushPendingDeltaLog(sessionId, pipeline, active, true);

    if (TurnTask* active = m_turnManager.activeTurn(sessionId)) {
        updateTaskStateForSession(
            sessionId,
            QStringLiteral("canceled"),
            active,
            QJsonObject { { QStringLiteral("reason"), QStringLiteral("user_stop") },
                          { QStringLiteral("source_event"), QStringLiteral("turn_cancelled") },
                          { QStringLiteral("summary"), taskStateTextPreview(active->userContent, 220) },
                          { QStringLiteral("current_step"), QStringLiteral("已取消当前执行") },
                          { QStringLiteral("next_step"), QJsonValue::Null } });
    }

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime)
        runtime->abort();

    TurnTask cancelled;
    if (!m_turnManager.clearActiveTurn(sessionId, &cancelled))
        return;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);
    QJsonObject extra;
    extra.insert(QStringLiteral("reason"), QStringLiteral("user_stop"));
    emitPipelineEvent(QStringLiteral("turn_cancelled"),
                      sessionId,
                      &cancelled,
                      QString(),
                      QString(),
                      extra);
    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
}

QString ConversationService::abortAndRollback(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !m_turnManager.hasActiveTurn(sessionId)) {
        resetSessionStreamState(sessionId);
        return QString();
    }

    if (TurnTask* active = m_turnManager.activeTurn(sessionId))
        flushPendingDeltaLog(sessionId, pipeline, active, true);

    if (TurnTask* active = m_turnManager.activeTurn(sessionId)) {
        updateTaskStateForSession(
            sessionId,
            QStringLiteral("canceled"),
            active,
            QJsonObject { { QStringLiteral("reason"), QStringLiteral("user_stop") },
                          { QStringLiteral("source_event"), QStringLiteral("turn_cancelled") },
                          { QStringLiteral("summary"), taskStateTextPreview(active->userContent, 220) },
                          { QStringLiteral("current_step"), QStringLiteral("已取消当前执行") },
                          { QStringLiteral("next_step"), QJsonValue::Null } });
    }

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    QString rolledBack;
    if (runtime)
        rolledBack = runtime->abortAndRollback();

    TurnTask cancelled;
    if (!m_turnManager.clearActiveTurn(sessionId, &cancelled))
        return rolledBack;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);

    if (rolledBack.isEmpty())
        rolledBack = cancelled.userContent;

    QJsonObject extra;
    extra.insert(QStringLiteral("reason"), QStringLiteral("user_stop"));
    extra.insert(QStringLiteral("rolledBackUserMessage"), rolledBack);
    emitPipelineEvent(QStringLiteral("turn_cancelled"),
                      sessionId,
                      &cancelled,
                      QString(),
                      QString(),
                      extra);
    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
    return rolledBack;
}

void ConversationService::finalizeTurn(const QString& sessionId, TurnTask* outTurn)
{
    ConversationCompletionAccess::finalizeTurnInternal(*this, sessionId, outTurn);
}

void ConversationService::onRuntimeFinished(const QString& sessionId, const QString& fullContent)
{
    if (!m_app.m_memoryService)
        return;

    CompletionResult result;
    ConversationCompletionAccess::finalizeTurnInternal(*this, sessionId, &result.finishedTurn);
    if (!fullContent.isEmpty())
        result.finishedTurn.assistantContent = fullContent;

    result.agentId = agentIdentityIdForSession(sessionId);
    const bool backgroundHeartbeat =
        isBackgroundHeartbeatClientMessageId(result.finishedTurn.clientMessageId);
    result.heartbeatTurn = isHeartbeatClientMessageId(result.finishedTurn.clientMessageId);
    const bool manualHeartbeat = isManualHeartbeatClientMessageId(result.finishedTurn.clientMessageId);
    const bool skipPersistForBackgroundHeartbeat = backgroundHeartbeat;
    result.skipMemoryForHeartbeat = skipPersistForBackgroundHeartbeat;

    m_app.m_memoryService->reportPulseProgress(result.agentId, QStringLiteral("finished"));

    const QJsonObject existingTaskState = taskStateForSession(sessionId);
    const bool blockedBySameTurn =
        existingTaskState.value(QStringLiteral("state")).toString() == QLatin1String("blocked")
        && existingTaskState.value(QStringLiteral("turn_id")).toString().trimmed()
            == result.finishedTurn.turnId.trimmed();
    if (!result.heartbeatTurn && !blockedBySameTurn) {
        QJsonObject taskExtra;
        taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_completed"));
        taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_completed"));
        taskExtra.insert(QStringLiteral("summary"),
                         taskStateTextPreview(
                             fullContent.isEmpty() ? result.finishedTurn.assistantContent
                                                   : fullContent,
                             220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("本轮执行已完成"));
        taskExtra.insert(QStringLiteral("next_step"), QJsonValue::Null);
        taskExtra.insert(QStringLiteral("last_error"), QJsonValue::Null);
        taskExtra.insert(QStringLiteral("waiting_job_id"), QJsonValue::Null);
        updateTaskStateForSession(sessionId, QStringLiteral("done"), &result.finishedTurn, taskExtra);
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const HeartbeatReplyUtils::DeliveryDecision heartbeatDelivery =
        HeartbeatReplyUtils::evaluateReplyDelivery(
            result.finishedTurn.assistantContent,
            backgroundHeartbeat,
            m_app.m_memoryService->heartbeatService()
                ? m_app.m_memoryService->heartbeatService()->configForAgent(result.agentId).duplicateWindowMs
                : (24 * 60 * 60 * 1000),
            result.heartbeatTurn
                ? ConversationCompletionAccess::ensureHeartbeatRuntimeState(
                      *this,
                      result.agentId,
                      ConversationCompletionAccess::makeHeartbeatStateStore(m_app))
                      .lastDeliveredAtUtc
                : QDateTime(),
            result.heartbeatTurn
                ? ConversationCompletionAccess::ensureHeartbeatRuntimeState(
                      *this,
                      result.agentId,
                      ConversationCompletionAccess::makeHeartbeatStateStore(m_app))
                      .lastDeliveredDigest
                : QString(),
            nowUtc);

    const QString normalizedAssistantContent = heartbeatDelivery.normalizedText;
    const bool heartbeatNoChangeReply =
        result.heartbeatTurn && heartbeatDelivery.reason == QLatin1String("no_change_reply");
    bool suppressHeartbeatReply = false;
    QString heartbeatReplySkipReason;
    const QString heartbeatReplyDigestValue = heartbeatDelivery.digest;

    if (result.heartbeatTurn && !result.agentId.isEmpty()) {
        HeartbeatStateStore stateStore =
            ConversationCompletionAccess::makeHeartbeatStateStore(m_app);
        HeartbeatRuntimeState& runtimeState =
            ConversationCompletionAccess::ensureHeartbeatRuntimeState(
                *this, result.agentId, stateStore);
        if (heartbeatDelivery.suppress && backgroundHeartbeat) {
            suppressHeartbeatReply = true;
            heartbeatReplySkipReason = heartbeatDelivery.reason;
            runtimeState.stateObj.insert(QStringLiteral("last_duplicate_suppressed_at_utc"),
                                         nowUtc.toString(Qt::ISODateWithMs));
            runtimeState.stateObj.insert(QStringLiteral("last_duplicate_digest"),
                                         heartbeatReplyDigestValue);
            runtimeState.stateObj.insert(QStringLiteral("last_duplicate_reason"),
                                         heartbeatReplySkipReason);
            stateStore.persist(result.agentId, &runtimeState, nowUtc, true);
        } else if (!normalizedAssistantContent.isEmpty()
                   && !heartbeatReplyDigestValue.isEmpty()) {
            runtimeState.lastDeliveredDigest = heartbeatReplyDigestValue;
            runtimeState.lastDeliveredAtUtc = nowUtc;
            runtimeState.stateObj.insert(QStringLiteral("last_delivered_at_utc"),
                                         nowUtc.toString(Qt::ISODateWithMs));
            runtimeState.stateObj.insert(QStringLiteral("last_delivered_digest"),
                                         heartbeatReplyDigestValue);
            runtimeState.stateObj.insert(QStringLiteral("last_delivered_preview"),
                                         normalizedAssistantContent.left(160));
            stateStore.persist(result.agentId, &runtimeState, nowUtc, true);
        } else if (heartbeatDelivery.suppress && !backgroundHeartbeat
                   && !heartbeatReplyDigestValue.isEmpty()) {
            runtimeState.lastDeliveredDigest = heartbeatReplyDigestValue;
            runtimeState.stateObj.insert(QStringLiteral("last_delivered_digest"),
                                         heartbeatReplyDigestValue);
            runtimeState.stateObj.insert(QStringLiteral("last_manual_suppress_at_utc"),
                                         nowUtc.toString(Qt::ISODateWithMs));
            runtimeState.stateObj.insert(QStringLiteral("last_manual_suppress_reason"),
                                         heartbeatDelivery.reason);
            stateStore.persist(result.agentId, &runtimeState, nowUtc, true);
        }
    }

    if (!skipPersistForBackgroundHeartbeat
        && !result.finishedTurn.assistantContent.trimmed().isEmpty()
        && !result.agentId.isEmpty()) {
        Message assistantMsg =
            Message::createText(sessionId, result.agentId, result.finishedTurn.assistantContent);
        assistantMsg.traceId = result.finishedTurn.requestTraceId;
        assistantMsg.turnId = result.finishedTurn.turnId;
        assistantMsg.status = Message::Status::Completed;
        m_app.m_sessionManager->postMessage(sessionId, assistantMsg);
    }

    const bool shouldSurfaceBackgroundHeartbeat =
        backgroundHeartbeat && !suppressHeartbeatReply && !heartbeatNoChangeReply
        && !result.finishedTurn.assistantContent.trimmed().isEmpty()
        && !result.agentId.isEmpty();
    if (shouldSurfaceBackgroundHeartbeat) {
        Message assistantMsg =
            Message::createText(sessionId, result.agentId, result.finishedTurn.assistantContent);
        assistantMsg.traceId = result.finishedTurn.requestTraceId;
        assistantMsg.turnId = result.finishedTurn.turnId;
        assistantMsg.status = Message::Status::Completed;
        m_app.m_sessionManager->postMessage(sessionId, assistantMsg);
    }

    const QString deliveredContent =
        shouldSurfaceBackgroundHeartbeat
            ? result.finishedTurn.assistantContent
            : (backgroundHeartbeat ? QString() : result.finishedTurn.assistantContent);

    QJsonObject extra;
    extra.insert(QStringLiteral("fullContent"), deliveredContent);
    if (!skipPersistForBackgroundHeartbeat || shouldSurfaceBackgroundHeartbeat || manualHeartbeat)
        emit m_app.finished(sessionId, deliveredContent);
    emitPipelineEvent(QStringLiteral("turn_completed"),
                      sessionId,
                      &result.finishedTurn,
                      QString(),
                      QString(),
                      extra,
                      true);
    ConversationCompletionAccess::persistCompletionContext(
        *this, sessionId, result.finishedTurn, existingTaskState, nowUtc);

    if (backgroundHeartbeat && suppressHeartbeatReply) {
        QJsonObject hbExtra;
        hbExtra.insert(QStringLiteral("agent_id"), result.agentId);
        hbExtra.insert(QStringLiteral("session_id"), sessionId);
        hbExtra.insert(QStringLiteral("reason"), heartbeatReplySkipReason);
        if (!heartbeatReplyDigestValue.isEmpty())
            hbExtra.insert(QStringLiteral("reply_digest"), heartbeatReplyDigestValue);
        emitPipelineEvent(QStringLiteral("heartbeat.skipped"),
                          sessionId,
                          &result.finishedTurn,
                          QString(),
                          heartbeatReplySkipReason,
                          hbExtra,
                          true);
    }

    ConversationCompletionAccess::handleFinishMemory(*this,
                                                     sessionId,
                                                     result.agentId,
                                                     result.finishedTurn,
                                                     result.skipMemoryForHeartbeat,
                                                     result.heartbeatTurn);
}

void ConversationService::onRuntimeError(const QString& sessionId, const QString& errorMsg)
{
    TurnTask failedTurn;
    ConversationCompletionAccess::finalizeTurnInternal(*this, sessionId, &failedTurn);

    const bool skipNotifyForBackgroundHeartbeat =
        isBackgroundHeartbeatClientMessageId(failedTurn.clientMessageId);
    const bool heartbeatTurn = isHeartbeatClientMessageId(failedTurn.clientMessageId);

    const QString agentId = agentIdentityIdForSession(sessionId);
    if (m_app.m_memoryService)
        m_app.m_memoryService->reportPulseProgress(agentId, QStringLiteral("error"));

    if (isTransientUpstreamError(errorMsg) && !agentId.isEmpty()) {
        QList<DelegateTaskScheduler::JobInfo> activeJobs =
            DelegateTaskScheduler::instance()->listJobs(agentId, true, 5);
        if (!activeJobs.isEmpty()) {
            const QString fallbackReply = buildDelegateRecoveryReply(activeJobs);

            Message assistantMsg = Message::createText(sessionId, agentId, fallbackReply);
            assistantMsg.traceId = failedTurn.requestTraceId;
            assistantMsg.turnId = failedTurn.turnId;
            assistantMsg.status = Message::Status::Completed;
            m_app.m_sessionManager->postMessage(sessionId, assistantMsg);

            QJsonObject extra;
            extra.insert(QStringLiteral("recovered"), true);
            extra.insert(QStringLiteral("reason"),
                         QStringLiteral("transient_upstream_error_with_active_delegate_jobs"));
            extra.insert(QStringLiteral("active_delegate_jobs"), activeJobs.size());
            QJsonArray jobIds;
            for (const DelegateTaskScheduler::JobInfo& job : activeJobs) {
                const QString jobId = job.jobId.trimmed();
                if (!jobId.isEmpty())
                    jobIds.append(jobId);
            }
            extra.insert(QStringLiteral("job_ids"), jobIds);
            extra.insert(QStringLiteral("error"), errorMsg);

            if (!heartbeatTurn) {
                QJsonObject taskExtra;
                taskExtra.insert(
                    QStringLiteral("reason"),
                    QStringLiteral("transient_upstream_error_with_active_delegate_jobs"));
                taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_recovered"));
                taskExtra.insert(QStringLiteral("summary"),
                                 taskStateTextPreview(fallbackReply, 220));
                taskExtra.insert(QStringLiteral("current_step"),
                                 QStringLiteral("等待后台子代理任务完成"));
                taskExtra.insert(
                    QStringLiteral("next_step"),
                    QStringLiteral("可继续跟进后台任务进度或等待完成通知"));
                taskExtra.insert(QStringLiteral("last_error"),
                                 taskStateTextPreview(errorMsg, 160));
                updateTaskStateForSession(sessionId,
                                          QStringLiteral("blocked"),
                                          &failedTurn,
                                          taskExtra);
            }

            emit m_app.finished(sessionId, fallbackReply);
            emitPipelineEvent(QStringLiteral("turn_recovered"),
                              sessionId,
                              &failedTurn,
                              QString(),
                              errorMsg,
                              extra,
                              true);
            emitPipelineEvent(QStringLiteral("turn_failed"),
                              sessionId,
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
        taskExtra.insert(QStringLiteral("summary"),
                         taskStateTextPreview(failedTurn.userContent, 220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("执行失败"));
        taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("等待重试或调整任务方向"));
        taskExtra.insert(QStringLiteral("last_error"), taskStateTextPreview(errorMsg, 160));
        updateTaskStateForSession(sessionId, QStringLiteral("failed"), &failedTurn, taskExtra);
    }

    if (!skipNotifyForBackgroundHeartbeat)
        emit m_app.errorOccurred(sessionId, errorMsg);
    emitPipelineEvent(QStringLiteral("turn_failed"),
                      sessionId,
                      &failedTurn,
                      QString(),
                      errorMsg,
                      QJsonObject(),
                      true);
}

HeartbeatStateStore ConversationCompletionAccess::makeHeartbeatStateStore(ApplicationServices& app)
{
    return HeartbeatStateStore(HeartbeatStateStore::Dependencies {
        [&app](const QString& key) {
            return app.m_persistence ? app.m_persistence->getAppState(key) : QString();
        },
        [&app](const QString& key, const QString& value) {
            return app.m_persistence ? app.m_persistence->setAppState(key, value) : false;
        },
        [&app](const QString& path) {
            return app.m_persistence ? app.m_persistence->readJsonObject(path) : QJsonObject();
        },
        [&app]() { return app.m_persistence ? app.m_persistence->agentsDirPath() : QString(); },
        []() { return DatabaseManager::instance()->isReady(); }
    });
}

HeartbeatRuntimeState& ConversationCompletionAccess::ensureHeartbeatRuntimeState(
    ConversationService& service,
    const QString& agentId,
    const HeartbeatStateStore& stateStore)
{
    HeartbeatRuntimeState& runtimeState =
        service.m_app.m_memoryService->heartbeatRuntimeByAgent()[agentId.trimmed()];
    if (!runtimeState.loaded)
        stateStore.load(agentId.trimmed(), &runtimeState);
    return runtimeState;
}

void ConversationCompletionAccess::persistCompletionContext(ConversationService& service,
                                                            const QString& sessionId,
                                                            const TurnTask& finishedTurn,
                                                            const QJsonObject& existingTaskState,
                                                            const QDateTime& nowUtc)
{
    if (!service.m_app.m_persistence)
        return;
    const ConversationContextService contextService(
        ConversationContextService::Dependencies {
            [&service](const QString& sid, const ConversationContext::TaskContextSnapshot& snapshot) {
                return service.m_app.m_persistence
                    && service.m_app.m_persistence->saveTaskContextSnapshot(sid, snapshot);
            },
            [&service](const QString& sid,
                       const ConversationContext::ContextCompressionCheckpoint& checkpoint) {
                return service.m_app.m_persistence
                    && service.m_app.m_persistence->saveContextCompressionCheckpoint(sid, checkpoint);
            },
            [&service](const QString& sid, const ConversationContext::ResumePacket& packet) {
                return service.m_app.m_persistence
                    && service.m_app.m_persistence->saveResumePacket(sid, packet);
            },
            [&service](const QString& sid, bool* ok) {
                return service.m_app.m_persistence
                    ? service.m_app.m_persistence->loadTaskContextSnapshot(sid, ok)
                    : ConversationContext::TaskContextSnapshot();
            },
            [&service](const QString& sid,
                       const QString& type,
                       const TurnTask* turn,
                       const QString& delta,
                       const QString& error,
                       const QJsonObject& extra,
                       bool persistToDisk) {
                service.emitPipelineEvent(type, sid, turn, delta, error, extra, persistToDisk);
            }
        });
    contextService.persistCompletionArtifacts(sessionId, finishedTurn, existingTaskState, nowUtc);
}

void ConversationCompletionAccess::handleFinishMemory(ConversationService& service,
                                                      const QString& sessionId,
                                                      const QString& agentId,
                                                      const TurnTask& finishedTurn,
                                                      bool skipMemoryForHeartbeat,
                                                      bool heartbeatTurn)
{
    if (!service.m_app.m_memoryService)
        return;

    if (skipMemoryForHeartbeat) {
        QJsonObject memoryExtra;
        memoryExtra.insert(QStringLiteral("reason"), QStringLiteral("heartbeat_turn"));
        memoryExtra.insert(
            QStringLiteral("reflection_triggered"),
            service.m_app.m_memoryService->memoryManager()
                && service.m_app.m_memoryService->memoryManager()->reflectionEnabled());
        service.emitPipelineEvent(QStringLiteral("memory.skipped"),
                                  sessionId,
                                  &finishedTurn,
                                  QString(),
                                  QString(),
                                  memoryExtra,
                                  true);
        if (!agentId.isEmpty()) {
            service.m_app.m_memoryService->maybeReflectMemoryAndEmit(
                sessionId,
                agentId,
                finishedTurn,
                true,
                QStringLiteral("heartbeat_turn"));
        }
        return;
    }

    if (!service.m_app.m_memoryService->memoryManager() || agentId.isEmpty())
        return;

    QString memorySummary;
    QString memoryPath;
    QJsonObject memoryMetadata;
    QString memoryError;
    const bool retained = service.m_app.m_memoryService->memoryManager()->retainTurn(
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
            service.emitPipelineEvent(QStringLiteral("memory.updated"),
                                      sessionId,
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
            compactExtra.insert(QStringLiteral("path"),
                                memoryMetadata.value(QStringLiteral("longMemoryPath")).toString());
            compactExtra.insert(QStringLiteral("longMemoryAdded"),
                                memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt());
            compactExtra.insert(
                QStringLiteral("longMemoryDuplicate"),
                memoryMetadata.value(QStringLiteral("longMemoryDuplicate")).toInt());
            compactExtra.insert(QStringLiteral("manualRemember"),
                                memoryMetadata.value(QStringLiteral("manualRemember")).toBool());
            for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
                compactExtra.insert(it.key(), it.value());
            service.emitPipelineEvent(QStringLiteral("memory.compacted"),
                                      sessionId,
                                      &finishedTurn,
                                      QString(),
                                      QString(),
                                      compactExtra,
                                      true);
        }

        service.m_app.m_memoryService->refreshMemoryIndexAndEmit(sessionId,
                                                                 agentId,
                                                                 &finishedTurn,
                                                                 QStringLiteral("retain_turn"),
                                                                 memoryPath,
                                                                 memoryMetadata);
        service.m_app.m_memoryService->maybeReflectMemoryAndEmit(
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
    service.emitPipelineEvent(QStringLiteral("memory.error"),
                              sessionId,
                              &finishedTurn,
                              QString(),
                              memoryError.isEmpty() ? QStringLiteral("memory retain failed")
                                                    : memoryError,
                              memoryExtra,
                              true);
}

void ConversationCompletionAccess::finalizeTurnInternal(ConversationService& service,
                                                        const QString& sessionId,
                                                        TurnTask* outTurn)
{
    SessionPipeline* pipeline = service.findPipeline(sessionId);
    TurnTask* activeTurn = service.m_turnManager.activeTurn(sessionId);
    if (pipeline && activeTurn)
        service.flushPendingDeltaLog(sessionId, pipeline, activeTurn, true);

    service.m_turnManager.clearActiveTurn(sessionId, outTurn);
    service.clearDelegateStartsForSession(sessionId);
    service.clearToolProgressCacheForSession(sessionId);

    const QString agentId = service.agentIdentityIdForSession(sessionId);
    if (!agentId.isEmpty() && service.m_agentActiveSession.value(agentId) == sessionId)
        service.m_agentActiveSession.remove(agentId);
    service.resetSessionStreamState(sessionId);

    service.tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        service.tryStartNextTurnForAgent(agentId);
}
