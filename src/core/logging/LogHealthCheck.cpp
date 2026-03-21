#include "LogHealthCheck.h"
#include "LogRecordSupport.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QVariant>

namespace LogHealthCheck {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const qint64 kMinDiskFreeBytes = 100LL * 1024 * 1024;   // 100 MB
static const qint64 kMaxLogDirBytes   = 500LL * 1024 * 1024;   // 500 MB

// ---------------------------------------------------------------------------
// check()
// ---------------------------------------------------------------------------

HealthStatus check(const QString& dataRootPath)
{
    HealthStatus status;

    QString root = dataRootPath;
    if (root.isEmpty())
        root = QDir::homePath() + QStringLiteral("/.tmagent");

    const QString logsDir = root + QStringLiteral("/logs");
    const QString eventsFile = logsDir + QStringLiteral("/events-current.jsonl");
    const QString sessionsDir = root + QStringLiteral("/sessions/data");
    const QString dbPath = LogRecordSupport::databasePathFromRoot(root);

    QString dbError;
    QSqlDatabase db = LogRecordSupport::openConnection(root, &dbError);
    const bool dbReady = db.isValid() && db.isOpen();

    if (dbReady) {
        status.eventBackend = QStringLiteral("sqlite");
        status.eventStorePath = QStringLiteral("sqlite://events");

        const QFileInfo dbInfo(dbPath);
        status.canRead = true;
        status.canWrite = dbInfo.exists() ? dbInfo.isWritable() : QFileInfo(root).isWritable();

        const QStorageInfo storage(QFileInfo(dbPath).absolutePath());
        if (storage.isValid()) {
            status.diskFreeBytes = storage.bytesAvailable();
            if (status.diskFreeBytes >= 0 && status.diskFreeBytes < kMinDiskFreeBytes) {
                status.issues.append(
                    QStringLiteral("Low disk space: %1 MB free (threshold: %2 MB)")
                        .arg(status.diskFreeBytes / (1024 * 1024))
                        .arg(kMinDiskFreeBytes / (1024 * 1024)));
            }
        }

        status.logDirSizeBytes = dbInfo.exists() ? dbInfo.size() : 0;

        QSqlQuery eventCountQuery(db);
        if (eventCountQuery.exec(QStringLiteral("SELECT COUNT(*) FROM events")) && eventCountQuery.next())
            status.eventFileCount = eventCountQuery.value(0).toInt();

        QSqlQuery sessionCountQuery(db);
        if (sessionCountQuery.exec(QStringLiteral("SELECT COUNT(*) FROM sessions")) && sessionCountQuery.next())
            status.sessionCount = sessionCountQuery.value(0).toInt();

        status.healthy = status.issues.isEmpty();
        return status;
    }

    status.eventBackend = QStringLiteral("jsonl");
    status.eventStorePath = eventsFile;

    // --- 检查 logs 目录是否存在且可写 ---
    {
        const QFileInfo dirInfo(logsDir);
        if (!dirInfo.exists() || !dirInfo.isDir()) {
            status.canWrite = false;
            status.issues.append(QStringLiteral("Logs directory does not exist: %1").arg(logsDir));
        } else {
            status.canWrite = dirInfo.isWritable();
            if (!status.canWrite)
                status.issues.append(QStringLiteral("Logs directory is not writable: %1").arg(logsDir));
        }
    }

    // --- 检查 events-current.jsonl 是否可读 ---
    {
        const QFileInfo fi(eventsFile);
        if (fi.exists() && fi.isReadable()) {
            status.canRead = true;
        } else if (fi.exists()) {
            status.canRead = false;
            status.issues.append(QStringLiteral("Events file is not readable: %1").arg(eventsFile));
        } else {
            // 文件不存在不算错误（可能还没产生事件）
            status.canRead = false;
        }
    }

    // --- 磁盘剩余空间 ---
    {
        const QStorageInfo storage(logsDir);
        if (storage.isValid()) {
            status.diskFreeBytes = storage.bytesAvailable();
            if (status.diskFreeBytes >= 0 && status.diskFreeBytes < kMinDiskFreeBytes) {
                status.issues.append(
                    QStringLiteral("Low disk space: %1 MB free (threshold: %2 MB)")
                        .arg(status.diskFreeBytes / (1024 * 1024))
                        .arg(kMinDiskFreeBytes / (1024 * 1024)));
            }
        }
    }

    // --- 统计 logs 目录大小和文件数 ---
    {
        QDirIterator it(logsDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            status.logDirSizeBytes += it.fileInfo().size();
            status.eventFileCount++;
        }

        if (status.logDirSizeBytes > kMaxLogDirBytes) {
            status.issues.append(
                QStringLiteral("Log directory is large: %1 MB (threshold: %2 MB)")
                    .arg(status.logDirSizeBytes / (1024 * 1024))
                    .arg(kMaxLogDirBytes / (1024 * 1024)));
        }
    }

    // --- 统计会话数 ---
    {
        const QDir sessDir(sessionsDir);
        if (sessDir.exists())
            status.sessionCount = sessDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
    }

    // --- 综合判定 ---
    status.healthy = status.issues.isEmpty();

    return status;
}

// ---------------------------------------------------------------------------
// formatReport()
// ---------------------------------------------------------------------------

QString formatReport(const HealthStatus& status)
{
    QString out;
    out += QStringLiteral("=== Log Health Check ===\n");
    out += QStringLiteral("Status:       %1\n").arg(status.healthy ? QStringLiteral("HEALTHY") : QStringLiteral("UNHEALTHY"));
    out += QStringLiteral("Backend:      %1\n").arg(status.eventBackend.isEmpty() ? QStringLiteral("unknown") : status.eventBackend);
    out += QStringLiteral("Store:        %1\n").arg(status.eventStorePath.isEmpty() ? QStringLiteral("-") : status.eventStorePath);
    out += QStringLiteral("Can write:    %1\n").arg(status.canWrite ? QStringLiteral("yes") : QStringLiteral("no"));
    out += QStringLiteral("Can read:     %1\n").arg(status.canRead ? QStringLiteral("yes") : QStringLiteral("no"));

    if (status.diskFreeBytes >= 0)
        out += QStringLiteral("Disk free:    %1 MB\n").arg(status.diskFreeBytes / (1024 * 1024));

    out += QStringLiteral("Store size:   %1 MB\n").arg(status.logDirSizeBytes / (1024 * 1024));
    out += QStringLiteral("Events:       %1\n").arg(status.eventFileCount);
    out += QStringLiteral("Sessions:     %1\n").arg(status.sessionCount);

    if (!status.issues.isEmpty()) {
        out += QStringLiteral("\nIssues:\n");
        for (const QString& issue : status.issues)
            out += QStringLiteral("  - %1\n").arg(issue);
    }

    return out;
}

// ---------------------------------------------------------------------------
// toJson()
// ---------------------------------------------------------------------------

QJsonObject toJson(const HealthStatus& status)
{
    QJsonObject obj;
    obj[QStringLiteral("healthy")] = status.healthy;
    obj[QStringLiteral("canWrite")] = status.canWrite;
    obj[QStringLiteral("canRead")] = status.canRead;
    obj[QStringLiteral("eventBackend")] = status.eventBackend;
    obj[QStringLiteral("eventStorePath")] = status.eventStorePath;
    obj[QStringLiteral("diskFreeBytes")] = status.diskFreeBytes;
    obj[QStringLiteral("logDirSizeBytes")] = status.logDirSizeBytes;
    obj[QStringLiteral("eventFileCount")] = status.eventFileCount;
    obj[QStringLiteral("sessionCount")] = status.sessionCount;

    QJsonArray issuesArr;
    for (const QString& issue : status.issues)
        issuesArr.append(issue);
    obj[QStringLiteral("issues")] = issuesArr;

    return obj;
}

} // namespace LogHealthCheck
