#ifndef HEARTBEATRUNTIMESTATE_H
#define HEARTBEATRUNTIMESTATE_H

#include "HeartbeatTypes.h"
#include <QDateTime>
#include <QJsonObject>
#include <QString>

struct HeartbeatRuntimeState {
    bool loaded = false;
    QString stateStorageKey;
    QString stateLocation;
    HeartbeatLaneState laneState = HeartbeatLaneState::Idle;
    HeartbeatSnapshot lastSnapshot;
    QString lastSnapshotDigest;
    QDateTime lastScheduledAtUtc;
    QDateTime lastStartedAtUtc;
    QDateTime lastCompletedAtUtc;
    QDateTime nextDueAtUtc;
    QDateTime lastEscalationAtUtc;
    QDateTime lastMaintenanceAtUtc;
    QDateTime lastDeliveredAtUtc;
    HeartbeatDecision lastDecision = HeartbeatDecision::Noop;
    QString lastSummaryDigest;
    bool hasPendingTicket = false;
    HeartbeatTicket pendingTicket;
    QString lastDeferredReason;
    QString providerState;
    QString pulseState;
    bool interruptedRun = false;
};

QJsonObject heartbeatRuntimeStateToJson(const HeartbeatRuntimeState& state);
HeartbeatRuntimeState heartbeatRuntimeStateFromJson(const QJsonObject& obj);

#endif // HEARTBEATRUNTIMESTATE_H
