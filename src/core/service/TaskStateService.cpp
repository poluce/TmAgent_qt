#include "TaskStateService.h"

#include "core/persistence/ChatPersistenceService.h"
#include <QDateTime>
#include <QFile>

namespace {

const QSet<QString> kAllowedTaskStates = {
    QStringLiteral("queued"),
    QStringLiteral("running"),
    QStringLiteral("blocked"),
    QStringLiteral("done"),
    QStringLiteral("failed"),
    QStringLiteral("canceled")
};

QString normalizedTextValue(const QJsonValue& value)
{
    if (!value.isString())
        return QString();
    return value.toString().simplified();
}

}

void TaskStateService::setPersistence(ChatPersistenceService* persistence)
{
    m_persistence = persistence;
}

QString TaskStateService::taskStatePath(const QString& sessionId) const
{
    if (!m_persistence || sessionId.trimmed().isEmpty())
        return QString();
    return m_persistence->sessionTaskStatePath(sessionId.trimmed());
}

QJsonObject TaskStateService::loadState(const QString& sessionId) const
{
    const QString key = sessionId.trimmed();
    if (key.isEmpty())
        return QJsonObject();
    if (m_loadedSessions.contains(key))
        return m_stateCache.value(key);

    m_loadedSessions.insert(key);
    const QString path = taskStatePath(key);
    if (path.isEmpty())
        return QJsonObject();

    bool ok = false;
    QJsonObject state = m_persistence->readJsonObject(path, &ok);
    if (!ok)
        state = QJsonObject();
    m_stateCache.insert(key, state);
    return state;
}

QJsonObject TaskStateService::stateForSession(const QString& sessionId) const
{
    return loadState(sessionId);
}

QString TaskStateService::normalizeState(const QString& rawState)
{
    const QString normalized = rawState.trimmed().toLower();
    if (kAllowedTaskStates.contains(normalized))
        return normalized;
    return QStringLiteral("queued");
}

void TaskStateService::normalizePatch(QJsonObject* patch)
{
    if (!patch)
        return;

    if (patch->contains(QStringLiteral("state")))
        patch->insert(QStringLiteral("state"), normalizeState(patch->value(QStringLiteral("state")).toString()));

    const QStringList textKeys = {
        QStringLiteral("session_id"),
        QStringLiteral("agent_id"),
        QStringLiteral("trace_id"),
        QStringLiteral("turn_id"),
        QStringLiteral("run_id"),
        QStringLiteral("reason"),
        QStringLiteral("summary"),
        QStringLiteral("current_step"),
        QStringLiteral("next_step"),
        QStringLiteral("last_error"),
        QStringLiteral("waiting_job_id"),
        QStringLiteral("source_event")
    };
    for (const QString& key : textKeys) {
        if (!patch->contains(key))
            continue;
        const QJsonValue value = patch->value(key);
        if (value.isNull() || value.isUndefined()) {
            patch->insert(key, QJsonValue::Null);
            continue;
        }
        patch->insert(key, normalizedTextValue(value));
    }
}

bool TaskStateService::updateState(const QString& sessionId, const QJsonObject& rawPatch, QJsonObject* mergedState)
{
    const QString key = sessionId.trimmed();
    if (!m_persistence || key.isEmpty())
        return false;

    QJsonObject patch = rawPatch;
    patch.insert(QStringLiteral("session_id"), key);
    patch.insert(QStringLiteral("updated_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    patch.insert(QStringLiteral("schema_version"), 1);
    normalizePatch(&patch);

    QJsonObject state = loadState(key);
    for (auto it = patch.constBegin(); it != patch.constEnd(); ++it) {
        if (it.value().isNull())
            state.remove(it.key());
        else
            state.insert(it.key(), it.value());
    }

    if (m_persistence->writeJsonObject(taskStatePath(key), state)) {
        m_stateCache.insert(key, state);
        if (mergedState)
            *mergedState = state;
        return true;
    }
    return false;
}

bool TaskStateService::clearState(const QString& sessionId)
{
    const QString key = sessionId.trimmed();
    if (key.isEmpty())
        return false;

    m_stateCache.remove(key);
    m_loadedSessions.remove(key);
    const QString path = taskStatePath(key);
    if (path.isEmpty())
        return false;
    if (!QFile::exists(path))
        return true;
    return QFile::remove(path);
}
