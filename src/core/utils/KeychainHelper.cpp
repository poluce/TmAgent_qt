#include "KeychainHelper.h"
#include "keychain.h"
#include <QCoreApplication>
#include <QEventLoop>

namespace {
QString sanitizeSegment(const QString& value)
{
    QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("default");
    return trimmed;
}
} // namespace

QString KeychainHelper::serviceName()
{
    QString name = QCoreApplication::applicationName().trimmed();
    if (name.isEmpty())
        name = QStringLiteral("TmAgent");
    return name;
}

QString KeychainHelper::entryIdForModel(const QString& provider, const QString& modelId)
{
    return QStringLiteral("%1:%2").arg(sanitizeSegment(provider), sanitizeSegment(modelId));
}

QString KeychainHelper::makeKeyRef(const QString& entryId)
{
    return QStringLiteral("keychain:%1").arg(entryId.trimmed());
}

bool KeychainHelper::parseKeyRef(const QString& value, QString* entryId)
{
    if (!entryId)
        return false;
    const QString trimmed = value.trimmed();
    if (!trimmed.startsWith(QStringLiteral("keychain:")))
        return false;
    *entryId = trimmed.mid(9).trimmed();
    return !entryId->isEmpty();
}

QString KeychainHelper::readPasswordSync(const QString& entryId, bool* ok, QString* error)
{
    QKeychain::ReadPasswordJob job(serviceName());
    job.setKey(entryId);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
    if (job.error() != QKeychain::NoError) {
        if (error)
            *error = job.errorString();
        if (ok)
            *ok = false;
        return QString();
    }
    if (ok)
        *ok = true;
    return job.textData();
}

bool KeychainHelper::writePasswordSync(const QString& entryId, const QString& secret, QString* error)
{
    QKeychain::WritePasswordJob job(serviceName());
    job.setKey(entryId);
    job.setTextData(secret);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
    if (job.error() != QKeychain::NoError) {
        if (error)
            *error = job.errorString();
        return false;
    }
    return true;
}
