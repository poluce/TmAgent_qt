#ifndef HEARTBEATSNAPSHOTSERVICE_H
#define HEARTBEATSNAPSHOTSERVICE_H

#include "HeartbeatTypes.h"
#include "core/agent/DelegateTaskScheduler.h"
#include <functional>

class HeartbeatSnapshotService {
public:
    struct Dependencies {
        std::function<QString(const QString&)> providerStateForAgent;
        std::function<QString(const QString&)> providerIdForAgent;
        std::function<QList<DelegateTaskScheduler::JobInfo>(const QString&)> activeJobsForAgent;
        std::function<QString(const QString&)> pulseStateForAgent;
        std::function<int(const QString&)> schedulerEnabledJobsForAgent;
        std::function<bool(const QString&)> schedulerIssueForAgent;
        std::function<int(const QString&)> memoryRetainedTurnsForAgent;
        std::function<qint64(const QString&)> memoryDocSizeBytesForAgent;
        std::function<bool(const QString&)> memoryIssueForAgent;
    };

    explicit HeartbeatSnapshotService(const Dependencies& dependencies)
        : m_dependencies(dependencies)
    {
    }

    HeartbeatSnapshot collect(const QString& agentId) const;

private:
    Dependencies m_dependencies;
};

#endif // HEARTBEATSNAPSHOTSERVICE_H
