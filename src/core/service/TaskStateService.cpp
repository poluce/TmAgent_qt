#include "TaskStateService.h"

#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>

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

QString taskStateStorageKey(const QString& sessionId)
{
    return QStringLiteral("task_state:") + sessionId.trimmed();
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
    return QDir(m_persistence->sessionDataDirPath(sessionId.trimmed()))
        .filePath(QStringLiteral("task_state.json"));
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

    QJsonObject state;
    bool ok = false;
    if (m_persistence && DatabaseManager::instance()->isReady()) {
        const QString raw = m_persistence->getAppState(taskStateStorageKey(key));
        if (!raw.trimmed().isEmpty()) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                state = doc.object();
                ok = true;
            }
        }
    }

    if (!ok) {
        state = m_persistence->readJsonObject(path, &ok);
        if (!ok) {
            state = QJsonObject();
        } else if (m_persistence && DatabaseManager::instance()->isReady() && !state.isEmpty()) {
            m_persistence->setAppState(
                taskStateStorageKey(key),
                QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)));
        }
    }
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
    if (!DatabaseManager::instance()->isReady())
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

    const bool persisted = m_persistence->setAppState(
        taskStateStorageKey(key),
        QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact)));

    if (persisted) {
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
    bool dbOk = true;
    if (m_persistence && DatabaseManager::instance()->isReady())
        dbOk = m_persistence->removeAppState(taskStateStorageKey(key));

    if (path.isEmpty())
        return dbOk;
    if (!QFile::exists(path))
        return dbOk;
    return QFile::remove(path) && dbOk;
}
