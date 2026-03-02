#ifndef LOGQUERYENGINE_H
#define LOGQUERYENGINE_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

class LogQueryEngine {
public:
    enum class OutputFormat {
        Report,
        Table,
        Json,
        Raw
    };

    struct Query {
        QString dataRootPath;
        QString source = QStringLiteral("all"); // all/events/messages
        QString sessionId;
        QString traceId;
        QString turnId;
        QString runId;
        QString requestId;
        QString toolCallId;
        QString actorId;
        QString toolName;
        QString eventType;
        QString keyword;
        QDateTime timeFrom;
        QDateTime timeTo;
        int limit = 50;
        bool ascending = false;
        bool includeRaw = false;
        OutputFormat format = OutputFormat::Report;
    };

    struct Hit {
        QString source; // event/message
        QString filePath;
        int lineNo = 0;
        QDateTime timestamp;
        qint64 timestampMs = -1;
        QString sessionId;
        QString traceId;
        QString turnId;
        QString runId;
        QString requestId;
        QString toolCallId;
        QString actorId;
        QString toolName;
        QString eventType;
        QString summary;
        bool successKnown = false;
        bool success = false;
        QJsonObject raw;
    };

    struct Result {
        Query query;
        QVector<Hit> hits;
        QStringList warnings;
        int scannedFiles = 0;
        qint64 scannedLines = 0;
    };

    static Query queryFromJson(const QJsonObject& args, QString* error = nullptr);
    static Result execute(const Query& query);

    static QString formatResult(const Result& result);
    static QJsonObject resultToJson(const Result& result);

    static bool parseDateTimeArg(const QString& raw, QDateTime* out);

private:
    static bool sourceMatches(const QString& source, bool isEvent);
    static bool withinTimeRange(const QDateTime& timestamp, const Query& query);
    static bool hitMatches(const Hit& hit, const Query& query, const QString& rawCompactLower);

    static QVector<Hit> scanEventFile(const QString& filePath, const Query& query, Result* result);
    static QVector<Hit> scanSessionFile(const QString& sessionId, const QString& filePath, const Query& query, Result* result);

    static QString extractEventType(const QJsonObject& obj, bool isEventSource);
    static QString extractToolName(const QJsonObject& obj, bool isEventSource);
    static QString extractToolCallId(const QJsonObject& obj, bool isEventSource);
    static QString extractRequestId(const QJsonObject& obj, bool isEventSource);
    static QString extractActorId(const QJsonObject& obj, bool isEventSource);
    static bool extractSuccess(const QJsonObject& obj, bool isEventSource, bool* known, bool* value);

    static QString valueAtPath(const QJsonObject& obj, const QStringList& path);
    static QString firstNonEmpty(const QStringList& candidates);
    static QString summarizeEvent(const QJsonObject& obj);
    static QString summarizeMessage(const QJsonObject& obj);
    static QString clip(const QString& text, int maxChars);
    static qint64 toMs(const QDateTime& dt);
};

#endif // LOGQUERYENGINE_H
