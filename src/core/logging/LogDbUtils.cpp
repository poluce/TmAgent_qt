#include "LogDbUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QtGlobal>

namespace {

QString connectionNameFor(const QString& dataRootPath)
{
    const QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThread()));
    return QStringLiteral("tmagent_log_") + threadId + QStringLiteral("_")
        + QString::number(qHash(dataRootPath));
}

} // namespace

namespace LogDbUtils {

QString resolveDataRoot(const QString& dataRootPath)
{
    if (!dataRootPath.trimmed().isEmpty())
        return dataRootPath.trimmed();
    return QDir::home().filePath(QStringLiteral(".tmagent"));
}

QString databasePathFromRoot(const QString& dataRootPath)
{
    return QDir(resolveDataRoot(dataRootPath)).filePath(QStringLiteral("tmagent.db"));
}

QSqlDatabase openConnection(const QString& dataRootPath, QString* error)
{
    if (error)
        *error = QString();

    const QString dbPath = databasePathFromRoot(dataRootPath);
    if (!QFileInfo::exists(dbPath)) {
        if (error)
            *error = QStringLiteral("数据库文件不存在: %1").arg(QDir::toNativeSeparators(dbPath));
        return QSqlDatabase();
    }

    const QString connName = connectionNameFor(dbPath);

    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase existing = QSqlDatabase::database(connName);
        if (existing.isOpen())
            return existing;
        if (!existing.open() && error)
            *error = existing.lastError().text();
        return existing;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        if (error)
            *error = db.lastError().text();
        return db;
    }

    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    QSqlQuery timeout(db);
    timeout.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    return db;
}

} // namespace LogDbUtils
