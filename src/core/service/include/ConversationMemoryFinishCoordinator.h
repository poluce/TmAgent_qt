#ifndef CONVERSATIONMEMORYFINISHCOORDINATOR_H
#define CONVERSATIONMEMORYFINISHCOORDINATOR_H

#include "TurnManager.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class ConversationMemoryFinishCoordinator {
public:
    struct Dependencies {
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
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
    };

    explicit ConversationMemoryFinishCoordinator(const Dependencies& dependencies);

    void handleFinishMemory(const QString& sessionId,
                            const QString& agentId,
                            const TurnTask& finishedTurn,
                            bool skipMemoryForHeartbeat,
                            bool heartbeatTurn);

private:
    Dependencies m_dependencies;
};

#endif // CONVERSATIONMEMORYFINISHCOORDINATOR_H

