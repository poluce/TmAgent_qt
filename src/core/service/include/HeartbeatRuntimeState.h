#ifndef HEARTBEATRUNTIMESTATE_H
#define HEARTBEATRUNTIMESTATE_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>

struct HeartbeatRuntimeState {
    bool loaded = false;
    bool hasSnapshot = false;
    QString stateStorageKey;
    QString statePath;
    QJsonObject stateObj;
    QJsonObject lastSnapshotObj;
    QString lastSnapshotDigest;
    QDateTime lastNotifyAtUtc;
    QDateTime lastDeliveredAtUtc;
    QDateTime lastPersistAtUtc;
    QString lastDeliveredDigest;
};

#endif // HEARTBEATRUNTIMESTATE_H
