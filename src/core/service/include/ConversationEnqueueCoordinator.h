#ifndef CONVERSATIONENQUEUECOORDINATOR_H
#define CONVERSATIONENQUEUECOORDINATOR_H

#include "TurnManager.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class IdentityManager;
class SessionManager;

class ConversationEnqueueCoordinator {
public:
    struct Limits {
        int softQueueDepth = 10;
        int hardQueueDepth = 200;
        int queueMergeWindowMs = 2500;
        int queueMergeMaxMergedMessages = 4;
        int queueMergeMaxChars = 12000;
    };

    struct Dependencies {
        IdentityManager* identityManager = nullptr;
        SessionManager* sessionManager = nullptr;
        TurnManager* turnManager = nullptr;

        std::function<bool(const QString&, const QString&)> canIdentitySendMessage;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QJsonObject&)> updateTaskStateForSession;
        std::function<void(const QString&)> tryStartNextTurn;
        std::function<bool(const QString&)> isBackgroundClientMessage;
        std::function<QString(const QString&, int)> taskStateTextPreview;
    };

    ConversationEnqueueCoordinator(const Dependencies& dependencies, const Limits& limits);

    QString enqueueUserMessageAs(const QString& actorIdentityId,
                                 const QString& sessionId,
                                 const QString& text,
                                 const QString& clientMessageId = QString());

private:
    Dependencies m_dependencies;
    Limits m_limits;
};

#endif // CONVERSATIONENQUEUECOORDINATOR_H

