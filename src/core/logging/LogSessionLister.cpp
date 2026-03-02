#include "LogSessionLister.h"

#include "LogDbUtils.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace LogSessionLister {

namespace {

QString humanReadableSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString padRight(const QString& text, int width)
{
    if (text.size() >= width)
        return text;
    return text + QString(width - text.size(), QLatin1Char(' '));
}

QString clipId(const QString& id, int maxLen)
{
    if (id.size() <= maxLen)
        return id;
    return id.left(maxLen - 3) + QStringLiteral("...");
}

QDateTime parseTimestamp(const QString& raw)
{
    if (raw.trimmed().isEmpty())
        return QDateTime();

    QDateTime dt = QDateTime::fromString(raw, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(raw, Qt::ISODate);
    if (dt.isValid() && dt.timeSpec() == Qt::LocalTime)
        dt = dt.toUTC();
    return dt;
}

} // namespace

ListResult listSessions(const QString& dataRootPath)
{
    ListResult result;

    QString dbError;
    QSqlDatabase db = LogDbUtils::openConnection(dataRootPath, &dbError);
    if (!db.isValid() || !db.isOpen()) {
        result.warnings.append(
            QStringLiteral("SQLite 连接不可用，无法列出会话: %1").arg(dbError));
        return result;
    }

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT "
            "  s.id, "
            "  s.owner_id, "
            "  s.title, "
            "  s.created_at, "
            "  s.last_active_at, "
            "  COALESCE(msg.message_count, 0), "
            "  COALESCE(msg.last_message_ts, '') "
            "FROM sessions s "
            "LEFT JOIN ("
            "  SELECT session_id, COUNT(*) AS message_count, MAX(timestamp) AS last_message_ts "
            "  FROM messages "
            "  GROUP BY session_id"
            ") msg ON msg.session_id = s.id "
            "ORDER BY "
            "  COALESCE(NULLIF(s.last_active_at, ''), NULLIF(msg.last_message_ts, ''), NULLIF(s.created_at, '')) DESC, "
            "  s.id DESC"))) {
        result.warnings.append(
            QStringLiteral("会话查询失败: %1").arg(q.lastError().text()));
        return result;
    }

    while (q.next()) {
        SessionInfo info;
        info.sessionId = q.value(0).toString();
        info.agentId = q.value(1).toString();
        info.title = q.value(2).toString();
        info.createdAt = parseTimestamp(q.value(3).toString());

        const QDateTime lastActive = parseTimestamp(q.value(4).toString());
        const QDateTime lastMessage = parseTimestamp(q.value(6).toString());
        info.lastModified = lastActive.isValid() ? lastActive : lastMessage;
        info.messageCount = q.value(5).toLongLong();
        info.fileSizeBytes = 0;

        result.sessions.append(info);
    }

    return result;
}

QString formatTable(const ListResult& result)
{
    QStringList lines;

    if (!result.warnings.isEmpty()) {
        for (const QString& w : result.warnings)
            lines << QStringLiteral("WARNING: %1").arg(w);
        lines << QString();
    }

    if (result.sessions.isEmpty()) {
        lines << QStringLiteral("(no sessions found)");
        return lines.join(QLatin1Char('\n'));
    }

    lines << QStringLiteral("%1 %2 %3 %4 %5 %6")
                 .arg(padRight(QStringLiteral("SESSION_ID"), 38),
                      padRight(QStringLiteral("AGENT"), 12),
                      padRight(QStringLiteral("MSGS"), 6),
                      padRight(QStringLiteral("SIZE"), 9),
                      padRight(QStringLiteral("CREATED"), 20),
                      QStringLiteral("LAST_ACTIVE"));

    for (const SessionInfo& s : result.sessions) {
        const QString created = s.createdAt.isValid()
            ? s.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm"))
            : QStringLiteral("-");
        const QString lastActive = s.lastModified.isValid()
            ? s.lastModified.toLocalTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm"))
            : QStringLiteral("-");

        lines << QStringLiteral("%1 %2 %3 %4 %5 %6")
                     .arg(padRight(clipId(s.sessionId, 38), 38),
                          padRight(s.agentId.isEmpty() ? QStringLiteral("-") : clipId(s.agentId, 12), 12),
                          padRight(QString::number(s.messageCount), 6),
                          padRight(humanReadableSize(s.fileSizeBytes), 9),
                          padRight(created, 20),
                          lastActive);
    }

    lines << QString();
    lines << QStringLiteral("Total: %1 session(s)").arg(result.sessions.size());
    return lines.join(QLatin1Char('\n'));
}

QString formatJson(const ListResult& result)
{
    QJsonObject root;

    QJsonArray warnings;
    for (const QString& w : result.warnings)
        warnings.append(w);
    root.insert(QStringLiteral("warnings"), warnings);

    QJsonArray sessions;
    for (const SessionInfo& s : result.sessions) {
        QJsonObject obj;
        obj.insert(QStringLiteral("session_id"), s.sessionId);
        obj.insert(QStringLiteral("agent_id"), s.agentId);
        obj.insert(QStringLiteral("title"), s.title);
        if (s.createdAt.isValid())
            obj.insert(QStringLiteral("created_at"), s.createdAt.toUTC().toString(Qt::ISODateWithMs));
        if (s.lastModified.isValid())
            obj.insert(QStringLiteral("last_modified"), s.lastModified.toUTC().toString(Qt::ISODateWithMs));
        obj.insert(QStringLiteral("message_count"), s.messageCount);
        obj.insert(QStringLiteral("file_size_bytes"), s.fileSizeBytes);
        sessions.append(obj);
    }
    root.insert(QStringLiteral("sessions"), sessions);
    root.insert(QStringLiteral("count"), result.sessions.size());

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace LogSessionLister
