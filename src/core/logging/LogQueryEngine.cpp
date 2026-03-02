#include "LogQueryEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace {

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

bool equalsIgnoreCase(const QString& a, const QString& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
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

LogQueryEngine::OutputFormat parseOutputFormat(const QString& raw)
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
    return obj;
}

QString normalizeSource(const QString& raw)
{
    const QString source = raw.trimmed().toLower();
    if (source.isEmpty())
        return QStringLiteral("all");
    return source;
}

QString hitTimeText(const LogQueryEngine::Hit& hit)
{
    if (hit.timestamp.isValid())
        return hit.timestamp.toUTC().toString(Qt::ISODateWithMs);
    return QStringLiteral("(unknown-time)");
}

} // namespace

LogQueryEngine::Query LogQueryEngine::queryFromJson(const QJsonObject& args, QString* error)
{
    if (error)
        *error = QString();

    Query query;
    query.dataRootPath = args.value(QStringLiteral("data_root")).toString().trimmed();
    if (query.dataRootPath.isEmpty())
        query.dataRootPath = QDir::home().filePath(QStringLiteral(".tmagent"));

    query.source = normalizeSource(args.value(QStringLiteral("source")).toString());
    if (query.source != QLatin1String("all")
        && query.source != QLatin1String("events")
        && query.source != QLatin1String("messages")) {
        if (error)
            *error = QStringLiteral("source 仅支持 all/events/messages");
        return query;
    }

    query.sessionId = args.value(QStringLiteral("session_id")).toString().trimmed();
    query.traceId = args.value(QStringLiteral("trace_id")).toString().trimmed();
    query.turnId = args.value(QStringLiteral("turn_id")).toString().trimmed();
    query.runId = args.value(QStringLiteral("run_id")).toString().trimmed();
    query.requestId = args.value(QStringLiteral("request_id")).toString().trimmed();
    query.toolCallId = args.value(QStringLiteral("tool_call_id")).toString().trimmed();

    query.actorId = args.value(QStringLiteral("actor_id")).toString().trimmed();
    if (query.actorId.isEmpty())
        query.actorId = args.value(QStringLiteral("agent_id")).toString().trimmed();

    query.toolName = args.value(QStringLiteral("tool_name")).toString().trimmed();
    query.eventType = args.value(QStringLiteral("event_type")).toString().trimmed();
    query.keyword = args.value(QStringLiteral("keyword")).toString().trimmed();

    const QJsonValue timeFromValue = args.value(QStringLiteral("time_from"));
    if (timeFromValue.isDouble()) {
        const qint64 raw = static_cast<qint64>(timeFromValue.toDouble());
        const qint64 ms = (qAbs(raw) < 100000000000LL) ? raw * 1000LL : raw;
        query.timeFrom = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
    } else {
        const QString timeFromRaw = timeFromValue.toString().trimmed();
        if (!timeFromRaw.isEmpty() && !parseDateTimeArg(timeFromRaw, &query.timeFrom)) {
            if (error)
                *error = QStringLiteral("time_from 格式无效，支持 ISO8601 或 epoch 秒/毫秒");
            return query;
        }
    }

    const QJsonValue timeToValue = args.value(QStringLiteral("time_to"));
    if (timeToValue.isDouble()) {
        const qint64 raw = static_cast<qint64>(timeToValue.toDouble());
        const qint64 ms = (qAbs(raw) < 100000000000LL) ? raw * 1000LL : raw;
        query.timeTo = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
    } else {
        const QString timeToRaw = timeToValue.toString().trimmed();
        if (!timeToRaw.isEmpty() && !parseDateTimeArg(timeToRaw, &query.timeTo)) {
            if (error)
                *error = QStringLiteral("time_to 格式无效，支持 ISO8601 或 epoch 秒/毫秒");
            return query;
        }
    }

    if (query.timeFrom.isValid() && query.timeTo.isValid() && query.timeFrom > query.timeTo) {
        if (error)
            *error = QStringLiteral("time_from 不能晚于 time_to");
        return query;
    }

    query.limit = qBound(1, args.value(QStringLiteral("limit")).toInt(50), 2000);
    query.ascending = args.value(QStringLiteral("ascending")).toBool(false);
    const QString order = args.value(QStringLiteral("order")).toString().trimmed().toLower();
    if (order == QLatin1String("asc"))
        query.ascending = true;
    else if (order == QLatin1String("desc"))
        query.ascending = false;

    query.includeRaw = args.value(QStringLiteral("include_raw")).toBool(false);
    query.format = parseOutputFormat(args.value(QStringLiteral("format")).toString());
    if (query.format == OutputFormat::Raw)
        query.includeRaw = true;
    return query;
}

LogQueryEngine::Result LogQueryEngine::execute(const Query& inputQuery)
{
    Result result;
    result.query = inputQuery;

    Query query = inputQuery;
    if (query.dataRootPath.trimmed().isEmpty())
        query.dataRootPath = QDir::home().filePath(QStringLiteral(".tmagent"));
    query.source = normalizeSource(query.source);
    if (query.source != QLatin1String("events")
        && query.source != QLatin1String("messages")) {
        query.source = QStringLiteral("all");
    }
    result.query = query;

    if (sourceMatches(query.source, true)) {
        const QString logsDirPath = QDir(query.dataRootPath).filePath(QStringLiteral("logs"));
        QDir logsDir(logsDirPath);
        if (!logsDir.exists()) {
            result.warnings.append(
                QStringLiteral("事件日志目录不存在: %1")
                    .arg(QDir::toNativeSeparators(logsDirPath)));
        } else {
            QStringList eventFiles = logsDir.entryList(
                QStringList() << QStringLiteral("events-*.jsonl"),
                QDir::Files,
                QDir::Name);
            if (QFileInfo(logsDir.filePath(QStringLiteral("events-current.jsonl"))).exists()
                && !eventFiles.contains(QStringLiteral("events-current.jsonl"))) {
                eventFiles.prepend(QStringLiteral("events-current.jsonl"));
            }
            for (const QString& fileName : eventFiles) {
                const QString filePath = logsDir.filePath(fileName);
                const QVector<Hit> hits = scanEventFile(filePath, query, &result);
                result.hits += hits;
            }
        }
    }

    if (sourceMatches(query.source, false)) {
        const QString sessionsDataPath = QDir(query.dataRootPath).filePath(QStringLiteral("sessions/data"));
        QDir sessionsDir(sessionsDataPath);
        if (!sessionsDir.exists()) {
            result.warnings.append(
                QStringLiteral("会话目录不存在: %1")
                    .arg(QDir::toNativeSeparators(sessionsDataPath)));
        } else {
            QStringList sessionIds;
            if (!query.sessionId.isEmpty()) {
                const QString sessionPath = sessionsDir.filePath(query.sessionId);
                if (QDir(sessionPath).exists()) {
                    sessionIds << query.sessionId;
                } else {
                    result.warnings.append(
                        QStringLiteral("指定 session_id 不存在: %1").arg(query.sessionId));
                }
            } else {
                sessionIds = sessionsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            }

            for (const QString& sessionId : sessionIds) {
                const QString filePath = QDir(sessionsDir.filePath(sessionId))
                                             .filePath(QStringLiteral("messages.jsonl"));
                const QVector<Hit> hits = scanSessionFile(sessionId, filePath, query, &result);
                result.hits += hits;
            }
        }
    }

    std::sort(result.hits.begin(), result.hits.end(), [query](const Hit& a, const Hit& b) {
        const bool asc = query.ascending;
        const bool aValidTs = a.timestampMs >= 0;
        const bool bValidTs = b.timestampMs >= 0;
        if (aValidTs != bValidTs)
            return aValidTs ? true : false;
        if (a.timestampMs != b.timestampMs)
            return asc ? (a.timestampMs < b.timestampMs) : (a.timestampMs > b.timestampMs);
        if (a.filePath != b.filePath)
            return asc ? (a.filePath < b.filePath) : (a.filePath > b.filePath);
        return asc ? (a.lineNo < b.lineNo) : (a.lineNo > b.lineNo);
    });

    if (result.hits.size() > query.limit)
        result.hits.resize(query.limit);
    return result;
}

QString LogQueryEngine::formatResult(const Result& result)
{
    if (result.query.format == OutputFormat::Json) {
        return QString::fromUtf8(
            QJsonDocument(resultToJson(result)).toJson(QJsonDocument::Indented));
    }

    if (result.query.format == OutputFormat::Raw) {
        QStringList lines;
        for (const Hit& hit : result.hits) {
            if (hit.raw.isEmpty())
                continue;
            lines.append(QString::fromUtf8(QJsonDocument(hit.raw).toJson(QJsonDocument::Compact)));
        }
        if (lines.isEmpty())
            return QStringLiteral("(no raw records)");
        return lines.join(QStringLiteral("\n"));
    }

    QStringList out;
    out << QStringLiteral("event_log_search");
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

    if (result.query.format == OutputFormat::Table) {
        out << QStringLiteral("");
        out << QStringLiteral("time | src | session | trace | turn | event | tool | summary");
        out << QStringLiteral("-----|-----|---------|-------|------|-------|------|--------");
        for (const Hit& hit : result.hits) {
            out << QStringLiteral("%1 | %2 | %3 | %4 | %5 | %6 | %7 | %8")
                       .arg(
                           hitTimeText(hit),
                           hit.source,
                           hit.sessionId.isEmpty() ? QStringLiteral("-") : hit.sessionId,
                           hit.traceId.isEmpty() ? QStringLiteral("-") : hit.traceId,
                           hit.turnId.isEmpty() ? QStringLiteral("-") : hit.turnId,
                           hit.eventType.isEmpty() ? QStringLiteral("-") : hit.eventType,
                           hit.toolName.isEmpty() ? QStringLiteral("-") : hit.toolName,
                           clip(hit.summary, 140));
        }
        return out.join(QStringLiteral("\n"));
    }

    out << QStringLiteral("");
    out << QStringLiteral("命中列表:");
    for (const Hit& hit : result.hits) {
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
            meta << QStringLiteral("success=%1").arg(hit.success ? QStringLiteral("true") : QStringLiteral("false"));

        out << QStringLiteral("- [%1] %2 (%3:%4)")
                   .arg(
                       hit.source,
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

QJsonObject LogQueryEngine::resultToJson(const Result& result)
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
    for (const Hit& hit : result.hits) {
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
        if (!hit.raw.isEmpty())
            obj.insert(QStringLiteral("raw"), hit.raw);
        hits.append(obj);
    }
    root.insert(QStringLiteral("hits"), hits);
    root.insert(QStringLiteral("count"), hits.size());
    return root;
}

bool LogQueryEngine::parseDateTimeArg(const QString& raw, QDateTime* out)
{
    if (out)
        *out = QDateTime();

    const QString text = raw.trimmed();
    if (text.isEmpty())
        return false;

    bool ok = false;
    const qint64 number = text.toLongLong(&ok);
    if (ok) {
        const qint64 ms = (qAbs(number) < 100000000000LL) ? number * 1000LL : number;
        if (out)
            *out = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
        return true;
    }

    QDateTime dt = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(text, Qt::ISODate);
    if (!dt.isValid())
        return false;
    if (dt.timeSpec() == Qt::LocalTime)
        dt = dt.toUTC();
    if (out)
        *out = dt;
    return true;
}

bool LogQueryEngine::sourceMatches(const QString& source, bool isEvent)
{
    if (source == QLatin1String("all"))
        return true;
    if (source == QLatin1String("events"))
        return isEvent;
    if (source == QLatin1String("messages"))
        return !isEvent;
    return false;
}

bool LogQueryEngine::withinTimeRange(const QDateTime& timestamp, const Query& query)
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

bool LogQueryEngine::hitMatches(const Hit& hit, const Query& query, const QString& rawCompactLower)
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

QVector<LogQueryEngine::Hit> LogQueryEngine::scanEventFile(const QString& filePath, const Query& query, Result* result)
{
    QVector<Hit> hits;
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
        Hit hit;
        hit.source = QStringLiteral("event");
        hit.filePath = filePath;
        hit.lineNo = lineNo;
        hit.timestamp = parseTimestampFromObject(obj);
        hit.timestampMs = toMs(hit.timestamp);
        hit.sessionId = firstNonEmpty(QStringList()
                                      << stringField(obj, QStringLiteral("session_id"))
                                      << stringField(obj, QStringLiteral("sessionId")));
        hit.traceId = stringField(obj, QStringLiteral("trace_id"));
        hit.turnId = firstNonEmpty(QStringList()
                                   << stringField(obj, QStringLiteral("turn_id"))
                                   << stringField(obj, QStringLiteral("turnId")));
        hit.runId = firstNonEmpty(QStringList()
                                  << stringField(obj, QStringLiteral("run_id"))
                                  << stringField(obj, QStringLiteral("runId")));
        hit.requestId = extractRequestId(obj, true);
        hit.toolCallId = extractToolCallId(obj, true);
        hit.actorId = extractActorId(obj, true);
        hit.toolName = extractToolName(obj, true);
        hit.eventType = extractEventType(obj, true);
        hit.summary = summarizeEvent(obj);
        extractSuccess(obj, true, &hit.successKnown, &hit.success);
        if (query.includeRaw)
            hit.raw = obj;

        if (!withinTimeRange(hit.timestamp, query))
            continue;

        const QString rawCompactLower = QString::fromUtf8(
            QJsonDocument(obj).toJson(QJsonDocument::Compact))
                                            .toLower();
        if (!hitMatches(hit, query, rawCompactLower))
            continue;

        hits.append(hit);
    }
    file.close();
    return hits;
}

QVector<LogQueryEngine::Hit> LogQueryEngine::scanSessionFile(const QString& sessionId, const QString& filePath, const Query& query, Result* result)
{
    QVector<Hit> hits;
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
        const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
        const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();

        Hit hit;
        hit.source = QStringLiteral("message");
        hit.filePath = filePath;
        hit.lineNo = lineNo;
        hit.timestamp = parseTimestampFromObject(obj);
        hit.timestampMs = toMs(hit.timestamp);
        hit.sessionId = firstNonEmpty(QStringList()
                                      << stringField(obj, QStringLiteral("sessionId"))
                                      << sessionId);
        hit.traceId = stringField(obj, QStringLiteral("traceId"));
        hit.turnId = stringField(obj, QStringLiteral("turnId"));
        hit.runId = stringField(payloadObj, QStringLiteral("run_id"));
        hit.requestId = extractRequestId(obj, false);
        hit.toolCallId = extractToolCallId(obj, false);
        hit.actorId = firstNonEmpty(QStringList()
                                    << stringField(obj, QStringLiteral("senderId"))
                                    << stringField(obj, QStringLiteral("sender_id")));
        hit.toolName = extractToolName(obj, false);
        hit.eventType = extractEventType(obj, false);
        hit.summary = summarizeMessage(obj);
        extractSuccess(obj, false, &hit.successKnown, &hit.success);
        if (query.includeRaw)
            hit.raw = obj;

        if (!withinTimeRange(hit.timestamp, query))
            continue;

        const QString rawCompactLower = QString::fromUtf8(
            QJsonDocument(obj).toJson(QJsonDocument::Compact))
                                            .toLower();
        if (!hitMatches(hit, query, rawCompactLower))
            continue;
        hits.append(hit);
    }
    file.close();
    return hits;
}

QString LogQueryEngine::extractEventType(const QJsonObject& obj, bool isEventSource)
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

QString LogQueryEngine::extractToolName(const QJsonObject& obj, bool isEventSource)
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
        return stringField(toolEventObj.value(QStringLiteral("data")).toObject(), QStringLiteral("tool_name"));
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    QString value = stringField(payloadObj, QStringLiteral("tool_name"));
    if (!value.isEmpty())
        return value;
    value = stringField(payloadObj.value(QStringLiteral("event_data")).toObject(), QStringLiteral("tool_name"));
    return value;
}

QString LogQueryEngine::extractToolCallId(const QJsonObject& obj, bool isEventSource)
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
        return stringField(toolEventObj.value(QStringLiteral("data")).toObject(), QStringLiteral("tool_call_id"));
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    QString value = stringField(payloadObj, QStringLiteral("tool_call_id"));
    if (!value.isEmpty())
        return value;
    return stringField(payloadObj, QStringLiteral("id"));
}

QString LogQueryEngine::extractRequestId(const QJsonObject& obj, bool isEventSource)
{
    QString value = stringField(obj, QStringLiteral("request_id"));
    if (!value.isEmpty())
        return value;

    if (isEventSource) {
        const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
        value = stringField(toolEventObj.value(QStringLiteral("data")).toObject(), QStringLiteral("child_request_id"));
        if (!value.isEmpty())
            return value;
        return stringField(obj, QStringLiteral("child_request_id"));
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    value = stringField(payloadObj, QStringLiteral("child_request_id"));
    if (!value.isEmpty())
        return value;
    return stringField(payloadObj.value(QStringLiteral("event_data")).toObject(), QStringLiteral("child_request_id"));
}

QString LogQueryEngine::extractActorId(const QJsonObject& obj, bool isEventSource)
{
    if (isEventSource) {
        QString value = stringFieldAny(obj, QStringList()
                                                << QStringLiteral("actorIdentityId")
                                                << QStringLiteral("actor_id"));
        if (!value.isEmpty())
            return value;
        const QJsonObject toolEventData = obj.value(QStringLiteral("toolEvent")).toObject().value(QStringLiteral("data")).toObject();
        return stringField(toolEventData, QStringLiteral("_agent_id"));
    }
    return firstNonEmpty(QStringList()
                         << stringField(obj, QStringLiteral("senderId"))
                         << stringField(obj, QStringLiteral("sender_id")));
}

bool LogQueryEngine::extractSuccess(const QJsonObject& obj, bool isEventSource, bool* known, bool* value)
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
        if (toolEventObj.contains(QStringLiteral("success")) && toolEventObj.value(QStringLiteral("success")).isBool()) {
            assign(toolEventObj.value(QStringLiteral("success")).toBool());
            return true;
        }
        return false;
    }

    const QJsonObject payloadObj = obj.value(QStringLiteral("content")).toObject().value(QStringLiteral("payload")).toObject();
    if (payloadObj.contains(QStringLiteral("success")) && payloadObj.value(QStringLiteral("success")).isBool()) {
        assign(payloadObj.value(QStringLiteral("success")).toBool());
        return true;
    }
    return false;
}

QString LogQueryEngine::valueAtPath(const QJsonObject& obj, const QStringList& path)
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

QString LogQueryEngine::firstNonEmpty(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        const QString trimmed = candidate.trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return QString();
}

QString LogQueryEngine::summarizeEvent(const QJsonObject& obj)
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
    return clip(text.simplified(), 220);
}

QString LogQueryEngine::summarizeMessage(const QJsonObject& obj)
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
    return clip(summary.simplified(), 220);
}

QString LogQueryEngine::clip(const QString& text, int maxChars)
{
    const QString simplified = text.simplified();
    if (maxChars <= 0 || simplified.size() <= maxChars)
        return simplified;
    return simplified.left(maxChars) + QStringLiteral("...");
}

qint64 LogQueryEngine::toMs(const QDateTime& dt)
{
    if (!dt.isValid())
        return -1;
    return dt.toUTC().toMSecsSinceEpoch();
}
