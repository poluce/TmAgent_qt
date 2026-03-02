#include "LogDbScanner.h"

#include "LogFieldExtractor.h"
#include "LogDbUtils.h"
#include "LogScanner.h"
#include "LogSummarizer.h"

#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtGlobal>

namespace {

QDateTime parseIsoDateTime(const QString& value)
{
    if (value.trimmed().isEmpty())
        return QDateTime();

    QDateTime dt = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(value, Qt::ISODate);
    if (dt.isValid() && dt.timeSpec() == Qt::LocalTime)
        dt = dt.toUTC();
    return dt;
}

void appendEqIgnoreCase(QStringList* where, QList<QVariant>* binds, const QString& field, const QString& value)
{
    if (!where || !binds)
        return;
    if (value.isEmpty())
        return;
    where->append(QStringLiteral("LOWER(%1) = LOWER(?)").arg(field));
    binds->append(value);
}

QString buildWhereClause(const QStringList& where)
{
    if (where.isEmpty())
        return QString();
    return QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
}

} // namespace

namespace LogDbScanner {

QVector<LogQueryEngine::Hit> queryEvents(const LogQueryEngine::Query& query,
                                         LogQueryEngine::Result* result)
{
    QVector<LogQueryEngine::Hit> hits;

    QString dbError;
    QSqlDatabase db = LogDbUtils::openConnection(query.dataRootPath, &dbError);
    if (!db.isValid() || !db.isOpen()) {
        if (result)
            result->warnings.append(QStringLiteral("SQLite 连接不可用，无法查询 events: %1").arg(dbError));
        return hits;
    }

    QStringList where;
    QList<QVariant> binds;

    appendEqIgnoreCase(&where, &binds, QStringLiteral("session_id"), query.sessionId);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("trace_id"), query.traceId);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("turn_id"), query.turnId);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("run_id"), query.runId);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("actor_id"), query.actorId);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("tool_name"), query.toolName);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("event_type"), query.eventType);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("level"), query.level);

    if (!query.requestId.isEmpty()) {
        where.append(QStringLiteral("(LOWER(request_id) = LOWER(?) OR LOWER(raw_json) LIKE LOWER(?))"));
        binds.append(query.requestId);
        binds.append(QStringLiteral("%") + query.requestId + QStringLiteral("%"));
    }

    if (!query.toolCallId.isEmpty()) {
        where.append(QStringLiteral("(LOWER(tool_call_id) = LOWER(?) OR LOWER(raw_json) LIKE LOWER(?))"));
        binds.append(query.toolCallId);
        binds.append(QStringLiteral("%") + query.toolCallId + QStringLiteral("%"));
    }

    if (query.timeFrom.isValid()) {
        where.append(QStringLiteral("timestamp_ms >= ?"));
        binds.append(query.timeFrom.toUTC().toMSecsSinceEpoch());
    }
    if (query.timeTo.isValid()) {
        where.append(QStringLiteral("timestamp_ms <= ?"));
        binds.append(query.timeTo.toUTC().toMSecsSinceEpoch());
    }

    if (query.minDurationMs >= 0) {
        where.append(QStringLiteral("(duration_ms IS NULL OR duration_ms >= ?)"));
        binds.append(query.minDurationMs);
    }
    if (query.maxDurationMs >= 0) {
        where.append(QStringLiteral("(duration_ms IS NULL OR duration_ms <= ?)"));
        binds.append(query.maxDurationMs);
    }

    if (!query.keyword.isEmpty()) {
        QStringList fields;
        fields << QStringLiteral("summary")
               << QStringLiteral("event_type")
               << QStringLiteral("tool_name")
               << QStringLiteral("session_id")
               << QStringLiteral("trace_id")
               << QStringLiteral("turn_id")
               << QStringLiteral("run_id")
               << QStringLiteral("request_id")
               << QStringLiteral("tool_call_id")
               << QStringLiteral("actor_id")
               << QStringLiteral("raw_json");

        QStringList ors;
        for (const QString& field : fields)
            ors.append(QStringLiteral("LOWER(%1) LIKE LOWER(?)").arg(field));

        where.append(QStringLiteral("(") + ors.join(QStringLiteral(" OR ")) + QStringLiteral(")"));
        for (int i = 0; i < fields.size(); ++i)
            binds.append(QStringLiteral("%") + query.keyword + QStringLiteral("%"));
    }

    const QString sql = QStringLiteral(
                            "SELECT id, timestamp, timestamp_ms, session_id, trace_id, turn_id, run_id, request_id, "
                            "tool_call_id, actor_id, tool_name, event_type, level, duration_ms, success, summary, raw_json "
                            "FROM events")
        + buildWhereClause(where)
        + QStringLiteral(" ORDER BY timestamp_ms ")
        + (query.ascending ? QStringLiteral("ASC") : QStringLiteral("DESC"))
        + QStringLiteral(", id ")
        + (query.ascending ? QStringLiteral("ASC") : QStringLiteral("DESC"));

    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant& bind : binds)
        q.addBindValue(bind);

    if (!q.exec()) {
        if (result) {
            result->warnings.append(
                QStringLiteral("events 查询失败: %1").arg(q.lastError().text()));
        }
        return hits;
    }

    if (result)
        ++result->scannedFiles;

    while (q.next()) {
        if (result)
            ++result->scannedLines;

        LogQueryEngine::Hit hit;
        hit.source = QStringLiteral("event");
        hit.filePath = QStringLiteral("sqlite://events");
        hit.lineNo = q.value(0).toInt();
        hit.timestampMs = q.value(2).toLongLong();
        hit.timestamp = parseIsoDateTime(q.value(1).toString());
        if (!hit.timestamp.isValid() && hit.timestampMs >= 0)
            hit.timestamp = QDateTime::fromMSecsSinceEpoch(hit.timestampMs, Qt::UTC);

        hit.sessionId = q.value(3).toString();
        hit.traceId = q.value(4).toString();
        hit.turnId = q.value(5).toString();
        hit.runId = q.value(6).toString();
        hit.requestId = q.value(7).toString();
        hit.toolCallId = q.value(8).toString();
        hit.actorId = q.value(9).toString();
        hit.toolName = q.value(10).toString();
        hit.eventType = q.value(11).toString();
        hit.level = q.value(12).toString();

        hit.durationMs = q.value(13).isNull() ? -1 : q.value(13).toLongLong();
        hit.successKnown = !q.value(14).isNull();
        hit.success = hit.successKnown ? (q.value(14).toInt() != 0) : false;
        hit.summary = q.value(15).toString();

        const QString rawJson = q.value(16).toString();
        const QString rawCompactLower = rawJson.toLower();

        if (hit.summary.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument rawDoc = QJsonDocument::fromJson(rawJson.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && rawDoc.isObject())
                hit.summary = LogSummarizer::summarizeEvent(rawDoc.object());
        }

        if (query.includeRaw) {
            QJsonParseError parseError;
            const QJsonDocument rawDoc = QJsonDocument::fromJson(rawJson.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && rawDoc.isObject())
                hit.raw = rawDoc.object();
        }

        if (!LogScanner::withinTimeRange(hit.timestamp, query))
            continue;
        if (!LogScanner::hitMatches(hit, query, rawCompactLower))
            continue;

        hits.append(hit);
    }

    return hits;
}

QVector<LogQueryEngine::Hit> queryMessages(const LogQueryEngine::Query& query,
                                           LogQueryEngine::Result* result)
{
    QVector<LogQueryEngine::Hit> hits;

    QString dbError;
    QSqlDatabase db = LogDbUtils::openConnection(query.dataRootPath, &dbError);
    if (!db.isValid() || !db.isOpen()) {
        if (result)
            result->warnings.append(QStringLiteral("SQLite 连接不可用，无法查询 messages: %1").arg(dbError));
        return hits;
    }

    QStringList where;
    QList<QVariant> binds;

    appendEqIgnoreCase(&where, &binds, QStringLiteral("session_id"), query.sessionId);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("trace_id"), query.traceId);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("turn_id"), query.turnId);
    appendEqIgnoreCase(&where, &binds, QStringLiteral("sender_id"), query.actorId);

    const QString sql = QStringLiteral(
                            "SELECT rowid, id, session_id, trace_id, turn_id, sender_id, "
                            "content_type, content_text, content_payload, timestamp, status, source "
                            "FROM messages")
        + buildWhereClause(where)
        + QStringLiteral(" ORDER BY timestamp ")
        + (query.ascending ? QStringLiteral("ASC") : QStringLiteral("DESC"))
        + QStringLiteral(", rowid ")
        + (query.ascending ? QStringLiteral("ASC") : QStringLiteral("DESC"));

    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant& bind : binds)
        q.addBindValue(bind);

    if (!q.exec()) {
        if (result) {
            result->warnings.append(
                QStringLiteral("messages 查询失败: %1").arg(q.lastError().text()));
        }
        return hits;
    }

    if (result)
        ++result->scannedFiles;

    while (q.next()) {
        if (result)
            ++result->scannedLines;

        QJsonObject payloadObj;
        const QString payloadText = q.value(8).toString();
        if (!payloadText.trimmed().isEmpty()) {
            QJsonParseError payloadError;
            const QJsonDocument payloadDoc = QJsonDocument::fromJson(payloadText.toUtf8(), &payloadError);
            if (payloadError.error == QJsonParseError::NoError && payloadDoc.isObject())
                payloadObj = payloadDoc.object();
        }

        QJsonObject contentObj;
        contentObj.insert(QStringLiteral("type"), q.value(6).toString());
        contentObj.insert(QStringLiteral("text"), q.value(7).toString());
        contentObj.insert(QStringLiteral("payload"), payloadObj);

        QJsonObject rawObj;
        rawObj.insert(QStringLiteral("id"), q.value(1).toString());
        rawObj.insert(QStringLiteral("sessionId"), q.value(2).toString());
        rawObj.insert(QStringLiteral("traceId"), q.value(3).toString());
        rawObj.insert(QStringLiteral("turnId"), q.value(4).toString());
        rawObj.insert(QStringLiteral("senderId"), q.value(5).toString());
        rawObj.insert(QStringLiteral("content"), contentObj);
        rawObj.insert(QStringLiteral("timestamp"), q.value(9).toString());
        rawObj.insert(QStringLiteral("status"), q.value(10).toString());
        rawObj.insert(QStringLiteral("source"), q.value(11).toString());

        LogQueryEngine::Hit hit;
        hit.source = QStringLiteral("message");
        hit.filePath = QStringLiteral("sqlite://messages");
        hit.lineNo = q.value(0).toInt();
        hit.timestamp = parseIsoDateTime(q.value(9).toString());
        hit.timestampMs = LogScanner::toMs(hit.timestamp);
        hit.sessionId = q.value(2).toString();
        hit.traceId = q.value(3).toString();
        hit.turnId = q.value(4).toString();
        hit.runId = LogFields::stringField(payloadObj, QStringLiteral("run_id"));
        hit.requestId = LogFields::extractRequestId(rawObj, false);
        hit.toolCallId = LogFields::extractToolCallId(rawObj, false);
        hit.actorId = q.value(5).toString();
        hit.toolName = LogFields::extractToolName(rawObj, false);
        hit.eventType = LogFields::extractEventType(rawObj, false);
        hit.summary = LogSummarizer::summarizeMessage(rawObj);
        LogFields::extractSuccess(rawObj, false, &hit.successKnown, &hit.success);
        hit.level = LogFields::extractLevel(rawObj, false);

        const QJsonValue durationMs = payloadObj.value(QStringLiteral("duration_ms"));
        if (durationMs.isDouble())
            hit.durationMs = static_cast<qint64>(durationMs.toDouble());
        const QJsonValue durationCamel = payloadObj.value(QStringLiteral("durationMs"));
        if (hit.durationMs < 0 && durationCamel.isDouble())
            hit.durationMs = static_cast<qint64>(durationCamel.toDouble());

        if (query.includeRaw)
            hit.raw = rawObj;

        if (!LogScanner::withinTimeRange(hit.timestamp, query))
            continue;

        const QString rawCompactLower = QString::fromUtf8(
                                            QJsonDocument(rawObj).toJson(QJsonDocument::Compact))
                                            .toLower();
        if (!LogScanner::hitMatches(hit, query, rawCompactLower))
            continue;

        hits.append(hit);
    }

    return hits;
}

} // namespace LogDbScanner
