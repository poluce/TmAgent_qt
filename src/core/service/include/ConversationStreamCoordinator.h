#ifndef CONVERSATIONSTREAMCOORDINATOR_H
#define CONVERSATIONSTREAMCOORDINATOR_H

#include "TurnManager.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class Session;

class ConversationStreamCoordinator {
public:
    struct Dependencies {
        std::function<SessionPipeline*(const QString&)> findPipeline;
        std::function<TurnTask*(const QString&)> activeTurn;
        std::function<QString(const QString&)> agentIdentityIdForSession;
        std::function<void(const QString&, const QString&)> reportPulseProgress;
        std::function<bool(const QString&)> isBackgroundClientMessage;
        std::function<Session*(const QString&)> findSession;
        std::function<void(const QString&, const QString&)> emitStreamData;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
        std::function<void(const QString&, SessionPipeline*, const TurnTask*, bool)> flushPendingDeltaLog;
        bool logVerboseStreamEvents = false;
    };

    explicit ConversationStreamCoordinator(const Dependencies& dependencies);

    void onRuntimeStreamData(const QString& sessionId, const QString& data);

private:
    Dependencies m_dependencies;
};

#endif // CONVERSATIONSTREAMCOORDINATOR_H

