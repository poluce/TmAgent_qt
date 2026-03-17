#ifndef CONVERSATIONTOOLEVENTCOORDINATOR_H
#define CONVERSATIONTOOLEVENTCOORDINATOR_H

#include "core/agent/ToolTypes.h"
#include "TurnManager.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class ConversationToolEventCoordinator {
public:
    struct DelegateStats {
        int totalCount = 0;
        int successCount = 0;
        int failureCount = 0;
        qint64 totalDurationMs = 0;
    };

    struct Dependencies {
        std::function<QString(const QString&)> agentIdentityIdForSession;
        std::function<void(const QString&, const QString&)> reportPulseProgress;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QJsonObject&)> updateTaskStateForSession;
        std::function<QString(const QString&, int)> taskStateTextPreview;
        std::function<void(const QString&, const QString&)> suppressHeartbeat;
        std::function<void(const QString&)> unsuppressHeartbeat;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
        std::function<qint64(const QString&, const QString&)> takeDelegateStartMs;
        std::function<void(const QString&, const QString&, qint64)> putDelegateStartMs;
        std::function<DelegateStats(const QString&)> delegateStatsForSession;
        std::function<void(const QString&, const DelegateStats&)> setDelegateStatsForSession;
    };

    explicit ConversationToolEventCoordinator(const Dependencies& dependencies);

    void handleToolEvent(const QString& sessionId,
                         const TurnTask* activeTurn,
                         const ToolExecutionEvent& event);

private:
    Dependencies m_dependencies;
};

#endif // CONVERSATIONTOOLEVENTCOORDINATOR_H

