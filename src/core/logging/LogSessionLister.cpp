#include "LogSessionLister.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

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

qint64 countLines(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return 0;

    qint64 count = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (!line.isEmpty())
            ++count;
    }
    return count;
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

} // namespace

ListResult listSessions(const QString& dataRootPath)
{
    ListResult result;

    const QString rootPath = dataRootPath.isEmpty()
        ? QDir::home().filePath(QStringLiteral(".tmagent"))
        : dataRootPath;

    const QString sessionsDataPath = QDir(rootPath).filePath(QStringLiteral("sessions/data"));
    QDir sessionsDir(sessionsDataPath);

    if (!sessionsDir.exists()) {
        result.warnings.append(
            QStringLiteral("会话目录不存在: %1")
                .arg(QDir::toNativeSeparators(sessionsDataPath)));
        return result;
    }

    const QStringList sessionDirs = sessionsDir.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QString& sessionId : sessionDirs) {
        const QString sessionPath = sessionsDir.filePath(sessionId);
        const QDir sessionDir(sessionPath);

        SessionInfo info;
        info.sessionId = sessionId;

        // 读取 meta.json
        const QString metaPath = sessionDir.filePath(QStringLiteral("meta.json"));
        QFile metaFile(metaPath);
        if (metaFile.open(QFile::ReadOnly | QFile::Text)) {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject meta = doc.object();
                info.agentId = meta.value(QStringLiteral("agentId")).toString().trimmed();
                if (info.agentId.isEmpty())
                    info.agentId = meta.value(QStringLiteral("agent_id")).toString().trimmed();
                info.title = meta.value(QStringLiteral("title")).toString().trimmed();

                const QString createdStr = meta.value(QStringLiteral("createdAt")).toString().trimmed();
                if (!createdStr.isEmpty()) {
                    info.createdAt = QDateTime::fromString(createdStr, Qt::ISODateWithMs);
                    if (!info.createdAt.isValid())
                        info.createdAt = QDateTime::fromString(createdStr, Qt::ISODate);
                }
            } else {
                result.warnings.append(
                    QStringLiteral("无法解析 meta.json: %1")
                        .arg(QDir::toNativeSeparators(metaPath)));
            }
            metaFile.close();
        }

        // 统计 messages.jsonl
        const QString messagesPath = sessionDir.filePath(QStringLiteral("messages.jsonl"));
        QFileInfo messagesInfo(messagesPath);
        if (messagesInfo.exists()) {
            info.fileSizeBytes = messagesInfo.size();
            info.messageCount = countLines(messagesPath);
            info.lastModified = messagesInfo.lastModified();
        } else {
            info.lastModified = QFileInfo(sessionPath).lastModified();
        }

        // 如果 createdAt 未从 meta 获取，回退到目录创建时间
        if (!info.createdAt.isValid())
            info.createdAt = QFileInfo(sessionPath).birthTime();

        result.sessions.append(info);
    }

    // 按 lastModified 降序排序
    std::sort(result.sessions.begin(), result.sessions.end(),
        [](const SessionInfo& a, const SessionInfo& b) {
            return a.lastModified > b.lastModified;
        });

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

    // 表头
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
