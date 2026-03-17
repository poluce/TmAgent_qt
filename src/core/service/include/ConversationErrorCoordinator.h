#ifndef CONVERSATIONERRORCOORDINATOR_H
#define CONVERSATIONERRORCOORDINATOR_H

#include "core/model/Message.h"
#include "TurnManager.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class ConversationErrorCoordinator {
public:
    struct ActiveJobInfo {
        QString jobId;
        QString status;
        QString summary;
    };

    struct Dependencies {
        std::function<void(const QString&, TurnTask*)> finalizeTurn;
        std::function<bool(const QString&)> isBackgroundClientMessage;
        std::function<bool(const QString&)> isHeartbeatClientMessage;
        std::function<QString(const QString&)> agentIdentityIdForSession;
        std::function<void(const QString&, const QString&)> reportPulseProgress;
        std::function<bool(const QString&)> isTransientUpstreamError;
        std::function<QList<ActiveJobInfo>(const QString&, int)> listActiveJobs;
        std::function<QString(const QList<ActiveJobInfo>&)> buildDelegateRecoveryReply;
        std::function<void(const QString&, const Message&)> postMessage;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QJsonObject&)> updateTaskStateForSession;
        std::function<QString(const QString&, int)> taskStateTextPreview;
        std::function<void(const QString&, const QString&)> emitFinished;
        std::function<void(const QString&, const QString&)> emitError;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
    };

    explicit ConversationErrorCoordinator(const Dependencies& dependencies);

    void onRuntimeError(const QString& sessionId, const QString& errorMsg);

private:
    Dependencies m_dependencies;
};

#endif // CONVERSATIONERRORCOORDINATOR_H

