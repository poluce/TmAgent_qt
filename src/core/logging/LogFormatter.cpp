#include "LogFormatter.h"
#include "LogSummarizer.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

namespace {

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

} // namespace

namespace LogFormatter {

LogQueryEngine::OutputFormat parseFormat(const QString& raw)
{
    return parseOutputFormat(raw);
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

    if (result.query.format == LogQueryEngine::OutputFormat::Table) {
        out << QStringLiteral("");
        out << QStringLiteral("time | src | session | trace | turn | event | tool | level | duration | summary");
        out << QStringLiteral("-----|-----|---------|-------|------|-------|------|-------|----------|--------");
        for (const LogQueryEngine::Hit& hit : result.hits) {
            const QString durationText = (hit.durationMs >= 0)
                ? QString::number(hit.durationMs) + QStringLiteral("ms")
                : QStringLiteral("-");
            out << QStringLiteral("%1 | %2 | %3 | %4 | %5 | %6 | %7 | %8 | %9 | %10")
                       .arg(
                           hitTimeText(hit),
                           hit.source,
                           hit.sessionId.isEmpty() ? QStringLiteral("-") : hit.sessionId,
                           hit.traceId.isEmpty() ? QStringLiteral("-") : hit.traceId,
                           hit.turnId.isEmpty() ? QStringLiteral("-") : hit.turnId,
                           hit.eventType.isEmpty() ? QStringLiteral("-") : hit.eventType,
                           hit.toolName.isEmpty() ? QStringLiteral("-") : hit.toolName,
                           hit.level.isEmpty() ? QStringLiteral("-") : hit.level,
                           durationText)
                       .arg(LogSummarizer::clip(hit.summary, 140));
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
            meta << QStringLiteral("success=%1").arg(hit.success ? QStringLiteral("true") : QStringLiteral("false"));
        if (!hit.level.isEmpty())
            meta << QStringLiteral("level=%1").arg(hit.level);
        if (hit.durationMs >= 0)
            meta << QStringLiteral("duration=%1ms").arg(hit.durationMs);

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
                LogSummarizer::clip(QString::fromUtf8(QJsonDocument(hit.raw).toJson(QJsonDocument::Compact)), 360));
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

} // namespace LogFormatter
