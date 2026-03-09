#include "LogFollower.h"

#include "LogFieldExtractor.h"
#include "LogDbUtils.h"
#include "LogScanner.h"
#include "LogSummarizer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QTimer>

namespace {

QDateTime parseTimestamp(const QJsonObject& obj)
{
    return LogFields::parseTimestampFromObject(obj);
}

QString extractEventType(const QJsonObject& obj)
{
    return LogFields::extractEventType(obj, true);
}

QString extractToolName(const QJsonObject& obj)
{
    return LogFields::extractToolName(obj, true);
}

QString extractLevel(const QJsonObject& obj)
{
    return LogFields::extractLevel(obj, true);
}

QString clip(const QString& text, int maxChars)
{
    const QString simplified = text.simplified();
    if (maxChars <= 0 || simplified.size() <= maxChars)
        return simplified;
    return simplified.left(maxChars) + QStringLiteral("...");
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

bool matchesFilter(const QJsonObject& obj, const LogQueryEngine::Query& filter)
{
    LogQueryEngine::Hit hit;
    hit.source = QStringLiteral("event");
    hit.timestamp = parseTimestamp(obj);
    hit.timestampMs = hit.timestamp.isValid() ? hit.timestamp.toUTC().toMSecsSinceEpoch() : -1;
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
    hit.toolName = extractToolName(obj);
    hit.eventType = extractEventType(obj);
    hit.summary = LogSummarizer::summarizeEvent(obj);
    LogFields::extractSuccess(obj, true, &hit.successKnown, &hit.success);
    hit.level = extractLevel(obj);
    hit.durationMs = extractDuration(obj);

    const QString rawCompactLower = QString::fromUtf8(
                                        QJsonDocument(obj).toJson(QJsonDocument::Compact))
                                        .toLower();

    if (!LogScanner::withinTimeRange(hit.timestamp, filter))
        return false;
    return LogScanner::hitMatches(hit, filter, rawCompactLower);
}

} // namespace

LogFollower::LogFollower(const LogQueryEngine::Query& filter,
                         const QString& dataRootPath,
                         QObject* parent)
    : QObject(parent)
    , m_filter(filter)
    , m_dataRootPath(dataRootPath)
{
    if (m_dataRootPath.isEmpty())
        m_dataRootPath = QDir::home().filePath(QStringLiteral(".tmagent"));
}

void LogFollower::start()
{
    QTextStream err(stderr);

    QString dbError;
    QSqlDatabase db = LogDbUtils::openConnection(m_dataRootPath, &dbError);
    if (!db.isValid() || !db.isOpen()) {
        err << QStringLiteral("SQLite 连接失败，无法 follow events: %1\n").arg(dbError);
        err.flush();
        QCoreApplication::quit();
        return;
    }

    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT COALESCE(MAX(id), 0) FROM events")) && q.next())
        m_anchorId = q.value(0).toLongLong();
    else
        m_anchorId = 0;

    err << QStringLiteral("Following: sqlite://events (anchor_id=%1)\n").arg(m_anchorId);
    err << QStringLiteral("Press Ctrl+C to stop.\n");
    err.flush();

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &LogFollower::pollNewEvents);
    m_pollTimer->start();
}

void LogFollower::pollNewEvents()
{
    QString dbError;
    QSqlDatabase db = LogDbUtils::openConnection(m_dataRootPath, &dbError);
    if (!db.isValid() || !db.isOpen())
        return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, raw_json FROM events "
        "WHERE id > ? "
        "ORDER BY id ASC "
        "LIMIT 200"));
    q.addBindValue(m_anchorId);

    if (!q.exec())
        return;

    qint64 latestId = m_anchorId;
    while (q.next()) {
        const qint64 id = q.value(0).toLongLong();
        if (id > latestId)
            latestId = id;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(1).toString().toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        if (!matchesFilter(obj, m_filter))
            continue;

        const QDateTime timestamp = parseTimestamp(obj);
        const QString level = extractLevel(obj);
        const QString eventType = extractEventType(obj);
        const QString toolName = extractToolName(obj);

        QString summary = LogFields::stringField(obj, QStringLiteral("summary"));
        if (summary.isEmpty()) {
            const QString error = LogFields::stringField(obj, QStringLiteral("error"));
            if (!error.isEmpty())
                summary = QStringLiteral("error=%1").arg(error);
            else
                summary = LogSummarizer::summarizeEvent(obj);
        }

        const QString timeStr = timestamp.isValid()
            ? timestamp.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz"))
            : QStringLiteral("??:??:??.???");

        QTextStream out(stdout);
        out << QStringLiteral("[%1] [%2] [%3] [%4] %5\n")
                   .arg(timeStr,
                        level.isEmpty() ? QStringLiteral("-") : level,
                        eventType.isEmpty() ? QStringLiteral("-") : eventType,
                        toolName.isEmpty() ? QStringLiteral("-") : toolName,
                        clip(summary, 200));
        out.flush();
    }

    m_anchorId = latestId;
}
