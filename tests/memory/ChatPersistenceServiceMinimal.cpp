#include "core/persistence/ChatPersistenceService.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>

QString ChatPersistenceService::dataRootPath() const
{
    return QDir::home().filePath(QStringLiteral(".tmagent"));
}

QString ChatPersistenceService::agentsDirPath() const
{
    return QDir(QDir(dataRootPath()).filePath(QStringLiteral("identities")))
        .filePath(QStringLiteral("agents"));
}

QString ChatPersistenceService::memoryPolicyPath() const
{
    return QDir(QDir(dataRootPath()).filePath(QStringLiteral("config")))
        .filePath(QStringLiteral("memory_policy.json"));
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

