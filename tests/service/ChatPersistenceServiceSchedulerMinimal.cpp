#include "core/persistence/ChatPersistenceService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

namespace {

QString schedulerTestRoot()
{
    const QByteArray envRoot = qgetenv("TMAGENT_TEST_SCHEDULER_ROOT");
    if (!envRoot.trimmed().isEmpty())
        return QString::fromUtf8(envRoot);
    return QDir::home().filePath(QStringLiteral(".tmagent-test-scheduler"));
}

}

QString ChatPersistenceService::dataRootPath() const
{
    return schedulerTestRoot();
}

QString ChatPersistenceService::configDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("config"));
}

QString ChatPersistenceService::scheduledJobsPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("scheduled_jobs.json"));
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
