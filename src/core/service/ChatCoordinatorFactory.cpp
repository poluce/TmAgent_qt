#include "ChatCoordinatorFactory.h"

#include "AgentPulse.h"
#include "AgentPulseRegistry.h"
#include "AgentRuntime.h"
#include "ConversationContextService.h"
#include "MemoryMaintenanceService.h"
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

// ── 共享上下文 ──

CoordinatorContext ChatCoordinatorFactory::makeSharedContext()
{
    CoordinatorContext ctx;
    ctx.emitPipelineEvent = [this](const QString& sid,
                                    const QString& type,
                                    const TurnTask* turn,
                                    const QString& delta,
                                    const QString& error,
                                    const QJsonObject& extra,
                                    bool persist) {
        m_service.emitPipelineEvent(type, sid, turn, delta, error, extra, persist);
    };
    ctx.updateTaskState = [this](const QString& sid,
                                 const QString& state,
                                 const TurnTask* turn,
                                 const QJsonObject& extra) {
        m_service.updateTaskStateForSession(sid, state, turn, extra);
    };
    ctx.taskStateTextPreview = [](const QString& text, int maxChars) {
        return taskStateTextPreview(text, maxChars);
    };
    ctx.agentIdentityIdForSession = [this](const QString& sid) {
        return m_service.agentIdentityIdForSession(sid);
    };
    ctx.reportPulseProgress = [this](const QString& agentId, const QString& summary) {
        m_service.reportPulseProgress(agentId, summary);
    };
    ctx.postMessage = [this](const QString& sid, const Message& message) {
        if (m_service.m_sessionManager)
            m_service.m_sessionManager->postMessage(sid, message);
    };
    ctx.isBackgroundClientMessage = [](const QString& id) {
        return isBackgroundHeartbeatClientMessageId(id);
    };
    ctx.isHeartbeatClientMessage = [](const QString& id) {
        return isHeartbeatClientMessageId(id);
    };
    return ctx;
}

// ── 辅助构建器 ──

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

// ── Enqueue ──

ConversationEnqueueCoordinator::Dependencies ChatCoordinatorFactory::makeEnqueueDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    ConversationEnqueueCoordinator::Dependencies deps;
    deps.identityManager = m_service.m_identityManager;
    deps.sessionManager = m_service.m_sessionManager;
    deps.turnManager = &m_service.m_turnManager;
    deps.canIdentitySendMessage = [this](const QString& identityId, const QString& sid) {
        return m_service.canIdentitySendMessage(identityId, sid);
    };
    deps.emitPipelineEvent = ctx.emitPipelineEvent;
    deps.updateTaskStateForSession = ctx.updateTaskState;
    deps.tryStartNextTurn = [this](const QString& sid) { m_service.tryStartNextTurn(sid); };
    deps.isBackgroundClientMessage = ctx.isBackgroundClientMessage;
    deps.taskStateTextPreview = ctx.taskStateTextPreview;
    return deps;
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

// ── Dispatch ──

ConversationDispatchCoordinator::Dependencies ChatCoordinatorFactory::makeDispatchDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    ConversationDispatchCoordinator::Dependencies deps;
    deps.turnManager = &m_service.m_turnManager;
    deps.findPipeline = [this](const QString& sid) { return m_service.findPipeline(sid); };
    deps.ensureRuntimeIdentityForSession = [this](const QString& sid, QString* agentIdOut) -> Identity* {
        AgentRuntime* runtime = m_service.ensureRuntimeForSession(sid);
        if (!runtime)
            return nullptr;
        if (agentIdOut)
            *agentIdOut = runtime->identityId().trimmed();
        return runtime->identity();
    };
    deps.findSession = [this](const QString& sid) {
        return m_service.m_sessionManager ? m_service.m_sessionManager->findById(sid) : nullptr;
    };
    deps.activeSessionForAgent = [this](const QString& agentId) {
        return m_service.m_agentActiveSession.value(agentId);
    };
    deps.setActiveSessionForAgent = [this](const QString& agentId, const QString& sid) {
        m_service.m_agentActiveSession.insert(agentId, sid);
    };
    deps.buildRuntimeHistoryFromMessages = [this](Session* session) {
        return m_service.buildRuntimeHistoryFromMessages(session);
    };
    deps.estimateHistoryChars = [](const QJsonArray& history) {
        return static_cast<qint64>(estimateHistoryChars(history));
    };
    deps.setRuntimeHistory = [this](const QString& sid, const QJsonArray& history) {
        AgentRuntime* runtime = m_service.runtimeForSession(sid);
        if (runtime)
            runtime->setHistory(history);
    };
    deps.setRuntimeConfig = [this](const QString& sid, const LLMConfig& config) {
        AgentRuntime* runtime = m_service.runtimeForSession(sid);
        if (runtime)
            runtime->setConfig(config);
    };
    deps.setRuntimeIoContext = [this](const QString& sid, const QJsonObject& context) {
        AgentRuntime* runtime = m_service.runtimeForSession(sid);
        if (runtime)
            runtime->setIoContext(context);
    };
    deps.sendRuntimeMessage = [this](const QString& sid, const QString& prompt) {
        AgentRuntime* runtime = m_service.runtimeForSession(sid);
        if (runtime)
            runtime->sendMessage(sid, prompt);
    };
    deps.emitPipelineEvent = ctx.emitPipelineEvent;
    deps.updateTaskStateForSession = ctx.updateTaskState;
    deps.taskStateTextPreview = ctx.taskStateTextPreview;
    deps.reportPulseProgress = ctx.reportPulseProgress;
    deps.ensureMemoryInitializedForAgent = [this](Identity* identity) {
        m_service.ensureMemoryInitializedForAgent(identity);
    };
    deps.composeConfigForIdentity = [this](Identity* identity) {
        return m_service.composeConfigForIdentity(identity);
    };
    deps.composeMemoryContext = [this](const QString& agentId, int maxChars) {
        return m_service.m_memoryManager ? m_service.m_memoryManager->composeMemoryContext(agentId, maxChars) : QString();
    };
    deps.delegateContextForAgent = [](const QString& agentId) {
        return DelegateTaskScheduler::instance()->formatActiveJobsContext(agentId);
    };
    deps.loadTaskContextSnapshot = [this](const QString& sid, bool* ok) {
        return m_service.m_persistence ? m_service.m_persistence->loadTaskContextSnapshot(sid, ok)
                                       : ConversationContext::TaskContextSnapshot();
    };
    deps.loadContextCompressionCheckpoint = [this](const QString& sid, bool* ok) {
        return m_service.m_persistence ? m_service.m_persistence->loadContextCompressionCheckpoint(sid, ok)
                                       : ConversationContext::ContextCompressionCheckpoint();
    };
    deps.drainTeammateInjections = [this](const QString& sid) -> QStringList {
        return m_service.m_teammateInjections.value(sid);
    };
    return deps;
}

ConversationDispatchCoordinator::Limits ChatCoordinatorFactory::makeDispatchLimits() const
{
    ConversationDispatchCoordinator::Limits limits;
    limits.memoryContextMaxChars = ChatService::kMemoryContextMaxChars;
    return limits;
}

// ── Stream ──

ConversationStreamCoordinator::Dependencies ChatCoordinatorFactory::makeStreamDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    ConversationStreamCoordinator::Dependencies deps;
    deps.findPipeline = [this](const QString& sid) { return m_service.findPipeline(sid); };
    deps.activeTurn = [this](const QString& sid) { return m_service.m_turnManager.activeTurn(sid); };
    deps.agentIdentityIdForSession = ctx.agentIdentityIdForSession;
    deps.reportPulseProgress = ctx.reportPulseProgress;
    deps.isBackgroundClientMessage = ctx.isBackgroundClientMessage;
    deps.findSession = [this](const QString& sid) {
        return m_service.m_sessionManager ? m_service.m_sessionManager->findById(sid) : nullptr;
    };
    deps.emitStreamData = [this](const QString& sid, const QString& chunk) {
        emit m_service.streamDataReceived(sid, chunk);
    };
    deps.emitPipelineEvent = ctx.emitPipelineEvent;
    deps.flushPendingDeltaLog = [this](const QString& sid, SessionPipeline* pipeline, const TurnTask* turn, bool force) {
        m_service.flushPendingDeltaLog(sid, pipeline, turn, force);
    };
    deps.logVerboseStreamEvents = m_service.m_logVerboseStreamEvents;
    return deps;
}

// ── TurnCompletion (合并 Finish+Finalize+Error+MemoryFinish) ──

TurnCompletionCoordinator::Dependencies ChatCoordinatorFactory::makeTurnCompletionDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    const HeartbeatStateStore stateStore = makeHeartbeatStateStore();
    auto ensureRuntimeState = [this, stateStore](const QString& agentId) -> HeartbeatRuntimeState& {
        const QString trimmedAgentId = agentId.trimmed();
        HeartbeatRuntimeState& runtimeState = m_service.m_heartbeatRuntimeByAgent[trimmedAgentId];
        if (!runtimeState.loaded)
            stateStore.load(trimmedAgentId, &runtimeState);
        return runtimeState;
    };

    TurnCompletionCoordinator::Dependencies deps;
    deps.ctx = ctx;

    // Finalize 专有
    deps.findPipeline = [this](const QString& sid) { return m_service.findPipeline(sid); };
    deps.activeTurn = [this](const QString& sid) { return m_service.m_turnManager.activeTurn(sid); };
    deps.flushPendingDeltaLog = [this](const QString& sid, SessionPipeline* pipeline, const TurnTask* turn, bool force) {
        m_service.flushPendingDeltaLog(sid, pipeline, turn, force);
    };
    deps.turnManager = &m_service.m_turnManager;
    deps.clearDelegateStartsForSession = [this](const QString& sid) { m_service.clearDelegateStartsForSession(sid); };
    deps.clearToolProgressCacheForSession = [this](const QString& sid) { m_service.clearToolProgressCacheForSession(sid); };
    deps.activeSessionForAgent = [this](const QString& agentId) { return m_service.m_agentActiveSession.value(agentId); };
    deps.clearActiveSessionForAgent = [this](const QString& agentId) { m_service.m_agentActiveSession.remove(agentId); };
    deps.resetSessionStreamState = [this](const QString& sid) { m_service.resetSessionStreamState(sid); };
    deps.tryStartNextTurn = [this](const QString& sid) { m_service.tryStartNextTurn(sid); };
    deps.tryStartNextTurnForAgent = [this](const QString& agentId) { m_service.tryStartNextTurnForAgent(agentId); };

    // Finish 专有
    deps.isManualHeartbeatClientMessage = [](const QString& clientMessageId) {
        return isManualHeartbeatClientMessageId(clientMessageId);
    };
    deps.taskStateForSession = [this](const QString& sid) { return m_service.taskStateForSession(sid); };
    deps.heartbeatDuplicateWindowMs = [this](const QString& agentId) {
        return m_service.m_heartbeatService ? m_service.m_heartbeatService->configForAgent(agentId).duplicateWindowMs : (24 * 60 * 60 * 1000);
    };
    deps.heartbeatLastDeliveredAt = [ensureRuntimeState](const QString& agentId) {
        return ensureRuntimeState(agentId).lastDeliveredAtUtc;
    };
    deps.heartbeatLastDeliveredDigest = [ensureRuntimeState](const QString& agentId) {
        return ensureRuntimeState(agentId).lastDeliveredDigest;
    };
    deps.recordHeartbeatSuppressed = [ensureRuntimeState, stateStore](const QString& agentId,
                                                                      const QString& digest,
                                                                      const QString& reason,
                                                                      const QDateTime& nowUtc) mutable {
        HeartbeatRuntimeState& runtimeState = ensureRuntimeState(agentId);
        runtimeState.stateObj.insert(QStringLiteral("last_duplicate_suppressed_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
        runtimeState.stateObj.insert(QStringLiteral("last_duplicate_digest"), digest);
        runtimeState.stateObj.insert(QStringLiteral("last_duplicate_reason"), reason);
        stateStore.persist(agentId, &runtimeState, nowUtc, true);
    };
    deps.recordHeartbeatDelivered = [ensureRuntimeState, stateStore](const QString& agentId,
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
    deps.recordHeartbeatManualSuppress = [ensureRuntimeState, stateStore](const QString& agentId,
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
    const ConversationContextService contextService(
        ConversationContextService::Dependencies {
            [this](const QString& sid, const ConversationContext::TaskContextSnapshot& snapshot) {
                return m_service.m_persistence
                    && m_service.m_persistence->saveTaskContextSnapshot(sid, snapshot);
            },
            [this](const QString& sid,
                   const ConversationContext::ContextCompressionCheckpoint& checkpoint) {
                return m_service.m_persistence
                    && m_service.m_persistence->saveContextCompressionCheckpoint(sid, checkpoint);
            },
            [this](const QString& sid, const ConversationContext::ResumePacket& packet) {
                return m_service.m_persistence
                    && m_service.m_persistence->saveResumePacket(sid, packet);
            },
            [this](const QString& sid, bool* ok) {
                return m_service.m_persistence
                    ? m_service.m_persistence->loadTaskContextSnapshot(sid, ok)
                    : ConversationContext::TaskContextSnapshot();
            },
            ctx.emitPipelineEvent
        });
    deps.persistCompletionContext =
        [contextService](const QString& sid,
                         const TurnTask& finishedTurn,
                         const QJsonObject& existingTaskState,
                         const QDateTime& nowUtc) {
            contextService.persistCompletionArtifacts(
                sid, finishedTurn, existingTaskState, nowUtc);
        };
    deps.emitFinished = [this](const QString& sid, const QString& content) {
        emit m_service.finished(sid, content);
    };

    // Error 专有
    deps.isTransientUpstreamError = [](const QString& err) {
        return isTransientUpstreamError(err);
    };
    deps.listActiveJobs = [](const QString& agentId, int limit) {
        QList<TurnCompletionCoordinator::ActiveJobInfo> jobs;
        const QList<DelegateTaskScheduler::JobInfo> rawJobs =
            DelegateTaskScheduler::instance()->listJobs(agentId, true, limit);
        for (const DelegateTaskScheduler::JobInfo& job : rawJobs) {
            TurnCompletionCoordinator::ActiveJobInfo info;
            info.jobId = job.jobId;
            info.status = job.status;
            info.summary = job.summary;
            jobs.append(info);
        }
        return jobs;
    };
    deps.buildDelegateRecoveryReply = [](const QList<TurnCompletionCoordinator::ActiveJobInfo>& jobs) {
        QList<DelegateTaskScheduler::JobInfo> rawJobs;
        for (const TurnCompletionCoordinator::ActiveJobInfo& job : jobs) {
            DelegateTaskScheduler::JobInfo info;
            info.jobId = job.jobId;
            info.status = job.status;
            info.summary = job.summary;
            rawJobs.append(info);
        }
        return buildDelegateRecoveryReply(rawJobs);
    };
    deps.emitError = [this](const QString& sid, const QString& error) {
        emit m_service.errorOccurred(sid, error);
    };

    // Memory 专有
    deps.reflectionEnabled = [this]() {
        return m_service.m_memoryManager && m_service.m_memoryManager->reflectionEnabled();
    };
    deps.retainTurn = [this](const QString& aid,
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
    const MemoryMaintenanceService memoryMaintenance = m_service.makeMemoryMaintenanceService();
    deps.refreshMemoryIndexAndEmit = [memoryMaintenance](const QString& sid,
                                                         const QString& aid,
                                                         const TurnTask* turn,
                                                         const QString& reason,
                                                         const QString& sourcePath,
                                                         const QJsonObject& sourceMetadata) {
        memoryMaintenance.refreshIndexAndEmit(
            sid, aid, turn, reason, sourcePath, sourceMetadata);
    };
    deps.maybeReflectMemoryAndEmit = [memoryMaintenance](const QString& sid,
                                                         const QString& aid,
                                                         const TurnTask& turn,
                                                         bool forceReflection,
                                                         const QString& triggerReason) {
        memoryMaintenance.maybeReflectAndEmit(
            sid, aid, turn, forceReflection, triggerReason);
    };

    return deps;
}

// ── ToolEvent (合并 ToolEvent+ToolPersistence) ──

ToolEventCoordinator::Dependencies ChatCoordinatorFactory::makeToolEventDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    ToolEventCoordinator::Dependencies deps;
    deps.ctx = ctx;

    // 原 ToolEvent 专有
    deps.suppressHeartbeat = [this](const QString& id, const QString& reason) {
        if (m_service.m_heartbeatService && !id.isEmpty())
            m_service.m_heartbeatService->suppressHeartbeat(id, reason);
    };
    deps.unsuppressHeartbeat = [this](const QString& id) {
        if (m_service.m_heartbeatService && !id.isEmpty())
            m_service.m_heartbeatService->unsuppressHeartbeat(id);
    };
    deps.taskStateForSession = [this](const QString& sid) {
        return m_service.taskStateForSession(sid);
    };
    deps.takeDelegateStartMs = [this](const QString& sid, const QString& id) -> qint64 {
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
    deps.putDelegateStartMs = [this](const QString& sid, const QString& id, qint64 startedAtMs) {
        if (!id.isEmpty())
            m_service.m_delegateStartMsByToolKey.insert(delegateToolKey(sid, id), startedAtMs);
    };
    deps.delegateStatsForSession = [this](const QString& sid) {
        ToolEventCoordinator::DelegateStats stats;
        const ChatService::DelegateStats existing = m_service.m_delegateStatsBySession.value(sid);
        stats.totalCount = existing.totalCount;
        stats.successCount = existing.successCount;
        stats.failureCount = existing.failureCount;
        stats.totalDurationMs = existing.totalDurationMs;
        return stats;
    };
    deps.setDelegateStatsForSession = [this](const QString& sid,
                                             const ToolEventCoordinator::DelegateStats& stats) {
        ChatService::DelegateStats out;
        out.totalCount = stats.totalCount;
        out.successCount = stats.successCount;
        out.failureCount = stats.failureCount;
        out.totalDurationMs = stats.totalDurationMs;
        m_service.m_delegateStatsBySession.insert(sid, out);
    };

    // 原 ToolPersistence 专有
    deps.sessionDataDirPath = [this](const QString& sid) {
        return m_service.m_persistence ? m_service.m_persistence->sessionDataDirPath(sid) : QString();
    };
    deps.sanitizePersistedToolArguments = [](const QString& name, const QJsonObject& args) {
        return sanitizePersistedToolArguments(name, args);
    };
    deps.sanitizePersistedToolEventData = [](const QString& name, const QJsonObject& data) {
        return sanitizePersistedToolEventData(name, data);
    };
    deps.sanitizePersistedToolRawResult = [](const QString& name, const QString& raw) {
        return sanitizePersistedToolRawResult(name, raw);
    };
    deps.toolEventToJson = [](const ToolExecutionEvent& toolEvent) {
        return toolEventToJson(toolEvent);
    };
    deps.emitToolEvent = [this](const QString& sid, const ToolExecutionEvent& toolExecutionEvent) {
        emit m_service.toolEvent(sid, toolExecutionEvent);
    };
    deps.toolProgressLastPersistMs = [this](const QString& key) {
        return m_service.m_toolProgressLastPersistMsByKey.value(key, 0);
    };
    deps.toolProgressLastDigest = [this](const QString& key) {
        return m_service.m_toolProgressLastDigestByKey.value(key);
    };
    deps.setToolProgressLastPersistMs = [this](const QString& key, qint64 value) {
        m_service.m_toolProgressLastPersistMsByKey.insert(key, value);
    };
    deps.setToolProgressLastDigest = [this](const QString& key, const QString& digest) {
        m_service.m_toolProgressLastDigestByKey.insert(key, digest);
    };
    deps.toolProgressPersistMinIntervalMs = ChatService::kToolProgressPersistMinIntervalMs;

    return deps;
}

// ── BackgroundTask (合并 SchedulerTrigger+DelegateSettlement) ──

BackgroundTaskCoordinator::Dependencies ChatCoordinatorFactory::makeBackgroundTaskDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    const PrimarySessionResolver resolver = makePrimarySessionResolver();
    BackgroundTaskCoordinator::Dependencies deps;
    deps.ctx = ctx;

    // Scheduler 专有
    deps.jobById = [this](const QString& id, ScheduledJob* outJob) {
        return m_service.m_schedulerService && m_service.m_schedulerService->jobById(id, outJob);
    };
    deps.findIdentity = [this](const QString& identityId) {
        return m_service.m_identityManager ? m_service.m_identityManager->findById(identityId) : nullptr;
    };
    deps.userIdentityId = [this]() {
        return m_service.m_identityManager && m_service.m_identityManager->userIdentity()
            ? m_service.m_identityManager->userIdentity()->id()
            : QString();
    };
    deps.buildClientMessageId = [](const QString& jobId,
                                   const QString& uuid,
                                   const QString&,
                                   const QString&) {
        return QStringLiteral("scheduler-%1-%2").arg(jobId, uuid);
    };
    deps.buildPrompt = [](const QString& jobNameArg,
                          const QString& storedJobName,
                          const QString& prompt,
                          const QString&) {
        const QString displayName = jobNameArg.trimmed().isEmpty()
            ? (storedJobName.trimmed().isEmpty() ? QStringLiteral("scheduled-job") : storedJobName.trimmed())
            : jobNameArg.trimmed();
        return QStringLiteral("【定时任务:%1】\n%2").arg(displayName, prompt);
    };
    deps.enqueueUserMessageAs = [this](const QString& actorId,
                                       const QString& sessionId,
                                       const QString& prompt,
                                       const QString& clientMessageId) {
        return m_service.enqueueUserMessageAs(actorId, sessionId, prompt, clientMessageId);
    };

    // DelegateSettlement 专有
    deps.triggerHeartbeat = [this](const QString& agentId, const QString& reason) {
        if (m_service.m_heartbeatService)
            m_service.m_heartbeatService->triggerHeartbeat(agentId, reason);
    };
    deps.updateTaskStateForSession = [this](const QString& sid,
                                            const QString& state,
                                            const void*,
                                            const QJsonObject& extra) {
        m_service.updateTaskStateForSession(sid, state, nullptr, extra);
    };

    // 共享
    deps.resolvePrimarySessionForAgent = [resolver](const QString& agentId,
                                                    bool createIfMissing,
                                                    bool isolated,
                                                    const QString& titleSuffix) {
        return resolver.resolveForAgent(agentId, createIfMissing, isolated, titleSuffix);
    };
    deps.emitPipelineEventSimple = [this](const QString& sid,
                                          const QString& type,
                                          const QString& error,
                                          const QString& delta,
                                          const QJsonObject& extra,
                                          bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, nullptr, delta, error, extra, persistToDisk);
    };

    return deps;
}

// ── HeartbeatDispatch ──

HeartbeatDispatchCoordinator::Dependencies ChatCoordinatorFactory::makeHeartbeatDispatchDependencies(
    HeartbeatRuntimeState& runtimeState,
    bool& shouldPersistState,
    const QDateTime& nowUtc)
{
    const HeartbeatPromptBuilder promptBuilder = makeHeartbeatPromptBuilder();
    const HeartbeatStateStore stateStore = makeHeartbeatStateStore();
    HeartbeatDispatchCoordinator::Dependencies deps;
    deps.queueDepthForSession = [this](const QString& sid) {
        return m_service.m_turnManager.totalDepth(sid);
    };
    deps.emitPipelineEventSimple = [this](const QString& sid,
                                          const QString& type,
                                          const QString& error,
                                          const QString& delta,
                                          const QJsonObject& extra,
                                          bool persistToDisk) {
        m_service.emitPipelineEvent(type, sid, nullptr, delta, error, extra, persistToDisk);
    };
    deps.userIdentityId = [this]() {
        return m_service.m_identityManager && m_service.m_identityManager->userIdentity()
            ? m_service.m_identityManager->userIdentity()->id()
            : QString();
    };
    deps.buildHeartbeatPrompt = [promptBuilder](const QString& aid, const QString& reasonText) {
        return promptBuilder.build(aid, reasonText);
    };
    deps.pulseForAgent = [this](const QString& aid) {
        return m_service.m_agentPulseRegistry ? m_service.m_agentPulseRegistry->find(aid) : nullptr;
    };
    deps.pulseStateText = [](AgentPulse* pulse) {
        return pulse ? pulseStateToString(pulse->currentState()) : QString();
    };
    deps.enqueueUserMessageAs = [this](const QString& actorId,
                                       const QString& sid,
                                       const QString& prompt,
                                       const QString& clientMessageId) {
        return m_service.enqueueUserMessageAs(actorId, sid, prompt, clientMessageId);
    };
    deps.buildHeartbeatClientMessageId = [](const QString& tag, const QString& uuid) {
        return QStringLiteral("%1-%2").arg(tag, uuid);
    };
    deps.markHeartbeatNotified = [&](const QString&, const QString&) {
        runtimeState.lastNotifyAtUtc = nowUtc;
        runtimeState.stateObj.insert(QStringLiteral("last_notify_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
        shouldPersistState = true;
    };
    deps.persistStateIfNeeded = [stateStore, &runtimeState, &shouldPersistState, nowUtc](bool forcePersist) mutable {
        const bool doPersist = forcePersist || shouldPersistState;
        stateStore.persist(
            runtimeState.stateStorageKey.mid(QStringLiteral("heartbeat_state:").size()),
            &runtimeState,
            nowUtc,
            doPersist);
    };
    return deps;
}
