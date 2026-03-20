#include "ChatCoordinatorFactory.h"

#include "AgentPulse.h"
#include "AgentPulseRegistry.h"
#include "AgentRuntime.h"
#include "ConversationContextService.h"
#include "MemoryMaintenanceService.h"
#include "ChatCoordinatorSupport.h"
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
#include <utility>

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

ChatCoordinatorFactory::ChatCoordinatorFactory(ConversationCoreDeps deps)
    : m_deps(std::move(deps))
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
        m_deps.emitPipelineEvent(type, sid, turn, delta, error, extra, persist);
    };
    ctx.updateTaskState = [this](const QString& sid,
                                 const QString& state,
                                 const TurnTask* turn,
                                 const QJsonObject& extra) {
        m_deps.updateTaskStateForSession(sid, state, turn, extra);
    };
    ctx.taskStateTextPreview = [](const QString& text, int maxChars) {
        return taskStateTextPreview(text, maxChars);
    };
    ctx.agentIdentityIdForSession = [this](const QString& sid) {
        return m_deps.agentIdentityIdForSession(sid);
    };
    ctx.reportPulseProgress = [this](const QString& agentId, const QString& summary) {
        m_deps.reportPulseProgress(agentId, summary);
    };
    ctx.postMessage = [this](const QString& sid, const Message& message) {
        m_deps.postMessage(sid, message);
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
    dependencies.identityManager = m_deps.identityManager;
    dependencies.sessionManager = m_deps.sessionManager;
    dependencies.userIdentityId = m_deps.userIdentityId;
    dependencies.createSessionForIdentityAs = [this](const QString& actorIdentityId,
                                                     const QString& identityId,
                                                     const QString& title) {
        return m_deps.createSessionForIdentityAs(actorIdentityId, identityId, title);
    };
    return PrimarySessionResolver(dependencies);
}

HeartbeatPromptBuilder ChatCoordinatorFactory::makeHeartbeatPromptBuilder() const
{
    HeartbeatPromptBuilder::Dependencies dependencies;
    dependencies.heartbeatPathForAgent = [this](const QString& agentId) {
        return m_deps.heartbeatService ? m_deps.heartbeatService->heartbeatPathForAgent(agentId) : QString();
    };
    return HeartbeatPromptBuilder(dependencies);
}

HeartbeatStateStore ChatCoordinatorFactory::makeHeartbeatStateStore() const
{
    HeartbeatStateStore::Dependencies dependencies;
    dependencies.loadAppState = [this](const QString& key) {
        return m_deps.persistence ? m_deps.persistence->getAppState(key) : QString();
    };
    dependencies.saveAppState = [this](const QString& key, const QString& value) {
        return m_deps.persistence ? m_deps.persistence->setAppState(key, value) : false;
    };
    dependencies.readJsonObject = [this](const QString& path) {
        return m_deps.persistence ? m_deps.persistence->readJsonObject(path) : QJsonObject();
    };
    dependencies.agentsDirPath = [this]() {
        return m_deps.persistence ? m_deps.persistence->agentsDirPath() : QString();
    };
    dependencies.databaseReady = []() { return DatabaseManager::instance()->isReady(); };
    return HeartbeatStateStore(dependencies);
}

// ── Enqueue ──

ConversationEnqueueCoordinator::Dependencies ChatCoordinatorFactory::makeEnqueueDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    ConversationEnqueueCoordinator::Dependencies deps;
    deps.identityManager = m_deps.identityManager;
    deps.sessionManager = m_deps.sessionManager;
    deps.turnManager = m_deps.turnManager;
    deps.canIdentitySendMessage = [this](const QString& identityId, const QString& sid) {
        return m_deps.canIdentitySendMessage(identityId, sid);
    };
    deps.emitPipelineEvent = ctx.emitPipelineEvent;
    deps.updateTaskStateForSession = ctx.updateTaskState;
    deps.tryStartNextTurn = m_deps.tryStartNextTurn;
    deps.isBackgroundClientMessage = ctx.isBackgroundClientMessage;
    deps.taskStateTextPreview = ctx.taskStateTextPreview;
    return deps;
}

ConversationEnqueueCoordinator::Limits ChatCoordinatorFactory::makeEnqueueLimits() const
{
    ConversationEnqueueCoordinator::Limits limits;
    limits.softQueueDepth = m_deps.softQueueDepth;
    limits.hardQueueDepth = m_deps.hardQueueDepth;
    limits.queueMergeWindowMs = m_deps.queueMergeWindowMs;
    limits.queueMergeMaxMergedMessages = m_deps.queueMergeMaxMergedMessages;
    limits.queueMergeMaxChars = m_deps.queueMergeMaxChars;
    return limits;
}

// ── Dispatch ──

ConversationDispatchCoordinator::Dependencies ChatCoordinatorFactory::makeDispatchDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    ConversationDispatchCoordinator::Dependencies deps;
    deps.turnManager = m_deps.turnManager;
    deps.findPipeline = m_deps.findPipeline;
    deps.ensureRuntimeIdentityForSession = [this](const QString& sid, QString* agentIdOut) -> Identity* {
        AgentRuntime* runtime = m_deps.ensureRuntimeForSession(sid);
        if (!runtime)
            return nullptr;
        if (agentIdOut)
            *agentIdOut = runtime->identityId().trimmed();
        return runtime->identity();
    };
    deps.findSession = [this](const QString& sid) {
        return m_deps.sessionManager ? m_deps.sessionManager->findById(sid) : nullptr;
    };
    deps.activeSessionForAgent = [this](const QString& agentId) {
        return m_deps.activeSessionForAgent(agentId);
    };
    deps.setActiveSessionForAgent = [this](const QString& agentId, const QString& sid) {
        m_deps.setActiveSessionForAgent(agentId, sid);
    };
    deps.buildRuntimeHistoryFromMessages = [this](Session* session) {
        return m_deps.buildRuntimeHistoryFromMessages(session);
    };
    deps.estimateHistoryChars = [](const QJsonArray& history) {
        return static_cast<qint64>(estimateHistoryChars(history));
    };
    deps.setRuntimeHistory = [this](const QString& sid, const QJsonArray& history) {
        AgentRuntime* runtime = m_deps.runtimeForSession(sid);
        if (runtime)
            runtime->setHistory(history);
    };
    deps.setRuntimeConfig = [this](const QString& sid, const LLMConfig& config) {
        AgentRuntime* runtime = m_deps.runtimeForSession(sid);
        if (runtime)
            runtime->setConfig(config);
    };
    deps.setRuntimeIoContext = [this](const QString& sid, const QJsonObject& context) {
        AgentRuntime* runtime = m_deps.runtimeForSession(sid);
        if (runtime)
            runtime->setIoContext(context);
    };
    deps.sendRuntimeMessage = [this](const QString& sid, const QString& prompt) {
        AgentRuntime* runtime = m_deps.runtimeForSession(sid);
        if (runtime)
            runtime->sendMessage(sid, prompt);
    };
    deps.emitPipelineEvent = ctx.emitPipelineEvent;
    deps.updateTaskStateForSession = ctx.updateTaskState;
    deps.taskStateTextPreview = ctx.taskStateTextPreview;
    deps.reportPulseProgress = ctx.reportPulseProgress;
    deps.ensureMemoryInitializedForAgent = [this](Identity* identity) {
        m_deps.ensureMemoryInitializedForAgent(identity);
    };
    deps.composeConfigForIdentity = [this](Identity* identity) {
        return m_deps.composeConfigForIdentity(identity);
    };
    deps.composeMemoryContext = [this](const QString& agentId, int maxChars) {
        return m_deps.memoryManager ? m_deps.memoryManager->composeMemoryContext(agentId, maxChars) : QString();
    };
    deps.delegateContextForAgent = [](const QString& agentId) {
        return DelegateTaskScheduler::instance()->formatActiveJobsContext(agentId);
    };
    deps.loadTaskContextSnapshot = [this](const QString& sid, bool* ok) {
        return m_deps.persistence ? m_deps.persistence->loadTaskContextSnapshot(sid, ok)
                                       : ConversationContext::TaskContextSnapshot();
    };
    deps.loadContextCompressionCheckpoint = [this](const QString& sid, bool* ok) {
        return m_deps.persistence ? m_deps.persistence->loadContextCompressionCheckpoint(sid, ok)
                                       : ConversationContext::ContextCompressionCheckpoint();
    };
    deps.drainTeammateInjections = [this](const QString& sid) -> QStringList {
        return m_deps.drainTeammateInjections(sid);
    };
    return deps;
}

ConversationDispatchCoordinator::Limits ChatCoordinatorFactory::makeDispatchLimits() const
{
    ConversationDispatchCoordinator::Limits limits;
    limits.memoryContextMaxChars = m_deps.memoryContextMaxChars;
    return limits;
}

// ── Stream ──

ConversationStreamCoordinator::Dependencies ChatCoordinatorFactory::makeStreamDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    ConversationStreamCoordinator::Dependencies deps;
    deps.findPipeline = m_deps.findPipeline;
    deps.activeTurn = [this](const QString& sid) {
        return m_deps.turnManager ? m_deps.turnManager->activeTurn(sid) : nullptr;
    };
    deps.agentIdentityIdForSession = ctx.agentIdentityIdForSession;
    deps.reportPulseProgress = ctx.reportPulseProgress;
    deps.isBackgroundClientMessage = ctx.isBackgroundClientMessage;
    deps.findSession = [this](const QString& sid) {
        return m_deps.sessionManager ? m_deps.sessionManager->findById(sid) : nullptr;
    };
    deps.emitStreamData = [this](const QString& sid, const QString& chunk) {
        m_deps.emitStreamData(sid, chunk);
    };
    deps.emitPipelineEvent = ctx.emitPipelineEvent;
    deps.flushPendingDeltaLog = [this](const QString& sid, SessionPipeline* pipeline, const TurnTask* turn, bool force) {
        m_deps.flushPendingDeltaLog(sid, pipeline, turn, force);
    };
    deps.logVerboseStreamEvents = m_deps.logVerboseStreamEvents;
    return deps;
}

// ── TurnCompletion (合并 Finish+Finalize+Error+MemoryFinish) ──

TurnCompletionCoordinator::Dependencies ChatCoordinatorFactory::makeTurnCompletionDependencies()
{
    const CoordinatorContext ctx = makeSharedContext();
    const HeartbeatStateStore stateStore = makeHeartbeatStateStore();
    auto ensureRuntimeState = [this, stateStore](const QString& agentId) -> HeartbeatRuntimeState& {
        const QString trimmedAgentId = agentId.trimmed();
        HeartbeatRuntimeState& runtimeState = m_deps.heartbeatRuntimeStateForAgent(trimmedAgentId);
        if (!runtimeState.loaded)
            stateStore.load(trimmedAgentId, &runtimeState);
        return runtimeState;
    };

    TurnCompletionCoordinator::Dependencies deps;
    deps.ctx = ctx;

    // Finalize 专有
    deps.findPipeline = m_deps.findPipeline;
    deps.activeTurn = [this](const QString& sid) {
        return m_deps.turnManager ? m_deps.turnManager->activeTurn(sid) : nullptr;
    };
    deps.flushPendingDeltaLog = [this](const QString& sid, SessionPipeline* pipeline, const TurnTask* turn, bool force) {
        m_deps.flushPendingDeltaLog(sid, pipeline, turn, force);
    };
    deps.turnManager = m_deps.turnManager;
    deps.clearDelegateStartsForSession = m_deps.clearDelegateStartsForSession;
    deps.clearToolProgressCacheForSession = m_deps.clearToolProgressCacheForSession;
    deps.activeSessionForAgent = m_deps.activeSessionForAgent;
    deps.clearActiveSessionForAgent = m_deps.clearActiveSessionForAgent;
    deps.resetSessionStreamState = m_deps.resetSessionStreamState;
    deps.tryStartNextTurn = m_deps.tryStartNextTurn;
    deps.tryStartNextTurnForAgent = m_deps.tryStartNextTurnForAgent;

    // Finish 专有
    deps.isManualHeartbeatClientMessage = [](const QString& clientMessageId) {
        return isManualHeartbeatClientMessageId(clientMessageId);
    };
    deps.taskStateForSession = m_deps.taskStateForSession;
    deps.heartbeatDuplicateWindowMs = [this](const QString& agentId) {
        return m_deps.heartbeatService
            ? m_deps.heartbeatService->configForAgent(agentId).duplicateWindowMs
            : (24 * 60 * 60 * 1000);
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
                return m_deps.persistence
                    && m_deps.persistence->saveTaskContextSnapshot(sid, snapshot);
            },
            [this](const QString& sid,
                   const ConversationContext::ContextCompressionCheckpoint& checkpoint) {
                return m_deps.persistence
                    && m_deps.persistence->saveContextCompressionCheckpoint(sid, checkpoint);
            },
            [this](const QString& sid, const ConversationContext::ResumePacket& packet) {
                return m_deps.persistence
                    && m_deps.persistence->saveResumePacket(sid, packet);
            },
            [this](const QString& sid, bool* ok) {
                return m_deps.persistence
                    ? m_deps.persistence->loadTaskContextSnapshot(sid, ok)
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
    deps.emitFinished = m_deps.emitFinished;

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
    deps.emitError = m_deps.emitError;

    // Memory 专有
    deps.reflectionEnabled = [this]() {
        return m_deps.memoryManager && m_deps.memoryManager->reflectionEnabled();
    };
    deps.retainTurn = [this](const QString& aid,
                             const QString& sid,
                             const TurnTask& turn,
                             QString* summary,
                             QString* path,
                             QJsonObject* metadata,
                             QString* error) {
        return m_deps.memoryManager
            ? m_deps.memoryManager->retainTurn(aid, sid, turn, summary, path, metadata, error)
            : false;
    };
    deps.refreshMemoryIndexAndEmit = m_deps.refreshMemoryIndexAndEmit;
    deps.maybeReflectMemoryAndEmit = m_deps.maybeReflectMemoryAndEmit;

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
        if (m_deps.heartbeatService && !id.isEmpty())
            m_deps.heartbeatService->suppressHeartbeat(id, reason);
    };
    deps.unsuppressHeartbeat = [this](const QString& id) {
        if (m_deps.heartbeatService && !id.isEmpty())
            m_deps.heartbeatService->unsuppressHeartbeat(id);
    };
    deps.taskStateForSession = m_deps.taskStateForSession;
    deps.takeDelegateStartMs = m_deps.takeDelegateStartMs;
    deps.putDelegateStartMs = m_deps.putDelegateStartMs;
    deps.delegateStatsForSession = m_deps.delegateStatsForSession;
    deps.setDelegateStatsForSession = m_deps.setDelegateStatsForSession;

    // 原 ToolPersistence 专有
    deps.sessionDataDirPath = [this](const QString& sid) {
        return m_deps.persistence ? m_deps.persistence->sessionDataDirPath(sid) : QString();
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
    deps.emitToolEvent = m_deps.emitToolEvent;
    deps.toolProgressLastPersistMs = m_deps.toolProgressLastPersistMs;
    deps.toolProgressLastDigest = m_deps.toolProgressLastDigest;
    deps.setToolProgressLastPersistMs = m_deps.setToolProgressLastPersistMs;
    deps.setToolProgressLastDigest = m_deps.setToolProgressLastDigest;
    deps.toolProgressPersistMinIntervalMs = m_deps.toolProgressPersistMinIntervalMs;

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
        return m_deps.schedulerService && m_deps.schedulerService->jobById(id, outJob);
    };
    deps.findIdentity = [this](const QString& identityId) {
        return m_deps.identityManager ? m_deps.identityManager->findById(identityId) : nullptr;
    };
    deps.userIdentityId = m_deps.userIdentityId;
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
        return m_deps.enqueueUserMessageAs(actorId, sessionId, prompt, clientMessageId);
    };

    // DelegateSettlement 专有
    deps.triggerHeartbeat = [this](const QString& agentId, const QString& reason) {
        if (m_deps.heartbeatService)
            m_deps.heartbeatService->triggerHeartbeat(agentId, reason);
    };
    deps.updateTaskStateForSession = [this](const QString& sid,
                                            const QString& state,
                                            const void*,
                                            const QJsonObject& extra) {
        m_deps.updateTaskStateForSession(sid, state, nullptr, extra);
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
        m_deps.emitPipelineEvent(type, sid, nullptr, delta, error, extra, persistToDisk);
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
        return m_deps.turnManager ? m_deps.turnManager->totalDepth(sid) : 0;
    };
    deps.emitPipelineEventSimple = [this](const QString& sid,
                                          const QString& type,
                                          const QString& error,
                                          const QString& delta,
                                          const QJsonObject& extra,
                                          bool persistToDisk) {
        m_deps.emitPipelineEvent(type, sid, nullptr, delta, error, extra, persistToDisk);
    };
    deps.userIdentityId = m_deps.userIdentityId;
    deps.buildHeartbeatPrompt = [promptBuilder](const QString& aid, const QString& reasonText) {
        return promptBuilder.build(aid, reasonText);
    };
    deps.pulseForAgent = [this](const QString& aid) {
        return m_deps.pulseForAgent(aid);
    };
    deps.pulseStateText = [](AgentPulse* pulse) {
        return pulse ? pulseStateToString(pulse->currentState()) : QString();
    };
    deps.enqueueUserMessageAs = [this](const QString& actorId,
                                       const QString& sid,
                                       const QString& prompt,
                                       const QString& clientMessageId) {
        return m_deps.enqueueUserMessageAs(actorId, sid, prompt, clientMessageId);
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
