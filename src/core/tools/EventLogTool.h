#ifndef EVENTLOGTOOL_H
#define EVENTLOGTOOL_H

#include "core/logging/LogQueryEngine.h"
#include <QJsonObject>
#include <QString>

class EventLogTool {
public:
    static QString executeSearch(const QJsonObject& args)
    {
        QString error;
        const LogQueryEngine::Query query = LogQueryEngine::queryFromJson(args, &error);
        if (!error.isEmpty())
            return QStringLiteral("错误: %1").arg(error);

        const LogQueryEngine::Result result = LogQueryEngine::execute(query);
        return LogQueryEngine::formatResult(result);
    }
};

#endif // EVENTLOGTOOL_H
