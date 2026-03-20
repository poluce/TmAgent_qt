#ifndef CHATCOORDINATORFACTORY_H
#define CHATCOORDINATORFACTORY_H

#include "HeartbeatRuntimeState.h"
#include "CoordinatorContext.h"
#include "PrimarySessionResolver.h"
#include "HeartbeatPromptBuilder.h"
#include "HeartbeatStateStore.h"
#include "ConversationDispatchCoordinator.h"
#include "ConversationEnqueueCoordinator.h"
#include "ConversationStreamCoordinator.h"
#include "TurnCompletionCoordinator.h"
#include "ToolEventCoordinator.h"
#include "BackgroundTaskCoordinator.h"
#include "HeartbeatDispatchCoordinator.h"

struct Message;
class IdentityManager;
class SessionManager;
class ChatPersistenceService;
class MemoryManager;
class HeartbeatService;
class SchedulerService;
class AgentPulseRegistry;
class AgentRuntime;
class Identity;
class Session;
class AgentPulse;

struct ConversationCoreDeps {
    IdentityManager* identityManager = nullptr;
    SessionManager* sessionManager = nullptr;
    ChatPersistenceService* persistence = nullptr;
    MemoryManager* memoryManager = nullptr;
    HeartbeatService* heartbeatService = nullptr;
    SchedulerService* schedulerService = nullptr;
    AgentPulseRegistry* agentPulseRegistry = nullptr;
    TurnManager* turnManager = nullptr;

    bool logVerboseStreamEvents = false;

    int softQueueDepth = 10;
    int hardQueueDepth = 200;
    int queueMergeWindowMs = 2500;
    int queueMergeMaxMergedMessages = 4;
    int queueMergeMaxChars = 12000;
    int memoryContextMaxChars = 4500;
    qint64 toolProgressPersistMinIntervalMs = 1200;

    std::function<void(const QString& type,
                       const QString& sessionId,
                       const TurnTask* turn,
                       const QString& delta,
                       const QString& error,
                       const QJsonObject& extra,
                       bool persistToDisk)>
        emitPipelineEvent;
    std::function<void(const QString& sessionId,
                       const QString& state,
                       const TurnTask* turn,
                       const QJsonObject& extra)>
        updateTaskStateForSession;
    std::function<QString(const QString& sessionId)> agentIdentityIdForSession;
    std::function<void(const QString& agentId, const QString& summary)> reportPulseProgress;
    std::function<void(const QString& sessionId, const Message& message)> postMessage;
    std::function<QString()> userIdentityId;
    std::function<Session*(const QString& actorIdentityId,
                           const QString& identityId,
                           const QString& title)>
        createSessionForIdentityAs;
    std::function<bool(const QString& identityId, const QString& sessionId)> canIdentitySendMessage;
    std::function<void(const QString& sessionId)> tryStartNextTurn;
    std::function<void(const QString& agentIdentityId)> tryStartNextTurnForAgent;
    std::function<SessionPipeline*(const QString& sessionId)> findPipeline;
    std::function<AgentRuntime*(const QString& sessionId)> ensureRuntimeForSession;
    std::function<AgentRuntime*(const QString& sessionId)> runtimeForSession;
    std::function<QJsonArray(Session* session)> buildRuntimeHistoryFromMessages;
    std::function<void(Identity* identity)> ensureMemoryInitializedForAgent;
    std::function<LLMConfig(Identity* identity)> composeConfigForIdentity;
    std::function<QStringList(const QString& sessionId)> drainTeammateInjections;
    std::function<void(const QString& sessionId,
                       SessionPipeline* pipeline,
                       const TurnTask* turn,
                       bool force)>
        flushPendingDeltaLog;
    std::function<HeartbeatRuntimeState&(const QString& agentId)> heartbeatRuntimeStateForAgent;
    std::function<void(const QString& sessionId)> clearDelegateStartsForSession;
    std::function<void(const QString& sessionId)> clearToolProgressCacheForSession;
    std::function<QString(const QString& agentId)> activeSessionForAgent;
    std::function<void(const QString& agentId, const QString& sessionId)> setActiveSessionForAgent;
    std::function<void(const QString& agentId)> clearActiveSessionForAgent;
    std::function<void(const QString& sessionId)> resetSessionStreamState;
    std::function<qint64(const QString& sessionId, const QString& toolId)> takeDelegateStartMs;
    std::function<void(const QString& sessionId, const QString& toolId, qint64 startedAtMs)>
        putDelegateStartMs;
    std::function<ToolEventCoordinator::DelegateStats(const QString& sessionId)> delegateStatsForSession;
    std::function<void(const QString& sessionId, const ToolEventCoordinator::DelegateStats& stats)>
        setDelegateStatsForSession;
    std::function<void(const QString& sessionId, const QString& chunk)> emitStreamData;
    std::function<void(const QString& sessionId, const QString& content)> emitFinished;
    std::function<void(const QString& sessionId, const QString& error)> emitError;
    std::function<void(const QString& sessionId, const ToolExecutionEvent& event)> emitToolEvent;
    std::function<qint64(const QString& key)> toolProgressLastPersistMs;
    std::function<QString(const QString& key)> toolProgressLastDigest;
    std::function<void(const QString& key, qint64 value)> setToolProgressLastPersistMs;
    std::function<void(const QString& key, const QString& digest)> setToolProgressLastDigest;
    std::function<void(const QString& sessionId,
                       const QString& agentId,
                       const TurnTask* turn,
                       const QString& reason,
                       const QString& sourcePath,
                       const QJsonObject& sourceMetadata)>
        refreshMemoryIndexAndEmit;
    std::function<void(const QString& sessionId,
                       const QString& agentId,
                       const TurnTask& turn,
                       bool forceReflection,
                       const QString& triggerReason)>
        maybeReflectMemoryAndEmit;
    std::function<QJsonObject(const QString& sessionId)> taskStateForSession;
    std::function<QString(const QString& actorIdentityId,
                          const QString& sessionId,
                          const QString& prompt,
                          const QString& clientMessageId)>
        enqueueUserMessageAs;
    std::function<AgentPulse*(const QString& agentId)> pulseForAgent;
};

class ChatCoordinatorFactory {
public:
    explicit ChatCoordinatorFactory(ConversationCoreDeps deps);

    CoordinatorContext makeSharedContext();
    PrimarySessionResolver makePrimarySessionResolver() const;
    HeartbeatPromptBuilder makeHeartbeatPromptBuilder() const;
    HeartbeatStateStore makeHeartbeatStateStore() const;

    ConversationEnqueueCoordinator::Dependencies makeEnqueueDependencies();
    ConversationEnqueueCoordinator::Limits makeEnqueueLimits() const;

    ConversationDispatchCoordinator::Dependencies makeDispatchDependencies();
    ConversationDispatchCoordinator::Limits makeDispatchLimits() const;

    ConversationStreamCoordinator::Dependencies makeStreamDependencies();
    TurnCompletionCoordinator::Dependencies makeTurnCompletionDependencies();
    ToolEventCoordinator::Dependencies makeToolEventDependencies();
    BackgroundTaskCoordinator::Dependencies makeBackgroundTaskDependencies();

    HeartbeatDispatchCoordinator::Dependencies makeHeartbeatDispatchDependencies(
        HeartbeatRuntimeState& runtimeState,
        bool& shouldPersistState,
        const QDateTime& nowUtc);

private:
    ConversationCoreDeps m_deps;
};

#endif // CHATCOORDINATORFACTORY_H
