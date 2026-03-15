#include "SessionSearchTool.h"

#include "core/persistence/DatabaseManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <algorithm>

namespace {

QDateTime parseStoredTimestamp(const QString& raw)
{
    QDateTime dt = QDateTime::fromString(raw.trimmed(), Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(raw.trimmed(), Qt::ISODate);
    return dt;
}

QJsonObject parsePayloadObject(const QString& raw)
{
    if (raw.trimmed().isEmpty())
        return QJsonObject();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

QString searchableTextFromDb(const QString& contentType,
                             const QString& contentText,
                             const QString& contentPayload,
                             bool includeToolMessages)
{
    const QString type = contentType.trimmed().toLower();
    if (type == QLatin1String("text") || type == QLatin1String("system"))
        return contentText;

    if (!includeToolMessages)
        return QString();

    const QJsonObject payloadObj = parsePayloadObject(contentPayload);
    if (type == QLatin1String("tool_result")) {
        QString text = contentText;
        if (text.trimmed().isEmpty())
            text = payloadObj.value(QStringLiteral("raw_result")).toString();
        return text;
    }

    if (type == QLatin1String("tool_call"))
        return payloadObj.value(QStringLiteral("tool_name")).toString();

    return QString();
}

}

QString SessionSearchTool::executeSearch(const QJsonObject& args)
{
    const QString query = args.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty())
        return QStringLiteral("错误: query 不能为空");

    const QString scope = normalizeScope(args);
    const QString agentId = resolveAgentId(args);
    const QString targetSessionId = args.value(QStringLiteral("session_id")).toString().trimmed();
    const bool includeToolMessages = args.value(QStringLiteral("include_tool_messages")).toBool(false);
    const int maxResults = qBound(1, args.value(QStringLiteral("max_results")).toInt(20), 200);
    const int maxSnippetChars = qBound(60, args.value(QStringLiteral("max_snippet_chars")).toInt(220), 500);

    QStringList targetSessions;
    QString resolveError;
    const bool useDbAsPrimary = DatabaseManager::instance()->isReady();
    if (useDbAsPrimary) {
        if (!resolveTargetSessionsFromDb(scope, agentId, targetSessionId, &targetSessions, &resolveError))
            return resolveError;
    } else {
        const QString root = dataRootPath();
        const QString sessionsDataDir = QDir(root).filePath(QStringLiteral("sessions/data"));
        QDir dataDir(sessionsDataDir);
        if (!dataDir.exists()) {
            return QStringLiteral("错误: 会话目录不存在: %1")
                .arg(QDir::toNativeSeparators(sessionsDataDir));
        }

        if (!resolveTargetSessions(sessionsDataDir, scope, agentId, targetSessionId, &targetSessions, &resolveError))
            return resolveError;
    }

    QVector<Hit> hits;
    int sessionsScanned = 0;
    for (const QString& sessionId : targetSessions) {
        if (hits.size() >= maxResults)
            break;
        ++sessionsScanned;

        QVector<Hit> sessionHits;
        if (useDbAsPrimary) {
            sessionHits = scanSessionMessagesFromDb(sessionId, query, includeToolMessages, maxSnippetChars);
        } else {
            const QString root = dataRootPath();
            const QString sessionsDataDir = QDir(root).filePath(QStringLiteral("sessions/data"));
            const QString path = QDir(QDir(sessionsDataDir).filePath(sessionId)).filePath(QStringLiteral("messages.jsonl"));
            sessionHits = scanSessionMessages(sessionId, path, query, includeToolMessages, maxSnippetChars);
        }
        for (const Hit& hit : sessionHits) {
            hits.append(hit);
            if (hits.size() >= maxResults)
                break;
        }
    }

    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.timestampMs == b.timestampMs)
            return a.seqNo > b.seqNo;
        return a.timestampMs > b.timestampMs;
    });
    if (hits.size() > maxResults)
        hits.resize(maxResults);

    QString output;
    output += QStringLiteral("session_search query: %1\n").arg(query);
    output += QStringLiteral("scope: %1\n").arg(scope);
    if (!agentId.isEmpty())
        output += QStringLiteral("agent_id: %1\n").arg(agentId);
    if (!targetSessionId.isEmpty())
        output += QStringLiteral("session_id: %1\n").arg(targetSessionId);
    output += QStringLiteral("sessions_scanned: %1\n").arg(sessionsScanned);
    output += QStringLiteral("results: %1\n").arg(hits.size());

    if (hits.isEmpty()) {
        output += QStringLiteral("未在会话历史中找到匹配内容。");
        return output;
    }

    QStringList lines;
    for (const Hit& hit : hits) {
        const QString ts = hit.timestamp.isValid()
            ? hit.timestamp.toString(Qt::ISODateWithMs)
            : QStringLiteral("(unknown-time)");
        lines.append(QStringLiteral("- [%1] %2 sender=%3 msg=%4 turn=%5 trace=%6\n  %7")
                         .arg(hit.sessionId, ts, hit.senderId.isEmpty() ? QStringLiteral("(unknown)") : hit.senderId, hit.messageId.isEmpty() ? QStringLiteral("(unknown)") : hit.messageId, hit.turnId.isEmpty() ? QStringLiteral("(none)") : hit.turnId, hit.traceId.isEmpty() ? QStringLiteral("(none)") : hit.traceId, hit.snippet));
    }
    output += QStringLiteral("\n命中列表:\n");
    output += lines.join(QStringLiteral("\n"));
    return output;
}

QString SessionSearchTool::dataRootPath()
{
    return QDir::home().filePath(QStringLiteral(".tmagent"));
}

QString SessionSearchTool::normalizeScope(const QJsonObject& args)
{
    const QString scope = args.value(QStringLiteral("scope")).toString().trimmed().toLower();
    if (scope.isEmpty())
        return QStringLiteral("self");
    return scope;
}

QString SessionSearchTool::resolveAgentId(const QJsonObject& args)
{
    QString id = args.value(QStringLiteral("agent_id")).toString().trimmed();
    if (id.isEmpty())
        id = args.value(QStringLiteral("_agent_id")).toString().trimmed();
    return id;
}

bool SessionSearchTool::resolveTargetSessionsFromDb(const QString& scope,
                                                    const QString& agentId,
                                                    const QString& targetSessionId,
                                                    QStringList* targetSessions,
                                                    QString* error)
{
    if (targetSessions)
        targetSessions->clear();
    if (error)
        error->clear();

    if (!DatabaseManager::instance()->isReady()) {
        if (error)
            *error = QStringLiteral("错误: SQLite 未初始化");
        return false;
    }

    QSqlDatabase db = DatabaseManager::instance()->connection();
    if (!db.isOpen()) {
        if (error)
            *error = QStringLiteral("错误: SQLite 连接不可用");
        return false;
    }

    if (!targetSessionId.isEmpty()) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT 1 FROM sessions WHERE id = ? LIMIT 1"));
        q.addBindValue(targetSessionId);
        if (!q.exec() || !q.next()) {
            if (error)
                *error = QStringLiteral("错误: 指定 session_id 不存在: %1").arg(targetSessionId);
            return false;
        }
        if (targetSessions)
            targetSessions->append(targetSessionId);
        return true;
    }

    if (scope != QLatin1String("all") && scope != QLatin1String("self")) {
        if (error)
            *error = QStringLiteral("错误: 不支持的 scope: %1（可用: self/all）").arg(scope);
        return false;
    }

    if (scope == QLatin1String("self") && agentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("错误: scope=self 时需要 agent_id（或系统注入 _agent_id）");
        return false;
    }

    QSqlQuery q(db);
    if (scope == QLatin1String("all")) {
        q.prepare(QStringLiteral(
            "SELECT id FROM sessions ORDER BY last_active_at DESC, created_at DESC, id DESC"));
    } else {
        q.prepare(QStringLiteral(
            "SELECT DISTINCT s.id "
            "FROM sessions s "
            "LEFT JOIN session_participants sp ON sp.session_id = s.id "
            "WHERE sp.identity_id = ? OR s.owner_id = ? "
            "ORDER BY s.last_active_at DESC, s.created_at DESC, s.id DESC"));
        q.addBindValue(agentId);
        q.addBindValue(agentId);
    }

    if (!q.exec()) {
        if (error)
            *error = QStringLiteral("错误: 会话查询失败: %1").arg(q.lastError().text());
        return false;
    }

    QStringList sessions;
    while (q.next()) {
        const QString sessionId = q.value(0).toString().trimmed();
        if (!sessionId.isEmpty())
            sessions.append(sessionId);
    }
    sessions.removeDuplicates();
    if (targetSessions)
        *targetSessions = sessions;
    return true;
}

bool SessionSearchTool::resolveTargetSessions(const QString& sessionsDataDir, const QString& scope, const QString& agentId, const QString& targetSessionId, QStringList* targetSessions, QString* error)
{
    if (targetSessions)
        targetSessions->clear();
    if (error)
        error->clear();

    QDir dataDir(sessionsDataDir);
    if (!dataDir.exists()) {
        if (error)
            *error = QStringLiteral("错误: sessions/data 不存在");
        return false;
    }

    if (!targetSessionId.isEmpty()) {
        if (!QDir(dataDir.filePath(targetSessionId)).exists()) {
            if (error)
                *error = QStringLiteral("错误: 指定 session_id 不存在: %1").arg(targetSessionId);
            return false;
        }
        if (targetSessions)
            targetSessions->append(targetSessionId);
        return true;
    }

    const QStringList allSessionIds = dataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (scope == QLatin1String("all")) {
        if (targetSessions)
            *targetSessions = allSessionIds;
        return true;
    }

    if (scope != QLatin1String("self")) {
        if (error)
            *error = QStringLiteral("错误: 不支持的 scope: %1（可用: self/all）").arg(scope);
        return false;
    }

    if (agentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("错误: scope=self 时需要 agent_id（或系统注入 _agent_id）");
        return false;
    }

    QStringList filtered;
    for (const QString& sid : allSessionIds) {
        if (sessionContainsAgent(sessionsDataDir, sid, agentId))
            filtered.append(sid);
    }
    if (targetSessions)
        *targetSessions = filtered;
    return true;
}

QVector<SessionSearchTool::Hit> SessionSearchTool::scanSessionMessagesFromDb(const QString& sessionId,
                                                                             const QString& query,
                                                                             bool includeToolMessages,
                                                                             int maxSnippetChars)
{
    QVector<Hit> hits;
    if (!DatabaseManager::instance()->isReady())
        return hits;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    if (!db.isOpen())
        return hits;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, sender_id, turn_id, trace_id, content_type, content_text, content_payload, "
        "timestamp, status, seq "
        "FROM messages WHERE session_id = ? ORDER BY timestamp ASC, rowid ASC"));
    q.addBindValue(sessionId);
    if (!q.exec())
        return hits;

    const QString queryLower = query.toLower();
    int fallbackSeq = 0;
    while (q.next()) {
        ++fallbackSeq;

        const QString status = q.value(8).toString().trimmed().toLower();
        if (status == QLatin1String("cancelled")
            || status == QLatin1String("interrupted")
            || status == QLatin1String("error")) {
            continue;
        }

        QString searchable = searchableTextFromDb(
            q.value(4).toString(),
            q.value(5).toString(),
            q.value(6).toString(),
            includeToolMessages);
        searchable = searchable.simplified();
        if (searchable.isEmpty() || !searchable.toLower().contains(queryLower))
            continue;

        Hit hit;
        hit.sessionId = sessionId;
        hit.messageId = q.value(0).toString().trimmed();
        hit.senderId = q.value(1).toString().trimmed();
        hit.turnId = q.value(2).toString().trimmed();
        hit.traceId = q.value(3).toString().trimmed();
        hit.snippet = clipSnippet(searchable, maxSnippetChars);
        hit.timestamp = parseStoredTimestamp(q.value(7).toString());
        hit.timestampMs = hit.timestamp.isValid() ? hit.timestamp.toMSecsSinceEpoch() : -1;
        hit.seqNo = q.value(9).toInt();
        if (hit.seqNo <= 0)
            hit.seqNo = fallbackSeq;
        hits.append(hit);
    }

    return hits;
}

bool SessionSearchTool::sessionContainsAgent(const QString& sessionsDataDir, const QString& sessionId, const QString& agentId)
{
    const QString sessionDir = QDir(sessionsDataDir).filePath(sessionId);
    const QString metaPath = QDir(sessionDir).filePath(QStringLiteral("meta.json"));
    const QString messagesPath = QDir(sessionDir).filePath(QStringLiteral("messages.jsonl"));

    QFile metaFile(metaPath);
    if (metaFile.open(QFile::ReadOnly | QFile::Text)) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &err);
        metaFile.close();
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject obj = doc.object();
            auto containsIdInArray = [&agentId](const QJsonArray& arr) {
                for (const QJsonValue& v : arr) {
                    if (v.toString().trimmed() == agentId)
                        return true;
                }
                return false;
            };
            if (containsIdInArray(obj.value(QStringLiteral("participants")).toArray()))
                return true;
            if (containsIdInArray(obj.value(QStringLiteral("participantIds")).toArray()))
                return true;
            if (obj.value(QStringLiteral("ownerId")).toString().trimmed() == agentId)
                return true;
        }
    }

    QFile msgFile(messagesPath);
    if (!msgFile.open(QFile::ReadOnly | QFile::Text))
        return false;
    while (!msgFile.atEnd()) {
        const QByteArray raw = msgFile.readLine().trimmed();
        if (raw.isEmpty())
            continue;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        if (doc.object().value(QStringLiteral("senderId")).toString().trimmed() == agentId) {
            msgFile.close();
            return true;
        }
    }
    msgFile.close();
    return false;
}

QVector<SessionSearchTool::Hit> SessionSearchTool::scanSessionMessages(const QString& sessionId, const QString& messagesPath, const QString& query, bool includeToolMessages, int maxSnippetChars)
{
    QVector<Hit> hits;
    QFile file(messagesPath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return hits;

    const QString queryLower = query.toLower();
    int seq = 0;
    while (!file.atEnd()) {
        ++seq;
        const QByteArray raw = file.readLine().trimmed();
        if (raw.isEmpty())
            continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        const QJsonObject msg = doc.object();

        const QString status = msg.value(QStringLiteral("status")).toString().trimmed().toLower();
        if (status == QLatin1String("cancelled")
            || status == QLatin1String("interrupted")
            || status == QLatin1String("error")) {
            continue;
        }

        const QJsonObject contentObj = msg.value(QStringLiteral("content")).toObject();
        const QString type = contentObj.value(QStringLiteral("type")).toString().trimmed().toLower();
        QString searchable;
        if (type == QLatin1String("text") || type == QLatin1String("system")) {
            searchable = contentObj.value(QStringLiteral("text")).toString();
        } else if (includeToolMessages && type == QLatin1String("tool_result")) {
            searchable = contentObj.value(QStringLiteral("text")).toString();
            if (searchable.trimmed().isEmpty())
                searchable = contentObj.value(QStringLiteral("payload"))
                                 .toObject()
                                 .value(QStringLiteral("raw_result"))
                                 .toString();
        } else if (includeToolMessages && type == QLatin1String("tool_call")) {
            searchable = contentObj.value(QStringLiteral("payload"))
                             .toObject()
                             .value(QStringLiteral("tool_name"))
                             .toString();
        } else {
            continue;
        }

        searchable = searchable.simplified();
        if (searchable.isEmpty() || !searchable.toLower().contains(queryLower))
            continue;

        Hit hit;
        hit.sessionId = sessionId;
        hit.messageId = msg.value(QStringLiteral("id")).toString().trimmed();
        hit.senderId = msg.value(QStringLiteral("senderId")).toString().trimmed();
        hit.turnId = msg.value(QStringLiteral("turnId")).toString().trimmed();
        hit.traceId = msg.value(QStringLiteral("traceId")).toString().trimmed();
        hit.seqNo = seq;
        hit.snippet = clipSnippet(searchable, maxSnippetChars);
        hit.timestamp = QDateTime::fromString(
            msg.value(QStringLiteral("timestamp")).toString().trimmed(),
            Qt::ISODateWithMs);
        if (!hit.timestamp.isValid()) {
            hit.timestamp = QDateTime::fromString(
                msg.value(QStringLiteral("timestamp")).toString().trimmed(),
                Qt::ISODate);
        }
        hit.timestampMs = hit.timestamp.isValid() ? hit.timestamp.toMSecsSinceEpoch() : -1;
        hits.append(hit);
    }
    file.close();
    return hits;
}

QString SessionSearchTool::clipSnippet(const QString& text, int maxChars)
{
    const QString simplified = text.simplified();
    if (simplified.size() <= maxChars)
        return simplified;
    return simplified.left(maxChars) + QStringLiteral("...");
}
