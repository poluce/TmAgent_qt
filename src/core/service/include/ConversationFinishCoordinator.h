#ifndef CONVERSATIONFINISHCOORDINATOR_H
#define CONVERSATIONFINISHCOORDINATOR_H

#include "core/model/Message.h"
#include "core/service/include/ConversationContextTypes.h"
#include "TurnManager.h"
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <functional>

class ConversationFinishCoordinator {
public:
    struct Result {
        bool valid = false;
        TurnTask finishedTurn;
        QString agentId;
        bool skipMemoryForHeartbeat = false;
        bool heartbeatTurn = false;
    };

    struct Dependencies {
        std::function<void(const QString&, TurnTask*)> finalizeTurn;
        std::function<QString(const QString&)> agentIdentityIdForSession;
        std::function<bool(const QString&)> isBackgroundClientMessage;
        std::function<bool(const QString&)> isHeartbeatClientMessage;
        std::function<bool(const QString&)> isManualHeartbeatClientMessage;
        std::function<void(const QString&, const QString&)> reportPulseProgress;
        std::function<QJsonObject(const QString&)> taskStateForSession;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QJsonObject&)> updateTaskStateForSession;
        std::function<QString(const QString&, int)> taskStateTextPreview;
        std::function<int(const QString&)> heartbeatDuplicateWindowMs;
        std::function<QDateTime(const QString&)> heartbeatLastDeliveredAt;
        std::function<QString(const QString&)> heartbeatLastDeliveredDigest;
        std::function<void(const QString&, const QString&, const QString&, const QDateTime&)> recordHeartbeatSuppressed;
        std::function<void(const QString&, const QString&, const QString&, const QDateTime&)> recordHeartbeatDelivered;
        std::function<void(const QString&, const QString&, const QString&, const QDateTime&)> recordHeartbeatManualSuppress;
        std::function<void(const QString&, const Message&)> postMessage;
        std::function<bool(const QString&, const ConversationContext::TaskContextSnapshot&)> saveTaskContextSnapshot;
        std::function<bool(const QString&, const ConversationContext::ContextCompressionCheckpoint&)> saveContextCompressionCheckpoint;
        std::function<bool(const QString&, const ConversationContext::ResumePacket&)> saveResumePacket;
        std::function<ConversationContext::TaskContextSnapshot(const QString&, bool* ok)> loadTaskContextSnapshot;
        std::function<void(const QString&, const QString&)> emitFinished;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
    };

    explicit ConversationFinishCoordinator(const Dependencies& dependencies);

    Result onRuntimeFinished(const QString& sessionId, const QString& fullContent);

private:
    Dependencies m_dependencies;
};

#endif // CONVERSATIONFINISHCOORDINATOR_H

