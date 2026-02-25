#ifndef EVENTLOGTOOL_H
#define EVENTLOGTOOL_H

#include "core/logging/LogQueryEngine.h"
#include "core/logging/LogSessionLister.h"
#include <QJsonObject>
#include <QString>

class EventLogTool {
public:
    static QString executeSearch(const QJsonObject& args)
    {
        // LLM 场景默认 json 格式、limit=20，减少 token 消耗
        QJsonObject adjusted = args;
        if (!adjusted.contains(QStringLiteral("format")))
            adjusted.insert(QStringLiteral("format"), QStringLiteral("json"));
        if (!adjusted.contains(QStringLiteral("limit")))
            adjusted.insert(QStringLiteral("limit"), 20);

        QString error;
        const LogQueryEngine::Query query = LogQueryEngine::queryFromJson(adjusted, &error);
        if (!error.isEmpty())
            return QStringLiteral("错误: %1").arg(error);

        const LogQueryEngine::Result result = LogQueryEngine::execute(query);
        return LogQueryEngine::formatResult(result);
    }

    static QString listSessions(const QJsonObject& args)
    {
        const QString dataRoot = args.value(QStringLiteral("data_root")).toString().trimmed();
        const QString format = args.value(QStringLiteral("format")).toString().trimmed().toLower();

        const LogSessionLister::ListResult result = LogSessionLister::listSessions(dataRoot);

        if (format == QLatin1String("table"))
            return LogSessionLister::formatTable(result);
        return LogSessionLister::formatJson(result);
    }
};

#endif // EVENTLOGTOOL_H
