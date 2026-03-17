#ifndef CONVERSATIONTOOLPERSISTENCECOORDINATOR_H
#define CONVERSATIONTOOLPERSISTENCECOORDINATOR_H

#include "core/agent/ToolTypes.h"
#include "core/model/Message.h"
#include "TurnManager.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class ConversationToolPersistenceCoordinator {
public:
    struct Dependencies {
        std::function<void(const QString&, const Message&)> postMessage;
        std::function<QString(const QString&)> sessionDataDirPath;
        std::function<QJsonObject(const QString&, const QJsonObject&)> sanitizePersistedToolArguments;
        std::function<QJsonObject(const QString&, const QJsonObject&)> sanitizePersistedToolEventData;
        std::function<QString(const QString&, const QString&)> sanitizePersistedToolRawResult;
        std::function<QJsonObject(const ToolExecutionEvent&)> toolEventToJson;
        std::function<void(const QString&, const ToolExecutionEvent&)> emitToolEvent;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
        std::function<qint64(const QString&)> toolProgressLastPersistMs;
        std::function<QString(const QString&)> toolProgressLastDigest;
        std::function<void(const QString&, qint64)> setToolProgressLastPersistMs;
        std::function<void(const QString&, const QString&)> setToolProgressLastDigest;
        qint64 toolProgressPersistMinIntervalMs = 1200;
    };

    explicit ConversationToolPersistenceCoordinator(const Dependencies& dependencies);

    void handleToolEvent(const QString& sessionId,
                         const QString& agentId,
                         const TurnTask* activeTurn,
                         const ToolExecutionEvent& event);

private:
    Dependencies m_dependencies;
};

#endif // CONVERSATIONTOOLPERSISTENCECOORDINATOR_H

