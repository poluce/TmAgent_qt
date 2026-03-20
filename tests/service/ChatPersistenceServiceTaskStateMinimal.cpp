#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>

namespace {

QString taskStateTestRoot()
{
    const QByteArray envRoot = qgetenv("TMAGENT_TEST_TASK_STATE_ROOT");
    if (!envRoot.trimmed().isEmpty())
        return QString::fromUtf8(envRoot);
    return QDir::home().filePath(QStringLiteral(".tmagent-test-task-state"));
}

QHash<QString, QString>& taskStateAppState()
{
    static QHash<QString, QString> s_appState;
    return s_appState;
}

}

DatabaseManager* DatabaseManager::instance()
{
    static DatabaseManager s_instance;
    return &s_instance;
}

DatabaseManager::DatabaseManager() = default;

bool DatabaseManager::isReady() const
{
    return true;
}

QString ChatPersistenceService::dataRootPath() const
{
    return taskStateTestRoot();
}

QString ChatPersistenceService::sessionsDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("sessions"));
}

QString ChatPersistenceService::sessionDataDirPath(const QString& sessionId) const
{
    return QDir(QDir(sessionsDirPath()).filePath(QStringLiteral("data"))).filePath(sessionId);
}

QJsonObject ChatPersistenceService::readJsonObject(const QString& filePath, bool* ok) const
{
    if (ok)
        *ok = false;
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QJsonObject();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    if (ok)
        *ok = true;
    return doc.object();
}

bool ChatPersistenceService::writeJsonObject(const QString& filePath, const QJsonObject& obj) const
{
    const QFileInfo info(filePath);
    QDir().mkpath(info.dir().path());
    QFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text | QFile::Truncate))
        return false;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool ChatPersistenceService::setAppState(const QString& key, const QString& value) const
{
    taskStateAppState().insert(key, value);
    return true;
}

QString ChatPersistenceService::getAppState(const QString& key, const QString& defaultValue) const
{
    return taskStateAppState().value(key, defaultValue);
}

bool ChatPersistenceService::removeAppState(const QString& key) const
{
    return taskStateAppState().remove(key) > 0;
}
