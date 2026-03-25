#ifndef HEARTBEATSTATESTORE_H
#define HEARTBEATSTATESTORE_H

#include "HeartbeatRuntimeState.h"
#include <QJsonDocument>
#include <QString>
#include <functional>

class HeartbeatStateStore {
public:
    struct Dependencies {
        std::function<QString(const QString&)> loadAppState;
        std::function<bool(const QString&, const QString&)> saveAppState;
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
        runtimeState->stateStorageKey = storageKeyForAgent(trimmedAgentId);
        runtimeState->stateLocation = locationForAgent(trimmedAgentId);
        if (trimmedAgentId.isEmpty())
            return;
        if (!m_dependencies.databaseReady || !m_dependencies.databaseReady() || !m_dependencies.loadAppState)
            return;

        QJsonParseError err;
        const QString raw = m_dependencies.loadAppState(runtimeState->stateStorageKey);
        if (raw.trimmed().isEmpty())
            return;

        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return;

        *runtimeState = heartbeatRuntimeStateFromJson(doc.object());
        runtimeState->loaded = true;
        runtimeState->stateStorageKey = storageKeyForAgent(trimmedAgentId);
        runtimeState->stateLocation = locationForAgent(trimmedAgentId);
    }

    bool save(const QString& agentId, HeartbeatRuntimeState* runtimeState) const
    {
        Q_UNUSED(agentId);
        if (!runtimeState || !m_dependencies.databaseReady || !m_dependencies.databaseReady())
            return false;
        if (!m_dependencies.saveAppState || runtimeState->stateStorageKey.trimmed().isEmpty())
            return false;

        const QString payload =
            QString::fromUtf8(QJsonDocument(heartbeatRuntimeStateToJson(*runtimeState))
                                  .toJson(QJsonDocument::Compact));
        return m_dependencies.saveAppState(runtimeState->stateStorageKey, payload);
    }

    bool remove(const QString& agentId) const
    {
        Q_UNUSED(agentId);
        return false;
    }

    static QString storageKeyForAgent(const QString& agentId)
    {
        return QStringLiteral("heartbeat_runtime:") + agentId.trimmed();
    }

    static QString locationForAgent(const QString& agentId)
    {
        return QStringLiteral("SQLite app_state :: heartbeat_runtime:%1").arg(agentId.trimmed());
    }

private:
    Dependencies m_dependencies;
};

#endif // HEARTBEATSTATESTORE_H
