#include "ChatCoordinatorFactory.h"

#include "AgentPulse.h"
#include "AgentPulseRegistry.h"
#include "AgentRuntime.h"
#include "ChatCoordinatorSupport.h"
#include "ChatService.h"
#include "HeartbeatPromptBuilder.h"
#include "HeartbeatStateStore.h"
#include "HeartbeatService.h"
#include "SchedulerService.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/memory/MemoryManager.h"
#include "core/model/Identity.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "llm/ModelFactory.h"
#include <QDateTime>

namespace {
using ChatCoordinatorSupport::buildDelegateRecoveryReply;
using ChatCoordinatorSupport::delegateToolKey;
using ChatCoordinatorSupport::estimateHistoryChars;
using ChatCoordinatorSupport::isBackgroundHeartbeatClientMessageId;
using ChatCoordinatorSupport::isHeartbeatClientMessageId;
using ChatCoordinatorSupport::isManualHeartbeatClientMessageId;
using ChatCoordinatorSupport::isTransientUpstreamError;
using ChatCoordinatorSupport::pulseStateToString;
using ChatCoordinatorSupport::sanitizePersistedToolArguments;
using ChatCoordinatorSupport::sanitizePersistedToolEventData;
using ChatCoordinatorSupport::sanitizePersistedToolRawResult;
using ChatCoordinatorSupport::taskStateTextPreview;
using ChatCoordinatorSupport::toolEventToJson;
} // namespace

ChatCoordinatorFactory::ChatCoordinatorFactory(ChatService& service)
    : m_service(service)
{
}

PrimarySessionResolver ChatCoordinatorFactory::makePrimarySessionResolver() const
{
    PrimarySessionResolver::Dependencies dependencies;
    dependencies.identityManager = m_service.m_identityManager;
    dependencies.sessionManager = m_service.m_sessionManager;
    dependencies.userIdentityId = [this]() {
        return m_service.m_identityManager && m_service.m_identityManager->userIdentity()
            ? m_service.m_identityManager->userIdentity()->id()
            : QString();
    };
    dependencies.createSessionForIdentityAs = [this](const QString& actorIdentityId,
                                                     const QString& identityId,
                                                     const QString& title) {
        return m_service.createSessionForIdentityAs(actorIdentityId, identityId, title);
    };
    return PrimarySessionResolver(dependencies);
}

HeartbeatPromptBuilder ChatCoordinatorFactory::makeHeartbeatPromptBuilder() const
{
    HeartbeatPromptBuilder::Dependencies dependencies;
    dependencies.heartbeatPathForAgent = [this](const QString& agentId) {
        return m_service.m_heartbeatService ? m_service.m_heartbeatService->heartbeatPathForAgent(agentId) : QString();
    };
    return HeartbeatPromptBuilder(dependencies);
}

HeartbeatStateStore ChatCoordinatorFactory::makeHeartbeatStateStore() const
{
    HeartbeatStateStore::Dependencies dependencies;
    dependencies.loadAppState = [this](const QString& key) {
        return m_service.m_persistence ? m_service.m_persistence->getAppState(key) : QString();
    };
    dependencies.saveAppState = [this](const QString& key, const QString& value) {
        return m_service.m_persistence ? m_service.m_persistence->setAppState(key, value) : false;
    };
    dependencies.readJsonObject = [this](const QString& path) {
        return m_service.m_persistence ? m_service.m_persistence->readJsonObject(path) : QJsonObject();
    };
    dependencies.agentsDirPath = [this]() {
        return m_service.m_persistence ? m_service.m_persistence->agentsDirPath() : QString();
    };
    dependencies.databaseReady = []() { return DatabaseManager::instance()->isReady(); };
    return HeartbeatStateStore(dependencies);
}

ConversationEnqueueCoordinator::Dependencies ChatCoordinatorFactory::makeEnqueueDependencies()
{
    ConversationEnqueueCoordinator::Dependencies dependencies;
    dependencies.identityManager = m_service.m_identityManager;
    dependencies.sessionManager = m_service.m_sessionManager;
    dependencies.turnManager = &m_service.m_turnManager;
    dependencies.canIdentitySendMessage = [this](const QString& identityId, const QString& sid) {
        return m_service.canIdentitySendMessage(identityId, sid);
    };
    dependencies.emitPipelineEvent = [this](const QString& sid,
                                            const QString& type,
                                            const TurnTask* turn,
                                            const QString& delta,
                                            const QString& error,
                                            const QJsonObject& extra,
                                            bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, turn, delta, error, extra, persistToDisk);
    };
    dependencies.updateTaskStateForSession = [this](const QString& sid,
                                                    const QString& state,
                                                    const TurnTask* turn,
                                                    const QJsonObject& extra) {
        m_service.updateTaskStateForSession(sid, state, turn, extra);
    };
    dependencies.tryStartNextTurn = [this](const QString& sid) { m_service.tryStartNextTurn(sid); };
    dependencies.isBackgroundClientMessage = [](const QString& id) {
        return isBackgroundHeartbeatClientMessageId(id);
    };
    dependencies.taskStateTextPreview = [](const QString& text, int maxChars) {
        return taskStateTextPreview(text, maxChars);
    };
    return dependencies;
}

ConversationEnqueueCoordinator::Limits ChatCoordinatorFactory::makeEnqueueLimits() const
{
    ConversationEnqueueCoordinator::Limits limits;
    limits.softQueueDepth = ChatService::kSoftQueueDepth;
    limits.hardQueueDepth = ChatService::kHardQueueDepth;
    limits.queueMergeWindowMs = ChatService::kQueueMergeWindowMs;
    limits.queueMergeMaxMergedMessages = ChatService::kQueueMergeMaxMergedMessages;
    limits.queueMergeMaxChars = ChatService::kQueueMergeMaxChars;
    return limits;
}

ConversationDispatchCoordinator::Dependencies ChatCoordinatorFactory::makeDispatchDependencies()
{
    ConversationDispatchCoordinator::Dependencies dependencies;
    dependencies.turnManager = &m_service.m_turnManager;
    dependencies.findPipeline = [this](const QString& sid) { return m_service.findPipeline(sid); };
    dependencies.ensureRuntimeIdentityForSession = [this](const QString& sid, QString* agentIdOut) -> Identity* {
        AgentRuntime* runtime = m_service.ensureRuntimeForSession(sid);
        if (!runtime)
            return nullptr;
        if (agentIdOut)
            *agentIdOut = runtime->identityId().trimmed();
        return runtime->identity();
    };
    dependencies.findSession = [this](const QString& sid) {
        return m_service.m_sessionManager ? m_service.m_sessionManager->findById(sid) : nullptr;
    };
    dependencies.activeSessionForAgent = [this](const QString& agentId) {
        return m_service.m_agentActiveSession.value(agentId);
    };
    dependencies.setActiveSessionForAgent = [this](const QString& agentId, const QString& sid) {
        m_service.m_agentActiveSession.insert(agentId, sid);
    };
    dependencies.buildRuntimeHistoryFromMessages = [this](Session* session) {
        return m_service.buildRuntimeHistoryFromMessages(session);
    };
    dependencies.estimateHistoryChars = [](const QJsonArray& history) {
        return static_cast<qint64>(estimateHistoryChars(history));
    };
    dependencies.setRuntimeHistory = [this](const QString& sid, const QJsonArray& history) {
        AgentRuntime* runtime = m_service.runtimeForSession(sid);
        if (runtime)
            runtime->setHistory(history);
    };
    dependencies.setRuntimeConfig = [this](const QString& sid, const LLMConfig& config) {
        AgentRuntime* runtime = m_service.runtimeForSession(sid);
        if (runtime)
            runtime->setConfig(config);
    };
    dependencies.setRuntimeIoContext = [this](const QString& sid, const QJsonObject& context) {
        AgentRuntime* runtime = m_service.runtimeForSession(sid);
        if (runtime)
            runtime->setIoContext(context);
    };
    dependencies.sendRuntimeMessage = [this](const QString& sid, const QString& prompt) {
        AgentRuntime* runtime = m_service.runtimeForSession(sid);
        if (runtime)
            runtime->sendMessage(sid, prompt);
    };
    dependencies.emitPipelineEvent = [this](const QString& sid,
                                            const QString& type,
                                            const TurnTask* turn,
                                            const QString& delta,
                                            const QString& error,
                                            const QJsonObject& extra,
                                            bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, turn, delta, error, extra, persistToDisk);
    };
    dependencies.updateTaskStateForSession = [this](const QString& sid,
                                                    const QString& state,
                                                    const TurnTask* turn,
                                                    const QJsonObject& extra) {
        m_service.updateTaskStateForSession(sid, state, turn, extra);
    };
    dependencies.taskStateTextPreview = [](const QString& text, int maxChars) {
        return taskStateTextPreview(text, maxChars);
    };
    dependencies.reportPulseProgress = [this](const QString& agentId, const QString& summary) {
        m_service.reportPulseProgress(agentId, summary);
    };
    dependencies.ensureMemoryInitializedForAgent = [this](Identity* identity) {
        m_service.ensureMemoryInitializedForAgent(identity);
    };
    dependencies.composeConfigForIdentity = [this](Identity* identity) {
        return m_service.composeConfigForIdentity(identity);
    };
    dependencies.composeMemoryContext = [this](const QString& agentId, int maxChars) {
        return m_service.m_memoryManager ? m_service.m_memoryManager->composeMemoryContext(agentId, maxChars) : QString();
    };
    dependencies.delegateContextForAgent = [](const QString& agentId) {
        return DelegateTaskScheduler::instance()->formatActiveJobsContext(agentId);
    };
    return dependencies;
}

ConversationDispatchCoordinator::Limits ChatCoordinatorFactory::makeDispatchLimits() const
{
    ConversationDispatchCoordinator::Limits limits;
    limits.memoryContextMaxChars = ChatService::kMemoryContextMaxChars;
    return limits;
}

ConversationFinalizeCoordinator::Dependencies ChatCoordinatorFactory::makeFinalizeDependencies()
{
    ConversationFinalizeCoordinator::Dependencies dependencies;
    dependencies.findPipeline = [this](const QString& sid) { return m_service.findPipeline(sid); };
    dependencies.activeTurn = [this](const QString& sid) { return m_service.m_turnManager.activeTurn(sid); };
    dependencies.flushPendingDeltaLog = [this](const QString& sid, SessionPipeline* pipeline, const TurnTask* turn, bool force) {
        m_service.flushPendingDeltaLog(sid, pipeline, turn, force);
    };
    dependencies.turnManager = &m_service.m_turnManager;
    dependencies.clearDelegateStartsForSession = [this](const QString& sid) { m_service.clearDelegateStartsForSession(sid); };
    dependencies.clearToolProgressCacheForSession = [this](const QString& sid) { m_service.clearToolProgressCacheForSession(sid); };
    dependencies.agentIdentityIdForSession = [this](const QString& sid) { return m_service.agentIdentityIdForSession(sid); };
    dependencies.activeSessionForAgent = [this](const QString& agentId) { return m_service.m_agentActiveSession.value(agentId); };
    dependencies.clearActiveSessionForAgent = [this](const QString& agentId) { m_service.m_agentActiveSession.remove(agentId); };
    dependencies.resetSessionStreamState = [this](const QString& sid) { m_service.resetSessionStreamState(sid); };
    dependencies.tryStartNextTurn = [this](const QString& sid) { m_service.tryStartNextTurn(sid); };
    dependencies.tryStartNextTurnForAgent = [this](const QString& agentId) { m_service.tryStartNextTurnForAgent(agentId); };
    return dependencies;
}

ConversationStreamCoordinator::Dependencies ChatCoordinatorFactory::makeStreamDependencies()
{
    ConversationStreamCoordinator::Dependencies dependencies;
    dependencies.findPipeline = [this](const QString& sid) { return m_service.findPipeline(sid); };
    dependencies.activeTurn = [this](const QString& sid) { return m_service.m_turnManager.activeTurn(sid); };
    dependencies.agentIdentityIdForSession = [this](const QString& sid) { return m_service.agentIdentityIdForSession(sid); };
    dependencies.reportPulseProgress = [this](const QString& agentId, const QString& summary) {
        m_service.reportPulseProgress(agentId, summary);
    };
    dependencies.isBackgroundClientMessage = [](const QString& clientMessageId) {
        return isBackgroundHeartbeatClientMessageId(clientMessageId);
    };
    dependencies.findSession = [this](const QString& sid) {
        return m_service.m_sessionManager ? m_service.m_sessionManager->findById(sid) : nullptr;
    };
    dependencies.emitStreamData = [this](const QString& sid, const QString& chunk) {
        emit m_service.streamDataReceived(sid, chunk);
    };
    dependencies.emitPipelineEvent = [this](const QString& sid,
                                            const QString& type,
                                            const TurnTask* turn,
                                            const QString& delta,
                                            const QString& error,
                                            const QJsonObject& extra,
                                            bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, turn, delta, error, extra, persistToDisk);
    };
    dependencies.flushPendingDeltaLog = [this](const QString& sid, SessionPipeline* pipeline, const TurnTask* turn, bool force) {
        m_service.flushPendingDeltaLog(sid, pipeline, turn, force);
    };
    dependencies.logVerboseStreamEvents = m_service.m_logVerboseStreamEvents;
    return dependencies;
}

ConversationFinishCoordinator::Dependencies ChatCoordinatorFactory::makeFinishDependencies()
{
    const HeartbeatStateStore stateStore = makeHeartbeatStateStore();
    auto ensureRuntimeState = [this, stateStore](const QString& agentId) -> HeartbeatRuntimeState& {
        const QString trimmedAgentId = agentId.trimmed();
        HeartbeatRuntimeState& runtimeState = m_service.m_heartbeatRuntimeByAgent[trimmedAgentId];
        if (!runtimeState.loaded)
            stateStore.load(trimmedAgentId, &runtimeState);
        return runtimeState;
    };

    ConversationFinishCoordinator::Dependencies dependencies;
    dependencies.finalizeTurn = [this](const QString& sid, TurnTask* outTurn) {
        m_service.finalizeTurn(sid, outTurn);
    };
    dependencies.agentIdentityIdForSession = [this](const QString& sid) { return m_service.agentIdentityIdForSession(sid); };
    dependencies.isBackgroundClientMessage = [](const QString& clientMessageId) {
        return isBackgroundHeartbeatClientMessageId(clientMessageId);
    };
    dependencies.isHeartbeatClientMessage = [](const QString& clientMessageId) {
        return isHeartbeatClientMessageId(clientMessageId);
    };
    dependencies.isManualHeartbeatClientMessage = [](const QString& clientMessageId) {
        return isManualHeartbeatClientMessageId(clientMessageId);
    };
    dependencies.reportPulseProgress = [this](const QString& agentId, const QString& summary) {
        m_service.reportPulseProgress(agentId, summary);
    };
    dependencies.taskStateForSession = [this](const QString& sid) { return m_service.taskStateForSession(sid); };
    dependencies.updateTaskStateForSession = [this](const QString& sid,
                                                    const QString& state,
                                                    const TurnTask* turn,
                                                    const QJsonObject& extra) {
        m_service.updateTaskStateForSession(sid, state, turn, extra);
    };
    dependencies.taskStateTextPreview = [](const QString& text, int maxChars) {
        return taskStateTextPreview(text, maxChars);
    };
    dependencies.heartbeatDuplicateWindowMs = [this](const QString& agentId) {
        return m_service.m_heartbeatService ? m_service.m_heartbeatService->configForAgent(agentId).duplicateWindowMs : (24 * 60 * 60 * 1000);
    };
    dependencies.heartbeatLastDeliveredAt = [ensureRuntimeState](const QString& agentId) {
        return ensureRuntimeState(agentId).lastDeliveredAtUtc;
    };
    dependencies.heartbeatLastDeliveredDigest = [ensureRuntimeState](const QString& agentId) {
        return ensureRuntimeState(agentId).lastDeliveredDigest;
    };
    dependencies.recordHeartbeatSuppressed = [ensureRuntimeState, stateStore](const QString& agentId,
                                                                              const QString& digest,
                                                                              const QString& reason,
                                                                              const QDateTime& nowUtc) mutable {
        HeartbeatRuntimeState& runtimeState = ensureRuntimeState(agentId);
        runtimeState.stateObj.insert(QStringLiteral("last_duplicate_suppressed_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
        runtimeState.stateObj.insert(QStringLiteral("last_duplicate_digest"), digest);
        runtimeState.stateObj.insert(QStringLiteral("last_duplicate_reason"), reason);
        stateStore.persist(agentId, &runtimeState, nowUtc, true);
    };
    dependencies.recordHeartbeatDelivered = [ensureRuntimeState, stateStore](const QString& agentId,
                                                                             const QString& digest,
                                                                             const QString& preview,
                                                                             const QDateTime& nowUtc) mutable {
        HeartbeatRuntimeState& runtimeState = ensureRuntimeState(agentId);
        runtimeState.lastDeliveredDigest = digest;
        runtimeState.lastDeliveredAtUtc = nowUtc;
        runtimeState.stateObj.insert(QStringLiteral("last_delivered_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
        runtimeState.stateObj.insert(QStringLiteral("last_delivered_digest"), digest);
        runtimeState.stateObj.insert(QStringLiteral("last_delivered_preview"), preview);
        stateStore.persist(agentId, &runtimeState, nowUtc, true);
    };
    dependencies.recordHeartbeatManualSuppress = [ensureRuntimeState, stateStore](const QString& agentId,
                                                                                  const QString& digest,
                                                                                  const QString& reason,
                                                                                  const QDateTime& nowUtc) mutable {
        HeartbeatRuntimeState& runtimeState = ensureRuntimeState(agentId);
        runtimeState.lastDeliveredDigest = digest;
        runtimeState.stateObj.insert(QStringLiteral("last_delivered_digest"), digest);
        runtimeState.stateObj.insert(QStringLiteral("last_manual_suppress_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
        runtimeState.stateObj.insert(QStringLiteral("last_manual_suppress_reason"), reason);
        stateStore.persist(agentId, &runtimeState, nowUtc, true);
    };
    dependencies.postMessage = [this](const QString& sid, const Message& message) {
        if (m_service.m_sessionManager)
            m_service.m_sessionManager->postMessage(sid, message);
    };
    dependencies.emitFinished = [this](const QString& sid, const QString& content) {
        emit m_service.finished(sid, content);
    };
    dependencies.emitPipelineEvent = [this](const QString& sid,
                                            const QString& type,
                                            const TurnTask* turn,
                                            const QString& delta,
                                            const QString& error,
                                            const QJsonObject& extra,
                                            bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, turn, delta, error, extra, persistToDisk);
    };
    return dependencies;
}

ConversationMemoryFinishCoordinator::Dependencies ChatCoordinatorFactory::makeMemoryFinishDependencies()
{
    ConversationMemoryFinishCoordinator::Dependencies dependencies;
    dependencies.reflectionEnabled = [this]() {
        return m_service.m_memoryManager && m_service.m_memoryManager->reflectionEnabled();
    };
    dependencies.retainTurn = [this](const QString& aid,
                                     const QString& sid,
                                     const TurnTask& turn,
                                     QString* summary,
                                     QString* path,
                                     QJsonObject* metadata,
                                     QString* error) {
        return m_service.m_memoryManager
            ? m_service.m_memoryManager->retainTurn(aid, sid, turn, summary, path, metadata, error)
            : false;
    };
    dependencies.refreshMemoryIndexAndEmit = [this](const QString& sid,
                                                    const QString& aid,
                                                    const TurnTask* turn,
                                                    const QString& reason,
                                                    const QString& sourcePath,
                                                    const QJsonObject& sourceMetadata) {
        m_service.refreshMemoryIndexAndEmit(sid, aid, turn, reason, sourcePath, sourceMetadata);
    };
    dependencies.maybeReflectMemoryAndEmit = [this](const QString& sid,
                                                    const QString& aid,
                                                    const TurnTask& turn,
                                                    bool forceReflection,
                                                    const QString& triggerReason) {
        m_service.maybeReflectMemoryAndEmit(sid, aid, turn, forceReflection, triggerReason);
    };
    dependencies.emitPipelineEvent = [this](const QString& sid,
                                            const QString& type,
                                            const TurnTask* turn,
                                            const QString& delta,
                                            const QString& error,
                                            const QJsonObject& extra,
                                            bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, turn, delta, error, extra, persistToDisk);
    };
    return dependencies;
}

ConversationErrorCoordinator::Dependencies ChatCoordinatorFactory::makeErrorDependencies()
{
    ConversationErrorCoordinator::Dependencies dependencies;
    dependencies.finalizeTurn = [this](const QString& sid, TurnTask* outTurn) {
        m_service.finalizeTurn(sid, outTurn);
    };
    dependencies.isBackgroundClientMessage = [](const QString& clientMessageId) {
        return isBackgroundHeartbeatClientMessageId(clientMessageId);
    };
    dependencies.isHeartbeatClientMessage = [](const QString& clientMessageId) {
        return isHeartbeatClientMessageId(clientMessageId);
    };
    dependencies.agentIdentityIdForSession = [this](const QString& sid) { return m_service.agentIdentityIdForSession(sid); };
    dependencies.reportPulseProgress = [this](const QString& agentId, const QString& summary) {
        m_service.reportPulseProgress(agentId, summary);
    };
    dependencies.isTransientUpstreamError = [](const QString& err) {
        return isTransientUpstreamError(err);
    };
    dependencies.listActiveJobs = [](const QString& agentId, int limit) {
        QList<ConversationErrorCoordinator::ActiveJobInfo> jobs;
        const QList<DelegateTaskScheduler::JobInfo> rawJobs =
            DelegateTaskScheduler::instance()->listJobs(agentId, true, limit);
        for (const DelegateTaskScheduler::JobInfo& job : rawJobs) {
            ConversationErrorCoordinator::ActiveJobInfo info;
            info.jobId = job.jobId;
            info.status = job.status;
            info.summary = job.summary;
            jobs.append(info);
        }
        return jobs;
    };
    dependencies.buildDelegateRecoveryReply = [](const QList<ConversationErrorCoordinator::ActiveJobInfo>& jobs) {
        QList<DelegateTaskScheduler::JobInfo> rawJobs;
        for (const ConversationErrorCoordinator::ActiveJobInfo& job : jobs) {
            DelegateTaskScheduler::JobInfo info;
            info.jobId = job.jobId;
            info.status = job.status;
            info.summary = job.summary;
            rawJobs.append(info);
        }
        return buildDelegateRecoveryReply(rawJobs);
    };
    dependencies.postMessage = [this](const QString& sid, const Message& message) {
        if (m_service.m_sessionManager)
            m_service.m_sessionManager->postMessage(sid, message);
    };
    dependencies.updateTaskStateForSession = [this](const QString& sid,
                                                    const QString& state,
                                                    const TurnTask* turn,
                                                    const QJsonObject& extra) {
        m_service.updateTaskStateForSession(sid, state, turn, extra);
    };
    dependencies.taskStateTextPreview = [](const QString& text, int maxChars) {
        return taskStateTextPreview(text, maxChars);
    };
    dependencies.emitFinished = [this](const QString& sid, const QString& content) {
        emit m_service.finished(sid, content);
    };
    dependencies.emitError = [this](const QString& sid, const QString& error) {
        emit m_service.errorOccurred(sid, error);
    };
    dependencies.emitPipelineEvent = [this](const QString& sid,
                                            const QString& type,
                                            const TurnTask* turn,
                                            const QString& delta,
                                            const QString& error,
                                            const QJsonObject& extra,
                                            bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, turn, delta, error, extra, persistToDisk);
    };
    return dependencies;
}

ConversationToolEventCoordinator::Dependencies ChatCoordinatorFactory::makeToolEventDependencies()
{
    ConversationToolEventCoordinator::Dependencies dependencies;
    dependencies.agentIdentityIdForSession = [this](const QString& sid) { return m_service.agentIdentityIdForSession(sid); };
    dependencies.reportPulseProgress = [this](const QString& id, const QString& summary) {
        m_service.reportPulseProgress(id, summary);
    };
    dependencies.updateTaskStateForSession = [this](const QString& sid,
                                                    const QString& state,
                                                    const TurnTask* turn,
                                                    const QJsonObject& extra) {
        m_service.updateTaskStateForSession(sid, state, turn, extra);
    };
    dependencies.taskStateTextPreview = [](const QString& text, int maxChars) {
        return taskStateTextPreview(text, maxChars);
    };
    dependencies.suppressHeartbeat = [this](const QString& id, const QString& reason) {
        if (m_service.m_heartbeatService && !id.isEmpty())
            m_service.m_heartbeatService->suppressHeartbeat(id, reason);
    };
    dependencies.unsuppressHeartbeat = [this](const QString& id) {
        if (m_service.m_heartbeatService && !id.isEmpty())
            m_service.m_heartbeatService->unsuppressHeartbeat(id);
    };
    dependencies.emitPipelineEvent = [this](const QString& sid,
                                            const QString& type,
                                            const TurnTask* turn,
                                            const QString& delta,
                                            const QString& error,
                                            const QJsonObject& extra,
                                            bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, turn, delta, error, extra, persistToDisk);
    };
    dependencies.takeDelegateStartMs = [this](const QString& sid, const QString& id) -> qint64 {
        if (id.isEmpty())
            return -1;
        const QString key = delegateToolKey(sid, id);
        if (!m_service.m_delegateStartMsByToolKey.contains(key))
            return -1;
        const qint64 durationMs =
            QDateTime::currentMSecsSinceEpoch() - m_service.m_delegateStartMsByToolKey.value(key);
        m_service.m_delegateStartMsByToolKey.remove(key);
        return durationMs;
    };
    dependencies.putDelegateStartMs = [this](const QString& sid, const QString& id, qint64 startedAtMs) {
        if (!id.isEmpty())
            m_service.m_delegateStartMsByToolKey.insert(delegateToolKey(sid, id), startedAtMs);
    };
    dependencies.delegateStatsForSession = [this](const QString& sid) {
        ConversationToolEventCoordinator::DelegateStats stats;
        const ChatService::DelegateStats existing = m_service.m_delegateStatsBySession.value(sid);
        stats.totalCount = existing.totalCount;
        stats.successCount = existing.successCount;
        stats.failureCount = existing.failureCount;
        stats.totalDurationMs = existing.totalDurationMs;
        return stats;
    };
    dependencies.setDelegateStatsForSession = [this](const QString& sid,
                                                     const ConversationToolEventCoordinator::DelegateStats& stats) {
        ChatService::DelegateStats out;
        out.totalCount = stats.totalCount;
        out.successCount = stats.successCount;
        out.failureCount = stats.failureCount;
        out.totalDurationMs = stats.totalDurationMs;
        m_service.m_delegateStatsBySession.insert(sid, out);
    };
    return dependencies;
}

ConversationToolPersistenceCoordinator::Dependencies ChatCoordinatorFactory::makeToolPersistenceDependencies()
{
    ConversationToolPersistenceCoordinator::Dependencies dependencies;
    dependencies.postMessage = [this](const QString& sid, const Message& message) {
        if (m_service.m_sessionManager)
            m_service.m_sessionManager->postMessage(sid, message);
    };
    dependencies.sessionDataDirPath = [this](const QString& sid) {
        return m_service.m_persistence ? m_service.m_persistence->sessionDataDirPath(sid) : QString();
    };
    dependencies.sanitizePersistedToolArguments = [](const QString& name, const QJsonObject& args) {
        return sanitizePersistedToolArguments(name, args);
    };
    dependencies.sanitizePersistedToolEventData = [](const QString& name, const QJsonObject& data) {
        return sanitizePersistedToolEventData(name, data);
    };
    dependencies.sanitizePersistedToolRawResult = [](const QString& name, const QString& raw) {
        return sanitizePersistedToolRawResult(name, raw);
    };
    dependencies.toolEventToJson = [](const ToolExecutionEvent& toolEvent) {
        return toolEventToJson(toolEvent);
    };
    dependencies.emitToolEvent = [this](const QString& sid, const ToolExecutionEvent& toolExecutionEvent) {
        emit m_service.toolEvent(sid, toolExecutionEvent);
    };
    dependencies.emitPipelineEvent = [this](const QString& sid,
                                            const QString& type,
                                            const TurnTask* turn,
                                            const QString& delta,
                                            const QString& error,
                                            const QJsonObject& extra,
                                            bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, turn, delta, error, extra, persistToDisk);
    };
    dependencies.toolProgressLastPersistMs = [this](const QString& key) {
        return m_service.m_toolProgressLastPersistMsByKey.value(key, 0);
    };
    dependencies.toolProgressLastDigest = [this](const QString& key) {
        return m_service.m_toolProgressLastDigestByKey.value(key);
    };
    dependencies.setToolProgressLastPersistMs = [this](const QString& key, qint64 value) {
        m_service.m_toolProgressLastPersistMsByKey.insert(key, value);
    };
    dependencies.setToolProgressLastDigest = [this](const QString& key, const QString& digest) {
        m_service.m_toolProgressLastDigestByKey.insert(key, digest);
    };
    dependencies.toolProgressPersistMinIntervalMs = ChatService::kToolProgressPersistMinIntervalMs;
    return dependencies;
}

DelegateSettlementCoordinator::Dependencies ChatCoordinatorFactory::makeDelegateSettlementDependencies()
{
    const PrimarySessionResolver resolver = makePrimarySessionResolver();
    DelegateSettlementCoordinator::Dependencies dependencies;
    dependencies.resolvePrimarySessionForAgent = [resolver](const QString& agentId,
                                                            bool createIfMissing,
                                                            bool isolated,
                                                            const QString& titleSuffix) {
        return resolver.resolveForAgent(agentId, createIfMissing, isolated, titleSuffix);
    };
    dependencies.postMessage = [this](const QString& sid, const Message& message) {
        if (m_service.m_sessionManager)
            m_service.m_sessionManager->postMessage(sid, message);
    };
    dependencies.updateTaskStateForSession = [this](const QString& sid,
                                                    const QString& state,
                                                    const void*,
                                                    const QJsonObject& extra) {
        m_service.updateTaskStateForSession(sid, state, nullptr, extra);
    };
    dependencies.taskStateTextPreview = [](const QString& text, int maxChars) {
        return taskStateTextPreview(text, maxChars);
    };
    dependencies.triggerHeartbeat = [this](const QString& agentId, const QString& reason) {
        if (m_service.m_heartbeatService)
            m_service.m_heartbeatService->triggerHeartbeat(agentId, reason);
    };
    dependencies.emitPipelineEventSimple = [this](const QString& sid,
                                                  const QString& type,
                                                  const QString& error,
                                                  const QString& delta,
                                                  const QJsonObject& extra,
                                                  bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, nullptr, delta, error, extra, persistToDisk);
    };
    return dependencies;
}

HeartbeatDispatchCoordinator::Dependencies ChatCoordinatorFactory::makeHeartbeatDispatchDependencies(
    HeartbeatRuntimeState& runtimeState,
    bool& shouldPersistState,
    const QDateTime& nowUtc)
{
    const HeartbeatPromptBuilder promptBuilder = makeHeartbeatPromptBuilder();
    const HeartbeatStateStore stateStore = makeHeartbeatStateStore();
    HeartbeatDispatchCoordinator::Dependencies dependencies;
    dependencies.queueDepthForSession = [this](const QString& sid) {
        return m_service.m_turnManager.totalDepth(sid);
    };
    dependencies.emitPipelineEventSimple = [this](const QString& sid,
                                                  const QString& type,
                                                  const QString& error,
                                                  const QString& delta,
                                                  const QJsonObject& extra,
                                                  bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, nullptr, delta, error, extra, persistToDisk);
    };
    dependencies.userIdentityId = [this]() {
        return m_service.m_identityManager && m_service.m_identityManager->userIdentity()
            ? m_service.m_identityManager->userIdentity()->id()
            : QString();
    };
    dependencies.buildHeartbeatPrompt = [promptBuilder](const QString& aid, const QString& reasonText) {
        return promptBuilder.build(aid, reasonText);
    };
    dependencies.pulseForAgent = [this](const QString& aid) {
        return m_service.m_agentPulseRegistry ? m_service.m_agentPulseRegistry->find(aid) : nullptr;
    };
    dependencies.pulseStateText = [](AgentPulse* pulse) {
        return pulse ? pulseStateToString(pulse->currentState()) : QString();
    };
    dependencies.enqueueUserMessageAs = [this](const QString& actorId,
                                               const QString& sid,
                                               const QString& prompt,
                                               const QString& clientMessageId) {
        return m_service.enqueueUserMessageAs(actorId, sid, prompt, clientMessageId);
    };
    dependencies.buildHeartbeatClientMessageId = [](const QString& tag, const QString& uuid) {
        return QStringLiteral("%1-%2").arg(tag, uuid);
    };
    dependencies.markHeartbeatNotified = [&](const QString&, const QString&) {
        runtimeState.lastNotifyAtUtc = nowUtc;
        runtimeState.stateObj.insert(QStringLiteral("last_notify_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
        shouldPersistState = true;
    };
    dependencies.persistStateIfNeeded = [stateStore, &runtimeState, &shouldPersistState, nowUtc](bool forcePersist) mutable {
        const bool doPersist = forcePersist || shouldPersistState;
        stateStore.persist(
            runtimeState.stateStorageKey.mid(QStringLiteral("heartbeat_state:").size()),
            &runtimeState,
            nowUtc,
            doPersist);
    };
    return dependencies;
}

SchedulerTriggerCoordinator::Dependencies ChatCoordinatorFactory::makeSchedulerTriggerDependencies()
{
    const PrimarySessionResolver resolver = makePrimarySessionResolver();
    SchedulerTriggerCoordinator::Dependencies dependencies;
    dependencies.jobById = [this](const QString& id, ScheduledJob* outJob) {
        return m_service.m_schedulerService && m_service.m_schedulerService->jobById(id, outJob);
    };
    dependencies.findIdentity = [this](const QString& identityId) {
        return m_service.m_identityManager ? m_service.m_identityManager->findById(identityId) : nullptr;
    };
    dependencies.resolvePrimarySessionForAgent = [resolver](const QString& agentId,
                                                            bool createIfMissing,
                                                            bool isolated,
                                                            const QString& titleSuffix) {
        return resolver.resolveForAgent(agentId, createIfMissing, isolated, titleSuffix);
    };
    dependencies.userIdentityId = [this]() {
        return m_service.m_identityManager && m_service.m_identityManager->userIdentity()
            ? m_service.m_identityManager->userIdentity()->id()
            : QString();
    };
    dependencies.buildClientMessageId = [](const QString& jobId,
                                           const QString& uuid,
                                           const QString&,
                                           const QString&) {
        return QStringLiteral("scheduler-%1-%2").arg(jobId, uuid);
    };
    dependencies.buildPrompt = [](const QString& jobNameArg,
                                  const QString& storedJobName,
                                  const QString& prompt,
                                  const QString&) {
        const QString displayName = jobNameArg.trimmed().isEmpty()
            ? (storedJobName.trimmed().isEmpty() ? QStringLiteral("scheduled-job") : storedJobName.trimmed())
            : jobNameArg.trimmed();
        return QStringLiteral("【定时任务:%1】\n%2").arg(displayName, prompt);
    };
    dependencies.enqueueUserMessageAs = [this](const QString& actorId,
                                               const QString& sessionId,
                                               const QString& prompt,
                                               const QString& clientMessageId) {
        return m_service.enqueueUserMessageAs(actorId, sessionId, prompt, clientMessageId);
    };
    dependencies.emitPipelineEventSimple = [this](const QString& sessionId,
                                                  const QString& type,
                                                  const QString& error,
                                                  const QString& delta,
                                                  const QJsonObject& extra,
                                                  bool persistToDisk) {
        m_service.emitPipelineEvent(type, sessionId, nullptr, delta, error, extra, persistToDisk);
    };
    return dependencies;
}
