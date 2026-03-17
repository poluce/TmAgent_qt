#ifndef HEARTBEATSNAPSHOTCOORDINATOR_H
#define HEARTBEATSNAPSHOTCOORDINATOR_H

#include "core/agent/DelegateTaskScheduler.h"
#include "HeartbeatService.h"
#include <QDateTime>
#include <QJsonObject>
#include <QString>

class HeartbeatSnapshotCoordinator {
public:
    struct RuntimeState {
        bool hasSnapshot = false;
        QJsonObject stateObj;
        QJsonObject lastSnapshotObj;
        QString lastSnapshotDigest;
        QDateTime lastNotifyAtUtc;
        QDateTime lastPersistAtUtc;
    };

    struct Inputs {
        QString agentId;
        QString reason;
        HeartbeatConfig config;
        QString providerId;
        bool providerDown = false;
        QList<DelegateTaskScheduler::JobInfo> activeJobs;
        QString pulseState;
        int schedulerEnabledJobs = 0;
        QDateTime schedulerNextFireAtUtc;
        int memoryRetainedTurns = 0;
        qint64 memoryDocSizeBytes = -1;
        RuntimeState runtimeState;
        QDateTime nowUtc;
    };

    struct Result {
        bool valid = false;
        QString reasonLabel;
        bool forceInteractive = false;
        bool hasChange = false;
        bool hasActionableChange = false;
        bool shouldNotify = true;
        QString skipReason;
        bool shouldPersistState = false;
        RuntimeState runtimeState;
        QJsonObject triggeredExtra;
    };

    static Result evaluate(const Inputs& inputs);
};

#endif // HEARTBEATSNAPSHOTCOORDINATOR_H

