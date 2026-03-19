#ifndef MEMORYMAINTENANCESERVICE_H
#define MEMORYMAINTENANCESERVICE_H

#include "TurnManager.h"

#include <QJsonObject>
#include <QString>
#include <functional>

class MemoryMaintenanceService {
public:
    struct Dependencies {
        std::function<bool(const QString&, QJsonObject*, QString*)> rebuildSearchIndex;
        std::function<bool()> reflectionEnabled;
        std::function<int()> reflectionIntervalTurns;
        std::function<bool(const QString&,
                           const QString&,
                           const QString&,
                           const QString&,
                           QString*,
                           QString*,
                           QJsonObject*,
                           QString*)> reflectAndScore;
        std::function<int(const QString&)> retainedTurnsForAgent;
        std::function<void(const QString&, int)> setRetainedTurnsForAgent;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
    };

    explicit MemoryMaintenanceService(const Dependencies& dependencies);

    void refreshIndexAndEmit(const QString& sessionId,
                             const QString& agentId,
                             const TurnTask* turn,
                             const QString& reason,
                             const QString& sourcePath,
                             const QJsonObject& sourceMetadata) const;

    void maybeReflectAndEmit(const QString& sessionId,
                             const QString& agentId,
                             const TurnTask& turn,
                             bool forceReflection = false,
                             const QString& triggerReason = QString()) const;

private:
    Dependencies m_dependencies;
};

#endif // MEMORYMAINTENANCESERVICE_H
