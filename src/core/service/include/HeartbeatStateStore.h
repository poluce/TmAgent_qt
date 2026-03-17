#ifndef HEARTBEATSTATESTORE_H
#define HEARTBEATSTATESTORE_H

#include "HeartbeatRuntimeState.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <functional>

class HeartbeatStateStore {
public:
    struct Dependencies {
        std::function<QString(const QString&)> loadAppState;
        std::function<bool(const QString&, const QString&)> saveAppState;
        std::function<QJsonObject(const QString&)> readJsonObject;
        std::function<QString()> agentsDirPath;
        std::function<bool()> databaseReady;
    };

    explicit HeartbeatStateStore(const Dependencies& dependencies)
        : m_dependencies(dependencies)
    {
    }

    void load(const QString& agentId, HeartbeatRuntimeState* runtimeState) const
    {
        if (!runtimeState)
            return;

        const QString trimmedAgentId = agentId.trimmed();
        runtimeState->loaded = true;
        if (trimmedAgentId.isEmpty())
            return;

        runtimeState->stateStorageKey = storageKeyForAgent(trimmedAgentId);
        runtimeState->statePath = statePathForAgent(trimmedAgentId);

        if (m_dependencies.databaseReady && m_dependencies.databaseReady() && m_dependencies.loadAppState) {
            QJsonParseError err;
            const QString raw = m_dependencies.loadAppState(runtimeState->stateStorageKey);
            if (!raw.trimmed().isEmpty()) {
                const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
                if (err.error == QJsonParseError::NoError && doc.isObject())
                    runtimeState->stateObj = doc.object();
            }
        }

        if (runtimeState->stateObj.isEmpty() && m_dependencies.readJsonObject && !runtimeState->statePath.trimmed().isEmpty())
            runtimeState->stateObj = m_dependencies.readJsonObject(runtimeState->statePath);

        runtimeState->lastSnapshotDigest = runtimeState->stateObj.value(QStringLiteral("last_snapshot_digest")).toString().trimmed();
        runtimeState->lastSnapshotObj = runtimeState->stateObj.value(QStringLiteral("last_snapshot")).toObject();
        runtimeState->hasSnapshot = !runtimeState->lastSnapshotObj.isEmpty();
        runtimeState->lastNotifyAtUtc = parseIsoDateTimeToUtc(
            runtimeState->stateObj.value(QStringLiteral("last_notify_at_utc")).toString());
        runtimeState->lastDeliveredAtUtc = parseIsoDateTimeToUtc(
            runtimeState->stateObj.value(QStringLiteral("last_delivered_at_utc")).toString());
        runtimeState->lastPersistAtUtc = parseIsoDateTimeToUtc(
            runtimeState->stateObj.value(QStringLiteral("last_snapshot_at_utc")).toString());
        runtimeState->lastDeliveredDigest = runtimeState->stateObj.value(QStringLiteral("last_delivered_digest")).toString().trimmed();
    }

    bool persist(const QString& agentId,
                 HeartbeatRuntimeState* runtimeState,
                 const QDateTime& nowUtc,
                 bool forcePersist) const
    {
        Q_UNUSED(agentId);
        if (!runtimeState || !forcePersist)
            return false;
        if (!m_dependencies.databaseReady || !m_dependencies.databaseReady())
            return false;
        if (!m_dependencies.saveAppState || runtimeState->stateStorageKey.trimmed().isEmpty())
            return false;

        const bool persisted = m_dependencies.saveAppState(
            runtimeState->stateStorageKey,
            QString::fromUtf8(QJsonDocument(runtimeState->stateObj).toJson(QJsonDocument::Compact)));
        if (persisted)
            runtimeState->lastPersistAtUtc = nowUtc;
        return persisted;
    }

    static QString storageKeyForAgent(const QString& agentId)
    {
        return QStringLiteral("heartbeat_state:") + agentId.trimmed();
    }

    QString statePathForAgent(const QString& agentId) const
    {
        if (!m_dependencies.agentsDirPath)
            return QString();
        return QDir(QDir(m_dependencies.agentsDirPath()).filePath(agentId))
            .filePath(QStringLiteral("heartbeat_state.json"));
    }

    static QDateTime parseIsoDateTimeToUtc(const QString& raw)
    {
        const QString text = raw.trimmed();
        if (text.isEmpty())
            return QDateTime();
        QDateTime dt = QDateTime::fromString(text, Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(text, Qt::ISODate);
        if (!dt.isValid())
            return QDateTime();
        return dt.toUTC();
    }

private:
    Dependencies m_dependencies;
};

#endif // HEARTBEATSTATESTORE_H

