#include "LogScanner.h"
#include "LogFieldExtractor.h"
#include "LogSummarizer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace {

bool equalsIgnoreCase(const QString& a, const QString& b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

} // namespace

namespace LogScanner {

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

bool hitMatches(const LogQueryEngine::Hit& hit, const LogQueryEngine::Query& query, const QString& rawCompactLower)
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

    // level 过滤
    if (!query.level.isEmpty() && !equalsIgnoreCase(hit.level, query.level))
        return false;

    // duration 过滤
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

QVector<LogQueryEngine::Hit> scanEventFile(const QString& filePath, const LogQueryEngine::Query& query, LogQueryEngine::Result* result)
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
        LogQueryEngine::Hit hit;
        hit.source = QStringLiteral("event");
        hit.filePath = filePath;
        hit.lineNo = lineNo;
        hit.timestamp = LogFields::parseTimestampFromObject(obj);
        hit.timestampMs = toMs(hit.timestamp);
        hit.sessionId = LogFields::firstNonEmpty(QStringList()
                                      << LogFields::stringField(obj, QStringLiteral("session_id"))
                                      << LogFields::stringField(obj, QStringLiteral("sessionId")));
        hit.traceId = LogFields::stringField(obj, QStringLiteral("trace_id"));
        hit.turnId = LogFields::firstNonEmpty(QStringList()
                                   << LogFields::stringField(obj, QStringLiteral("turn_id"))
                                   << LogFields::stringField(obj, QStringLiteral("turnId")));
        hit.runId = LogFields::firstNonEmpty(QStringList()
                                  << LogFields::stringField(obj, QStringLiteral("run_id"))
                                  << LogFields::stringField(obj, QStringLiteral("runId")));
        hit.requestId = LogFields::extractRequestId(obj, true);
        hit.toolCallId = LogFields::extractToolCallId(obj, true);
        hit.actorId = LogFields::extractActorId(obj, true);
        hit.toolName = LogFields::extractToolName(obj, true);
        hit.eventType = LogFields::extractEventType(obj, true);
        hit.summary = LogSummarizer::summarizeEvent(obj);
        LogFields::extractSuccess(obj, true, &hit.successKnown, &hit.success);
        hit.level = LogFields::extractLevel(obj, true);
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

QVector<LogQueryEngine::Hit> scanSessionFile(const QString& sessionId, const QString& filePath, const LogQueryEngine::Query& query, LogQueryEngine::Result* result)
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
        const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
        const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();

        LogQueryEngine::Hit hit;
        hit.source = QStringLiteral("message");
        hit.filePath = filePath;
        hit.lineNo = lineNo;
        hit.timestamp = LogFields::parseTimestampFromObject(obj);
        hit.timestampMs = toMs(hit.timestamp);
        hit.sessionId = LogFields::firstNonEmpty(QStringList()
                                      << LogFields::stringField(obj, QStringLiteral("sessionId"))
                                      << sessionId);
        hit.traceId = LogFields::stringField(obj, QStringLiteral("traceId"));
        hit.turnId = LogFields::stringField(obj, QStringLiteral("turnId"));
        hit.runId = LogFields::stringField(payloadObj, QStringLiteral("run_id"));
        hit.requestId = LogFields::extractRequestId(obj, false);
        hit.toolCallId = LogFields::extractToolCallId(obj, false);
        hit.actorId = LogFields::firstNonEmpty(QStringList()
                                    << LogFields::stringField(obj, QStringLiteral("senderId"))
                                    << LogFields::stringField(obj, QStringLiteral("sender_id")));
        hit.toolName = LogFields::extractToolName(obj, false);
        hit.eventType = LogFields::extractEventType(obj, false);
        hit.summary = LogSummarizer::summarizeMessage(obj);
        LogFields::extractSuccess(obj, false, &hit.successKnown, &hit.success);
        hit.level = LogFields::extractLevel(obj, false);
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

qint64 toMs(const QDateTime& dt)
{
    if (!dt.isValid())
        return -1;
    return dt.toUTC().toMSecsSinceEpoch();
}

} // namespace LogScanner
