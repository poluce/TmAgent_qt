#include "LogQueryEngine.h"
#include "LogRecordSupport.h"

#include <QDir>

namespace {

QString normalizeSource(const QString& raw)
{
    const QString source = raw.trimmed().toLower();
    if (source.isEmpty())
        return QStringLiteral("all");
    return source;
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
    query.format = LogRecordSupport::parseFormat(args.value(QStringLiteral("format")).toString());
    if (query.format == OutputFormat::Raw)
        query.includeRaw = true;

    query.level = args.value(QStringLiteral("level")).toString().trimmed();
    query.minDurationMs = static_cast<qint64>(args.value(QStringLiteral("min_duration")).toDouble(-1));
    query.maxDurationMs = static_cast<qint64>(args.value(QStringLiteral("max_duration")).toDouble(-1));

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

    if (LogRecordSupport::sourceMatches(query.source, true)) {
        const QVector<Hit> eventHits = LogRecordSupport::queryEvents(query, &result);
        result.hits += eventHits;
    }

    if (LogRecordSupport::sourceMatches(query.source, false)) {
        const QVector<Hit> messageHits = LogRecordSupport::queryMessages(query, &result);
        result.hits += messageHits;
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
    return LogRecordSupport::formatResult(result);
}

QJsonObject LogQueryEngine::resultToJson(const Result& result)
{
    return LogRecordSupport::resultToJson(result);
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
