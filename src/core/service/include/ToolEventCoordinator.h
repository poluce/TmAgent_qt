#ifndef TOOLEVENTCOORDINATOR_H
#define TOOLEVENTCOORDINATOR_H

#include "CoordinatorContext.h"
#include "core/agent/ToolTypes.h"
#include "core/model/Message.h"
#include "TurnManager.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class ToolEventCoordinator {
public:
    struct DelegateStats {
        int totalCount = 0;
        int successCount = 0;
        int failureCount = 0;
        qint64 totalDurationMs = 0;
    };

    struct Dependencies {
        CoordinatorContext ctx;

        // ── 原 ToolEvent 专有字段 ──
        std::function<void(const QString&, const QString&)> suppressHeartbeat;
        std::function<void(const QString&)> unsuppressHeartbeat;
        std::function<qint64(const QString&, const QString&)> takeDelegateStartMs;
        std::function<void(const QString&, const QString&, qint64)> putDelegateStartMs;
        std::function<DelegateStats(const QString&)> delegateStatsForSession;
        std::function<void(const QString&, const DelegateStats&)> setDelegateStatsForSession;
        std::function<QJsonObject(const QString&)> taskStateForSession;

        // ── 原 ToolPersistence 专有字段 ──
        std::function<QString(const QString&)> sessionDataDirPath;
        std::function<QJsonObject(const QString&, const QJsonObject&)> sanitizePersistedToolArguments;
        std::function<QJsonObject(const QString&, const QJsonObject&)> sanitizePersistedToolEventData;
        std::function<QString(const QString&, const QString&)> sanitizePersistedToolRawResult;
        std::function<QJsonObject(const ToolExecutionEvent&)> toolEventToJson;
        std::function<void(const QString&, const ToolExecutionEvent&)> emitToolEvent;
        std::function<qint64(const QString&)> toolProgressLastPersistMs;
        std::function<QString(const QString&)> toolProgressLastDigest;
        std::function<void(const QString&, qint64)> setToolProgressLastPersistMs;
        std::function<void(const QString&, const QString&)> setToolProgressLastDigest;
        qint64 toolProgressPersistMinIntervalMs = 1200;
    };

    explicit ToolEventCoordinator(const Dependencies& dependencies);

    void handleToolEvent(const QString& sessionId,
                         const TurnTask* activeTurn,
                         const ToolExecutionEvent& event);

private:
    void handleDelegateTracking(const QString& sessionId,
                                const QString& agentId,
                                const TurnTask* activeTurn,
                                const ToolExecutionEvent& event);
    void handlePersistence(const QString& sessionId,
                           const QString& agentId,
                           const TurnTask* activeTurn,
                           const ToolExecutionEvent& event);
    Dependencies m_deps;
};

#endif // TOOLEVENTCOORDINATOR_H
