#ifndef CONVERSATIONFINALIZECOORDINATOR_H
#define CONVERSATIONFINALIZECOORDINATOR_H

#include "TurnManager.h"
#include <QString>
#include <functional>

class ConversationFinalizeCoordinator {
public:
    struct Dependencies {
        std::function<SessionPipeline*(const QString&)> findPipeline;
        std::function<TurnTask*(const QString&)> activeTurn;
        std::function<void(const QString&, SessionPipeline*, const TurnTask*, bool)> flushPendingDeltaLog;
        TurnManager* turnManager = nullptr;
        std::function<void(const QString&)> clearDelegateStartsForSession;
        std::function<void(const QString&)> clearToolProgressCacheForSession;
        std::function<QString(const QString&)> agentIdentityIdForSession;
        std::function<QString(const QString&)> activeSessionForAgent;
        std::function<void(const QString&)> clearActiveSessionForAgent;
        std::function<void(const QString&)> resetSessionStreamState;
        std::function<void(const QString&)> tryStartNextTurn;
        std::function<void(const QString&)> tryStartNextTurnForAgent;
    };

    explicit ConversationFinalizeCoordinator(const Dependencies& dependencies);

    void finalizeTurn(const QString& sessionId, TurnTask* outTurn);

private:
    Dependencies m_dependencies;
};

#endif // CONVERSATIONFINALIZECOORDINATOR_H

