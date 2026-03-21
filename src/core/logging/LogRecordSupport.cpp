#include "LogRecordSupport.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QThread>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>

namespace {

QString connectionNameFor(const QString& dataRootPath)
{
    const QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThread()));
    return QStringLiteral("tmagent_log_") + threadId + QStringLiteral("_")
        + QString::number(qHash(dataRootPath));
}

QString stringField(const QJsonObject& obj, const QString& key)
{
    return obj.value(key).toString().trimmed();
}

QString stringFieldAny(const QJsonObject& obj, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString value = stringField(obj, key);
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

QDateTime parseTimestampFromObject(const QJsonObject& obj)
{
    const QString ts = stringFieldAny(obj, QStringList()
                                               << QStringLiteral("timestamp")
                                               << QStringLiteral("time")
                                               << QStringLiteral("createdAt")
                                               << QStringLiteral("created_at"));
    if (ts.isEmpty())
        return QDateTime();

    QDateTime dt = QDateTime::fromString(ts, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(ts, Qt::ISODate);
    if (dt.isValid() && dt.timeSpec() == Qt::LocalTime)
        dt = dt.toUTC();
    return dt;
}

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

QString extractEventType(const QJsonObject& obj, bool isEventSource)
{
    if (isEventSource) {
        QString type = stringField(obj, QStringLiteral("type"));
        if (!type.isEmpty())
            return type;
        type = stringField(obj.value(QStringLiteral("event")).toObject(), QStringLiteral("type"));
        return type;
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    return stringField(contentObj, QStringLiteral("type"));
}

QString extractToolName(const QJsonObject& obj, bool isEventSource)
{
    if (isEventSource) {
        QString value = stringFieldAny(obj, QStringList()
                                                << QStringLiteral("tool_name")
                                                << QStringLiteral("toolName"));
        if (!value.isEmpty())
            return value;
        const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
        value = stringFieldAny(toolEventObj, QStringList()
                                               << QStringLiteral("toolName")
                                               << QStringLiteral("tool_name"));
        if (!value.isEmpty())
            return value;
        return stringField(toolEventObj.value(QStringLiteral("data")).toObject(),
                           QStringLiteral("tool_name"));
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    QString value = stringField(payloadObj, QStringLiteral("tool_name"));
    if (!value.isEmpty())
        return value;
    value = stringField(payloadObj.value(QStringLiteral("event_data")).toObject(),
                        QStringLiteral("tool_name"));
    return value;
}

QString extractToolCallId(const QJsonObject& obj, bool isEventSource)
{
    if (isEventSource) {
        QString value = stringField(obj, QStringLiteral("tool_call_id"));
        if (!value.isEmpty())
            return value;
        value = stringField(obj, QStringLiteral("toolId"));
        if (!value.isEmpty())
            return value;
        const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
        value = stringField(toolEventObj, QStringLiteral("toolId"));
        if (!value.isEmpty())
            return value;
        return stringField(toolEventObj.value(QStringLiteral("data")).toObject(),
                           QStringLiteral("tool_call_id"));
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    QString value = stringField(payloadObj, QStringLiteral("tool_call_id"));
    if (!value.isEmpty())
        return value;
    return stringField(payloadObj, QStringLiteral("id"));
}

QString extractRequestId(const QJsonObject& obj, bool isEventSource)
{
    QString value = stringField(obj, QStringLiteral("request_id"));
    if (!value.isEmpty())
        return value;

    if (isEventSource) {
        const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
        value = stringField(toolEventObj.value(QStringLiteral("data")).toObject(),
                            QStringLiteral("child_request_id"));
        if (!value.isEmpty())
            return value;
        return stringField(obj, QStringLiteral("child_request_id"));
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    value = stringField(payloadObj, QStringLiteral("child_request_id"));
    if (!value.isEmpty())
        return value;
    return stringField(payloadObj.value(QStringLiteral("event_data")).toObject(),
                       QStringLiteral("child_request_id"));
}

QString extractActorId(const QJsonObject& obj, bool isEventSource)
{
    if (isEventSource) {
        QString value = stringFieldAny(obj, QStringList()
                                                << QStringLiteral("actorIdentityId")
                                                << QStringLiteral("actor_id"));
        if (!value.isEmpty())
            return value;
        const QJsonObject toolEventData = obj.value(QStringLiteral("toolEvent"))
                                              .toObject()
                                              .value(QStringLiteral("data"))
                                              .toObject();
        return stringField(toolEventData, QStringLiteral("_agent_id"));
    }
    return stringFieldAny(obj, QStringList() << QStringLiteral("senderId")
                                             << QStringLiteral("sender_id"));
}

bool extractSuccess(const QJsonObject& obj, bool isEventSource, bool* known, bool* value)
{
    if (known)
        *known = false;
    if (value)
        *value = false;

    auto assign = [known, value](bool v) {
        if (known)
            *known = true;
        if (value)
            *value = v;
    };

    if (isEventSource) {
        if (obj.contains(QStringLiteral("success")) && obj.value(QStringLiteral("success")).isBool()) {
            assign(obj.value(QStringLiteral("success")).toBool());
            return true;
        }
        const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
        if (toolEventObj.contains(QStringLiteral("success"))
            && toolEventObj.value(QStringLiteral("success")).isBool()) {
            assign(toolEventObj.value(QStringLiteral("success")).toBool());
            return true;
        }
        return false;
    }

    const QJsonObject payloadObj = obj.value(QStringLiteral("content"))
                                       .toObject()
                                       .value(QStringLiteral("payload"))
                                       .toObject();
    if (payloadObj.contains(QStringLiteral("success")) && payloadObj.value(QStringLiteral("success")).isBool()) {
        assign(payloadObj.value(QStringLiteral("success")).toBool());
        return true;
    }
    return false;
}

QString valueAtPath(const QJsonObject& obj, const QStringList& path)
{
    if (path.isEmpty())
        return QString();

    QJsonValue current(obj);
    for (const QString& segment : path) {
        if (!current.isObject())
            return QString();
        current = current.toObject().value(segment);
    }
    if (current.isString())
        return current.toString().trimmed();
    if (current.isDouble())
        return QString::number(current.toDouble());
    if (current.isBool())
        return current.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return QString();
}

QString extractLevel(const QJsonObject& obj, bool isEventSource)
{
    const QString level = stringFieldAny(obj, QStringList()
                                                  << QStringLiteral("level")
                                                  << QStringLiteral("severity")
                                                  << QStringLiteral("log_level"));
    if (!level.isEmpty())
        return level.toLower();

    const QString eventType = extractEventType(obj, isEventSource).toLower();
    if (eventType.contains(QStringLiteral("error")) || eventType.contains(QStringLiteral("failed")))
        return QStringLiteral("error");
    if (eventType.contains(QStringLiteral("warning")))
        return QStringLiteral("warning");
    return QStringLiteral("info");
}

qint64 extractDuration(const QJsonObject& obj)
{
    const QJsonValue durationMs = obj.value(QStringLiteral("duration_ms"));
    if (durationMs.isDouble())
        return static_cast<qint64>(durationMs.toDouble());

    const QJsonValue durationCamel = obj.value(QStringLiteral("durationMs"));
    if (durationCamel.isDouble())
        return static_cast<qint64>(durationCamel.toDouble());

    return -1;
}

QString summarizeEvent(const QJsonObject& obj)
{
    const QString type = extractEventType(obj, true);
    const QString toolName = extractToolName(obj, true);
    const QString error = stringField(obj, QStringLiteral("error"));
    const QString summary = stringField(obj, QStringLiteral("summary"));

    QString text;
    if (!type.isEmpty())
        text += type;
    if (!toolName.isEmpty()) {
        if (!text.isEmpty())
            text += QStringLiteral(" ");
        text += QStringLiteral("tool=%1").arg(toolName);
    }
    if (!summary.isEmpty()) {
        if (!text.isEmpty())
            text += QStringLiteral(" ");
        text += summary;
    } else {
        const QString toolEventStatus = valueAtPath(obj, QStringList()
                                                             << QStringLiteral("toolEvent")
                                                             << QStringLiteral("status"));
        if (!toolEventStatus.isEmpty()) {
            if (!text.isEmpty())
                text += QStringLiteral(" ");
            text += QStringLiteral("status=%1").arg(toolEventStatus);
        }
    }
    if (!error.isEmpty()) {
        if (!text.isEmpty())
            text += QStringLiteral(" ");
        text += QStringLiteral("error=%1").arg(error);
    }
    if (text.isEmpty())
        text = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    return LogRecordSupport::clip(text.simplified(), 220);
}

QString summarizeMessage(const QJsonObject& obj)
{
    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    const QString type = stringField(contentObj, QStringLiteral("type")).toLower();
    const QString sender = stringField(obj, QStringLiteral("senderId"));
    const QString text = stringField(contentObj, QStringLiteral("text"));
    const QString toolName = stringField(payloadObj, QStringLiteral("tool_name"));

    QString summary = QStringLiteral("sender=%1 type=%2")
                          .arg(sender.isEmpty() ? QStringLiteral("(unknown)") : sender,
                               type.isEmpty() ? QStringLiteral("(unknown)") : type);
    if (!toolName.isEmpty())
        summary += QStringLiteral(" tool=%1").arg(toolName);

    if (type == QLatin1String("text") || type == QLatin1String("system")) {
        if (!text.isEmpty())
            summary += QStringLiteral(" %1").arg(text);
    } else if (type == QLatin1String("tool_result")) {
        QString resultText = text;
        if (resultText.isEmpty())
            resultText = stringField(payloadObj, QStringLiteral("raw_result"));
        if (!resultText.isEmpty())
            summary += QStringLiteral(" %1").arg(resultText);
    } else if (!text.isEmpty()) {
        summary += QStringLiteral(" %1").arg(text);
    }
    return LogRecordSupport::clip(summary.simplified(), 220);
}

bool equalsIgnoreCase(const QString& a, const QString& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

QString buildWhereClause(const QStringList& where)
{
    if (where.isEmpty())
        return QString();
    return QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
}

void appendEqIgnoreCase(QStringList* where,
                        QList<QVariant>* binds,
                        const QString& field,
                        const QString& value)
{
    if (!where || !binds || value.isEmpty())
        return;
    where->append(QStringLiteral("LOWER(%1) = LOWER(?)").arg(field));
    binds->append(value);
}

QString outputFormatToString(LogQueryEngine::OutputFormat format)
{
    switch (format) {
    case LogQueryEngine::OutputFormat::Report:
        return QStringLiteral("report");
    case LogQueryEngine::OutputFormat::Table:
        return QStringLiteral("table");
    case LogQueryEngine::OutputFormat::Json:
        return QStringLiteral("json");
    case LogQueryEngine::OutputFormat::Raw:
        return QStringLiteral("raw");
    }
    return QStringLiteral("report");
}

QJsonObject queryToJson(const LogQueryEngine::Query& query)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("data_root"), query.dataRootPath);
    obj.insert(QStringLiteral("source"), query.source);
    obj.insert(QStringLiteral("session_id"), query.sessionId);
    obj.insert(QStringLiteral("trace_id"), query.traceId);
    obj.insert(QStringLiteral("turn_id"), query.turnId);
    obj.insert(QStringLiteral("run_id"), query.runId);
    obj.insert(QStringLiteral("request_id"), query.requestId);
    obj.insert(QStringLiteral("tool_call_id"), query.toolCallId);
    obj.insert(QStringLiteral("actor_id"), query.actorId);
    obj.insert(QStringLiteral("tool_name"), query.toolName);
    obj.insert(QStringLiteral("event_type"), query.eventType);
    obj.insert(QStringLiteral("keyword"), query.keyword);
    if (query.timeFrom.isValid())
        obj.insert(QStringLiteral("time_from"), query.timeFrom.toUTC().toString(Qt::ISODateWithMs));
    if (query.timeTo.isValid())
        obj.insert(QStringLiteral("time_to"), query.timeTo.toUTC().toString(Qt::ISODateWithMs));
    obj.insert(QStringLiteral("limit"), query.limit);
    obj.insert(QStringLiteral("ascending"), query.ascending);
    obj.insert(QStringLiteral("include_raw"), query.includeRaw);
    obj.insert(QStringLiteral("format"), outputFormatToString(query.format));
    obj.insert(QStringLiteral("level"), query.level);
    if (query.minDurationMs >= 0)
        obj.insert(QStringLiteral("min_duration"), static_cast<double>(query.minDurationMs));
    if (query.maxDurationMs >= 0)
        obj.insert(QStringLiteral("max_duration"), static_cast<double>(query.maxDurationMs));
    return obj;
}

QString hitTimeText(const LogQueryEngine::Hit& hit)
{
    if (hit.timestamp.isValid())
        return hit.timestamp.toUTC().toString(Qt::ISODateWithMs);
    return QStringLiteral("(unknown-time)");
}

QVector<LogQueryEngine::Hit> scanEventFileInternal(const QString& filePath,
                                                   const LogQueryEngine::Query& query,
                                                   LogQueryEngine::Result* result)
{
    QVector<LogQueryEngine::Hit> hits;
    QFile file(filePath);
    if (!file.exists())
        return hits;
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        if (result) {
            result->warnings.append(
                QStringLiteral("无法读取事件日志: %1").arg(QDir::toNativeSeparators(filePath)));
        }
        return hits;
    }

    if (result)
        ++result->scannedFiles;

    int lineNo = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        ++lineNo;
        if (result)
            ++result->scannedLines;
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            if (result && result->warnings.size() < 20) {
                result->warnings.append(
                    QStringLiteral("无效事件 JSONL: %1:%2").arg(
                        QDir::toNativeSeparators(filePath), QString::number(lineNo)));
            }
            continue;
        }

        const QJsonObject obj = doc.object();
        const LogQueryEngine::Hit hit =
            LogRecordSupport::buildEventHit(obj, filePath, lineNo, query.includeRaw);
        if (!LogRecordSupport::withinTimeRange(hit.timestamp, query))
            continue;
        const QString rawCompactLower = QString::fromUtf8(
                                            QJsonDocument(obj).toJson(QJsonDocument::Compact))
                                            .toLower();
        if (!LogRecordSupport::hitMatches(hit, query, rawCompactLower))
            continue;
        hits.append(hit);
    }
    return hits;
}

QVector<LogQueryEngine::Hit> scanSessionFileInternal(const QString& sessionId,
                                                     const QString& filePath,
                                                     const LogQueryEngine::Query& query,
                                                     LogQueryEngine::Result* result)
{
    QVector<LogQueryEngine::Hit> hits;
    QFile file(filePath);
    if (!file.exists())
        return hits;
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        if (result) {
            result->warnings.append(
                QStringLiteral("无法读取会话消息: %1").arg(QDir::toNativeSeparators(filePath)));
        }
        return hits;
    }

    if (result)
        ++result->scannedFiles;

    int lineNo = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        ++lineNo;
        if (result)
            ++result->scannedLines;
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            if (result && result->warnings.size() < 20) {
                result->warnings.append(
                    QStringLiteral("无效消息 JSONL: %1:%2").arg(
                        QDir::toNativeSeparators(filePath), QString::number(lineNo)));
            }
            continue;
        }

        const QJsonObject obj = doc.object();
        const LogQueryEngine::Hit hit =
            LogRecordSupport::buildMessageHit(obj, sessionId, filePath, lineNo, query.includeRaw);
        if (!LogRecordSupport::withinTimeRange(hit.timestamp, query))
            continue;
        const QString rawCompactLower = QString::fromUtf8(
                                            QJsonDocument(obj).toJson(QJsonDocument::Compact))
                                            .toLower();
        if (!LogRecordSupport::hitMatches(hit, query, rawCompactLower))
            continue;
        hits.append(hit);
    }

    return hits;
}

} // namespace

namespace LogRecordSupport {

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

LogQueryEngine::OutputFormat parseFormat(const QString& raw)
{
    const QString value = raw.trimmed().toLower();
    if (value == QLatin1String("table"))
        return LogQueryEngine::OutputFormat::Table;
    if (value == QLatin1String("json"))
        return LogQueryEngine::OutputFormat::Json;
    if (value == QLatin1String("raw"))
        return LogQueryEngine::OutputFormat::Raw;
    return LogQueryEngine::OutputFormat::Report;
}

QString formatResult(const LogQueryEngine::Result& result)
{
    if (result.query.format == LogQueryEngine::OutputFormat::Json) {
        return QString::fromUtf8(
            QJsonDocument(resultToJson(result)).toJson(QJsonDocument::Indented));
    }

    if (result.query.format == LogQueryEngine::OutputFormat::Raw) {
        QStringList lines;
        for (const LogQueryEngine::Hit& hit : result.hits) {
            if (hit.raw.isEmpty())
                continue;
            lines.append(QString::fromUtf8(QJsonDocument(hit.raw).toJson(QJsonDocument::Compact)));
        }
        if (lines.isEmpty())
            return QStringLiteral("(no raw records)");
        return lines.join(QStringLiteral("\n"));
    }

    QStringList out;
    out << QStringLiteral("event_log");
    out << QStringLiteral("source: %1").arg(result.query.source);
    out << QStringLiteral("data_root: %1").arg(QDir::toNativeSeparators(result.query.dataRootPath));
    out << QStringLiteral("scanned_files: %1").arg(result.scannedFiles);
    out << QStringLiteral("scanned_lines: %1").arg(result.scannedLines);
    out << QStringLiteral("results: %1").arg(result.hits.size());
    if (!result.warnings.isEmpty())
        out << QStringLiteral("warnings: %1").arg(result.warnings.join(QStringLiteral(" | ")));

    if (result.hits.isEmpty()) {
        out << QStringLiteral("未命中任何记录。");
        return out.join(QStringLiteral("\n"));
    }

    if (result.query.format == LogQueryEngine::OutputFormat::Table) {
        out << QStringLiteral("");
        out << QStringLiteral("time | src | session | trace | turn | event | tool | level | duration | summary");
        out << QStringLiteral("-----|-----|---------|-------|------|-------|------|-------|----------|--------");
        for (const LogQueryEngine::Hit& hit : result.hits) {
            const QString durationText = (hit.durationMs >= 0)
                ? QString::number(hit.durationMs) + QStringLiteral("ms")
                : QStringLiteral("-");
            out << QStringLiteral("%1 | %2 | %3 | %4 | %5 | %6 | %7 | %8 | %9 | %10")
                       .arg(hitTimeText(hit),
                            hit.source,
                            hit.sessionId.isEmpty() ? QStringLiteral("-") : hit.sessionId,
                            hit.traceId.isEmpty() ? QStringLiteral("-") : hit.traceId,
                            hit.turnId.isEmpty() ? QStringLiteral("-") : hit.turnId,
                            hit.eventType.isEmpty() ? QStringLiteral("-") : hit.eventType,
                            hit.toolName.isEmpty() ? QStringLiteral("-") : hit.toolName,
                            hit.level.isEmpty() ? QStringLiteral("-") : hit.level,
                            durationText)
                       .arg(clip(hit.summary, 140));
        }
        return out.join(QStringLiteral("\n"));
    }

    out << QStringLiteral("");
    out << QStringLiteral("命中列表:");
    for (const LogQueryEngine::Hit& hit : result.hits) {
        QStringList meta;
        if (!hit.sessionId.isEmpty())
            meta << QStringLiteral("sid=%1").arg(hit.sessionId);
        if (!hit.traceId.isEmpty())
            meta << QStringLiteral("trace=%1").arg(hit.traceId);
        if (!hit.turnId.isEmpty())
            meta << QStringLiteral("turn=%1").arg(hit.turnId);
        if (!hit.runId.isEmpty())
            meta << QStringLiteral("run=%1").arg(hit.runId);
        if (!hit.requestId.isEmpty())
            meta << QStringLiteral("req=%1").arg(hit.requestId);
        if (!hit.toolCallId.isEmpty())
            meta << QStringLiteral("tool_call=%1").arg(hit.toolCallId);
        if (!hit.toolName.isEmpty())
            meta << QStringLiteral("tool=%1").arg(hit.toolName);
        if (!hit.eventType.isEmpty())
            meta << QStringLiteral("event=%1").arg(hit.eventType);
        if (hit.successKnown)
            meta << QStringLiteral("success=%1").arg(hit.success ? QStringLiteral("true")
                                                                  : QStringLiteral("false"));
        if (!hit.level.isEmpty())
            meta << QStringLiteral("level=%1").arg(hit.level);
        if (hit.durationMs >= 0)
            meta << QStringLiteral("duration=%1ms").arg(hit.durationMs);

        out << QStringLiteral("- [%1] %2 (%3:%4)")
                   .arg(hit.source,
                        hitTimeText(hit),
                        QDir::toNativeSeparators(hit.filePath),
                        QString::number(hit.lineNo));
        if (!meta.isEmpty())
            out << QStringLiteral("  %1").arg(meta.join(QStringLiteral(", ")));
        if (!hit.summary.isEmpty())
            out << QStringLiteral("  %1").arg(hit.summary);
        if (result.query.includeRaw && !hit.raw.isEmpty()) {
            out << QStringLiteral("  raw: %1").arg(
                clip(QString::fromUtf8(QJsonDocument(hit.raw).toJson(QJsonDocument::Compact)), 360));
        }
    }
    return out.join(QStringLiteral("\n"));
}

QJsonObject resultToJson(const LogQueryEngine::Result& result)
{
    QJsonObject root;
    root.insert(QStringLiteral("query"), queryToJson(result.query));
    root.insert(QStringLiteral("scanned_files"), result.scannedFiles);
    root.insert(QStringLiteral("scanned_lines"), static_cast<double>(result.scannedLines));

    QJsonArray warnings;
    for (const QString& warning : result.warnings)
        warnings.append(warning);
    root.insert(QStringLiteral("warnings"), warnings);

    QJsonArray hits;
    for (const LogQueryEngine::Hit& hit : result.hits) {
        QJsonObject obj;
        obj.insert(QStringLiteral("source"), hit.source);
        obj.insert(QStringLiteral("file"), hit.filePath);
        obj.insert(QStringLiteral("line"), hit.lineNo);
        if (hit.timestamp.isValid())
            obj.insert(QStringLiteral("timestamp"), hit.timestamp.toUTC().toString(Qt::ISODateWithMs));
        if (hit.timestampMs >= 0)
            obj.insert(QStringLiteral("timestamp_ms"), static_cast<double>(hit.timestampMs));
        obj.insert(QStringLiteral("session_id"), hit.sessionId);
        obj.insert(QStringLiteral("trace_id"), hit.traceId);
        obj.insert(QStringLiteral("turn_id"), hit.turnId);
        obj.insert(QStringLiteral("run_id"), hit.runId);
        obj.insert(QStringLiteral("request_id"), hit.requestId);
        obj.insert(QStringLiteral("tool_call_id"), hit.toolCallId);
        obj.insert(QStringLiteral("actor_id"), hit.actorId);
        obj.insert(QStringLiteral("tool_name"), hit.toolName);
        obj.insert(QStringLiteral("event_type"), hit.eventType);
        obj.insert(QStringLiteral("summary"), hit.summary);
        if (hit.successKnown)
            obj.insert(QStringLiteral("success"), hit.success);
        obj.insert(QStringLiteral("level"), hit.level);
        if (hit.durationMs >= 0)
            obj.insert(QStringLiteral("duration_ms"), static_cast<double>(hit.durationMs));
        if (!hit.raw.isEmpty())
            obj.insert(QStringLiteral("raw"), hit.raw);
        hits.append(obj);
    }
    root.insert(QStringLiteral("hits"), hits);
    root.insert(QStringLiteral("count"), hits.size());
    return root;
}

bool sourceMatches(const QString& source, bool isEvent)
{
    if (source == QLatin1String("all"))
        return true;
    if (source == QLatin1String("events"))
        return isEvent;
    if (source == QLatin1String("messages"))
        return !isEvent;
    return false;
}

qint64 toMs(const QDateTime& dt)
{
    if (!dt.isValid())
        return -1;
    return dt.toUTC().toMSecsSinceEpoch();
}

bool withinTimeRange(const QDateTime& timestamp, const LogQueryEngine::Query& query)
{
    if (!query.timeFrom.isValid() && !query.timeTo.isValid())
        return true;
    if (!timestamp.isValid())
        return false;
    const QDateTime utc = timestamp.toUTC();
    if (query.timeFrom.isValid() && utc < query.timeFrom.toUTC())
        return false;
    if (query.timeTo.isValid() && utc > query.timeTo.toUTC())
        return false;
    return true;
}

bool hitMatches(const LogQueryEngine::Hit& hit,
                const LogQueryEngine::Query& query,
                const QString& rawCompactLower)
{
    if (!query.sessionId.isEmpty() && !equalsIgnoreCase(hit.sessionId, query.sessionId))
        return false;
    if (!query.traceId.isEmpty() && !equalsIgnoreCase(hit.traceId, query.traceId))
        return false;
    if (!query.turnId.isEmpty() && !equalsIgnoreCase(hit.turnId, query.turnId))
        return false;
    if (!query.runId.isEmpty() && !equalsIgnoreCase(hit.runId, query.runId))
        return false;
    if (!query.requestId.isEmpty()) {
        const bool requestMatched = equalsIgnoreCase(hit.requestId, query.requestId)
            || rawCompactLower.contains(query.requestId.toLower());
        if (!requestMatched)
            return false;
    }
    if (!query.toolCallId.isEmpty()) {
        const bool toolCallMatched = equalsIgnoreCase(hit.toolCallId, query.toolCallId)
            || rawCompactLower.contains(query.toolCallId.toLower());
        if (!toolCallMatched)
            return false;
    }
    if (!query.actorId.isEmpty() && !equalsIgnoreCase(hit.actorId, query.actorId))
        return false;
    if (!query.toolName.isEmpty() && !equalsIgnoreCase(hit.toolName, query.toolName))
        return false;
    if (!query.eventType.isEmpty() && !equalsIgnoreCase(hit.eventType, query.eventType))
        return false;
    if (!query.level.isEmpty() && !equalsIgnoreCase(hit.level, query.level))
        return false;
    if (query.minDurationMs >= 0 && hit.durationMs >= 0 && hit.durationMs < query.minDurationMs)
        return false;
    if (query.maxDurationMs >= 0 && hit.durationMs >= 0 && hit.durationMs > query.maxDurationMs)
        return false;

    if (!query.keyword.isEmpty()) {
        const QString keyword = query.keyword.toLower();
        QString searchable = hit.summary.toLower();
        searchable += QLatin1Char(' ') + hit.eventType.toLower();
        searchable += QLatin1Char(' ') + hit.toolName.toLower();
        searchable += QLatin1Char(' ') + hit.sessionId.toLower();
        searchable += QLatin1Char(' ') + hit.traceId.toLower();
        searchable += QLatin1Char(' ') + hit.turnId.toLower();
        searchable += QLatin1Char(' ') + hit.runId.toLower();
        searchable += QLatin1Char(' ') + hit.requestId.toLower();
        searchable += QLatin1Char(' ') + hit.toolCallId.toLower();
        searchable += QLatin1Char(' ') + hit.actorId.toLower();
        searchable += QLatin1Char(' ') + rawCompactLower;
        if (!searchable.contains(keyword))
            return false;
    }

    return true;
}

LogQueryEngine::Hit buildEventHit(const QJsonObject& obj,
                                  const QString& filePath,
                                  int lineNo,
                                  bool includeRaw)
{
    LogQueryEngine::Hit hit;
    hit.source = QStringLiteral("event");
    hit.filePath = filePath;
    hit.lineNo = lineNo;
    hit.timestamp = parseTimestampFromObject(obj);
    hit.timestampMs = toMs(hit.timestamp);
    hit.sessionId = stringFieldAny(obj, QStringList()
                                            << QStringLiteral("session_id")
                                            << QStringLiteral("sessionId"));
    hit.traceId = stringField(obj, QStringLiteral("trace_id"));
    hit.turnId = stringFieldAny(obj, QStringList()
                                         << QStringLiteral("turn_id")
                                         << QStringLiteral("turnId"));
    hit.runId = stringFieldAny(obj, QStringList()
                                        << QStringLiteral("run_id")
                                        << QStringLiteral("runId"));
    hit.requestId = extractRequestId(obj, true);
    hit.toolCallId = extractToolCallId(obj, true);
    hit.actorId = extractActorId(obj, true);
    hit.toolName = extractToolName(obj, true);
    hit.eventType = extractEventType(obj, true);
    hit.summary = summarizeEvent(obj);
    extractSuccess(obj, true, &hit.successKnown, &hit.success);
    hit.level = extractLevel(obj, true);
    hit.durationMs = extractDuration(obj);
    if (includeRaw)
        hit.raw = obj;
    return hit;
}

LogQueryEngine::Hit buildMessageHit(const QJsonObject& obj,
                                    const QString& sessionIdFallback,
                                    const QString& filePath,
                                    int lineNo,
                                    bool includeRaw)
{
    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();

    LogQueryEngine::Hit hit;
    hit.source = QStringLiteral("message");
    hit.filePath = filePath;
    hit.lineNo = lineNo;
    hit.timestamp = parseTimestampFromObject(obj);
    hit.timestampMs = toMs(hit.timestamp);
    hit.sessionId = stringFieldAny(obj, QStringList() << QStringLiteral("sessionId")
                                                      << sessionIdFallback);
    hit.traceId = stringField(obj, QStringLiteral("traceId"));
    hit.turnId = stringField(obj, QStringLiteral("turnId"));
    hit.runId = stringField(payloadObj, QStringLiteral("run_id"));
    hit.requestId = extractRequestId(obj, false);
    hit.toolCallId = extractToolCallId(obj, false);
    hit.actorId = stringFieldAny(obj, QStringList() << QStringLiteral("senderId")
                                                    << QStringLiteral("sender_id"));
    hit.toolName = extractToolName(obj, false);
    hit.eventType = extractEventType(obj, false);
    hit.summary = summarizeMessage(obj);
    extractSuccess(obj, false, &hit.successKnown, &hit.success);
    hit.level = extractLevel(obj, false);
    hit.durationMs = extractDuration(payloadObj);
    if (includeRaw)
        hit.raw = obj;
    return hit;
}

bool eventMatchesFilter(const QJsonObject& obj,
                        const LogQueryEngine::Query& filter,
                        LogQueryEngine::Hit* outHit)
{
    const LogQueryEngine::Hit hit = buildEventHit(obj, QStringLiteral("sqlite://events"), 0, false);
    const QString rawCompactLower = QString::fromUtf8(
                                        QJsonDocument(obj).toJson(QJsonDocument::Compact))
                                        .toLower();
    if (!withinTimeRange(hit.timestamp, filter))
        return false;
    if (!hitMatches(hit, filter, rawCompactLower))
        return false;
    if (outHit)
        *outHit = hit;
    return true;
}

QVector<LogQueryEngine::Hit> queryEvents(const LogQueryEngine::Query& query,
                                         LogQueryEngine::Result* result)
{
    QVector<LogQueryEngine::Hit> hits;

    QString dbError;
    QSqlDatabase db = openConnection(query.dataRootPath, &dbError);
    if (db.isValid() && db.isOpen()) {
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
                    hit.summary = summarizeEvent(rawDoc.object());
            }
            if (query.includeRaw) {
                QJsonParseError parseError;
                const QJsonDocument rawDoc = QJsonDocument::fromJson(rawJson.toUtf8(), &parseError);
                if (parseError.error == QJsonParseError::NoError && rawDoc.isObject())
                    hit.raw = rawDoc.object();
            }

            if (!withinTimeRange(hit.timestamp, query))
                continue;
            if (!hitMatches(hit, query, rawCompactLower))
                continue;
            hits.append(hit);
        }
        return hits;
    }

    if (result)
        result->warnings.append(QStringLiteral("SQLite 连接不可用，回退到 JSONL events 扫描: %1").arg(dbError));

    const QString logsDir = QDir(resolveDataRoot(query.dataRootPath)).filePath(QStringLiteral("logs"));
    const QDir dir(logsDir);
    const QStringList files = dir.entryList(QStringList()
                                                << QStringLiteral("events-*.jsonl")
                                                << QStringLiteral("events-current.jsonl"),
                                            QDir::Files,
                                            query.ascending ? QDir::Name : QDir::Reversed);
    for (const QString& fileName : files)
        hits += scanEventFileInternal(dir.filePath(fileName), query, result);
    return hits;
}

QVector<LogQueryEngine::Hit> queryMessages(const LogQueryEngine::Query& query,
                                           LogQueryEngine::Result* result)
{
    QVector<LogQueryEngine::Hit> hits;

    QString dbError;
    QSqlDatabase db = openConnection(query.dataRootPath, &dbError);
    if (db.isValid() && db.isOpen()) {
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

            LogQueryEngine::Hit hit =
                buildMessageHit(rawObj, q.value(2).toString(), QStringLiteral("sqlite://messages"), q.value(0).toInt(), query.includeRaw);
            hit.timestamp = parseIsoDateTime(q.value(9).toString());
            hit.timestampMs = toMs(hit.timestamp);
            if (!withinTimeRange(hit.timestamp, query))
                continue;
            const QString rawCompactLower = QString::fromUtf8(
                                                QJsonDocument(rawObj).toJson(QJsonDocument::Compact))
                                                .toLower();
            if (!hitMatches(hit, query, rawCompactLower))
                continue;
            hits.append(hit);
        }
        return hits;
    }

    if (result)
        result->warnings.append(QStringLiteral("SQLite 连接不可用，回退到 JSONL messages 扫描: %1").arg(dbError));

    const QString sessionsDataDir = QDir(resolveDataRoot(query.dataRootPath))
                                        .filePath(QStringLiteral("sessions/data"));
    if (!query.sessionId.trimmed().isEmpty()) {
        const QString path = QDir(QDir(sessionsDataDir).filePath(query.sessionId))
                                 .filePath(QStringLiteral("messages.jsonl"));
        return scanSessionFileInternal(query.sessionId, path, query, result);
    }

    QDirIterator it(sessionsDataDir, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        it.next();
        const QString sessionId = it.fileName();
        const QString path = QDir(it.filePath()).filePath(QStringLiteral("messages.jsonl"));
        hits += scanSessionFileInternal(sessionId, path, query, result);
    }
    return hits;
}

QString clip(const QString& text, int maxChars)
{
    const QString simplified = text.simplified();
    if (maxChars <= 0 || simplified.size() <= maxChars)
        return simplified;
    return simplified.left(maxChars) + QStringLiteral("...");
}

} // namespace LogRecordSupport
