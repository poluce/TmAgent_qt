#ifndef TURNCOMPLETIONCOORDINATOR_H
#define TURNCOMPLETIONCOORDINATOR_H

#include "core/model/Message.h"
#include "core/service/include/ConversationContextTypes.h"
#include "core/service/include/CoordinatorContext.h"
#include "TurnManager.h"
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

class TurnCompletionCoordinator {
public:
    struct Result {
        bool valid = false;
        TurnTask finishedTurn;
        QString agentId;
        bool skipMemoryForHeartbeat = false;
        bool heartbeatTurn = false;
    };

    struct ActiveJobInfo {
        QString jobId;
        QString status;
        QString summary;
    };

    struct Dependencies {
        CoordinatorContext ctx;

        // ── Finalize 专有字段 ──
        std::function<SessionPipeline*(const QString&)> findPipeline;
        std::function<TurnTask*(const QString&)> activeTurn;
        std::function<void(const QString&, SessionPipeline*, const TurnTask*, bool)> flushPendingDeltaLog;
        TurnManager* turnManager = nullptr;
        std::function<void(const QString&)> clearDelegateStartsForSession;
        std::function<void(const QString&)> clearToolProgressCacheForSession;
        std::function<QString(const QString&)> activeSessionForAgent;
        std::function<void(const QString&)> clearActiveSessionForAgent;
        std::function<void(const QString&)> resetSessionStreamState;
        std::function<void(const QString&)> tryStartNextTurn;
        std::function<void(const QString&)> tryStartNextTurnForAgent;
        // ── Finish 专有字段 ──
        std::function<bool(const QString&)> isManualHeartbeatClientMessage;
        std::function<QJsonObject(const QString&)> taskStateForSession;
        std::function<int(const QString&)> heartbeatDuplicateWindowMs;
        std::function<QDateTime(const QString&)> heartbeatLastDeliveredAt;
        std::function<QString(const QString&)> heartbeatLastDeliveredDigest;
        std::function<void(const QString&, const QString&, const QString&, const QDateTime&)> recordHeartbeatSuppressed;
        std::function<void(const QString&, const QString&, const QString&, const QDateTime&)> recordHeartbeatDelivered;
        std::function<void(const QString&, const QString&, const QString&, const QDateTime&)> recordHeartbeatManualSuppress;
        std::function<bool(const QString&, const ConversationContext::TaskContextSnapshot&)> saveTaskContextSnapshot;
        std::function<bool(const QString&, const ConversationContext::ContextCompressionCheckpoint&)> saveContextCompressionCheckpoint;
        std::function<bool(const QString&, const ConversationContext::ResumePacket&)> saveResumePacket;
        std::function<ConversationContext::TaskContextSnapshot(const QString&, bool* ok)> loadTaskContextSnapshot;
        std::function<void(const QString&, const QString&)> emitFinished;

        // ── Error 专有字段 ──
        std::function<bool(const QString&)> isTransientUpstreamError;
        std::function<QList<ActiveJobInfo>(const QString&, int)> listActiveJobs;
        std::function<QString(const QList<ActiveJobInfo>&)> buildDelegateRecoveryReply;
        std::function<void(const QString&, const QString&)> emitError;

        // ── Memory 专有字段 ──
        std::function<bool()> reflectionEnabled;
        std::function<bool(const QString&,
                           const QString&,
                           const TurnTask&,
                           QString*,
                           QString*,
                           QJsonObject*,
                           QString*)> retainTurn;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&)> refreshMemoryIndexAndEmit;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask&,
                           bool,
                           const QString&)> maybeReflectMemoryAndEmit;
    };

    explicit TurnCompletionCoordinator(const Dependencies& deps);

    Result onRuntimeFinished(const QString& sessionId, const QString& fullContent);
    void onRuntimeError(const QString& sessionId, const QString& errorMsg);

private:
    void finalizeTurnInternal(const QString& sessionId, TurnTask* outTurn);
    void handleFinishMemory(const QString& sessionId,
                            const QString& agentId,
                            const TurnTask& finishedTurn,
                            bool skipMemoryForHeartbeat,
                            bool heartbeatTurn);

    Dependencies m_deps;
};

#endif // TURNCOMPLETIONCOORDINATOR_H
