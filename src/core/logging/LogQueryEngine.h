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
        QString level;              // 过滤条件：仅返回指定级别
        qint64 minDurationMs = -1;  // 最小耗时过滤
        qint64 maxDurationMs = -1;  // 最大耗时过滤
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
        QString level;           // info/warning/error/debug
        qint64 durationMs = -1;  // 工具执行耗时（毫秒）
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
};

#endif // LOGQUERYENGINE_H
