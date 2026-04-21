#ifndef EVENTLOGTOOL_H
#define EVENTLOGTOOL_H

#include <tmagent/support/ToolSchemaBuilder.h>
#include "LogCatalog.h"
#include "LogQueryEngine.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

class EventLogTool {
public:
    static TmAgent::Tool toolSchema()
    {
        using namespace TmAgent;
        QJsonObject properties;
        properties.insert(
            QStringLiteral("action"),
            makePropertySchema(
                QStringLiteral("string"),
                QStringLiteral("操作: search(默认) / sessions / agents")));
        
        Tool tool;
        tool.name = QStringLiteral("event_log");
        tool.description = QStringLiteral("日志查询。通过 action 选择操作: search、sessions、agents。");
        tool.inputSchema = makeToolSchema(
            QStringLiteral("event_log"),
            QStringLiteral("日志查询。通过 action 选择操作: search、sessions、agents。"),
            properties);
        return tool;
    }

    static QString execute(const QJsonObject& args)
    {
        const QString action = args.value(QStringLiteral("action"))
                                   .toString().trimmed().toLower();

        if (action == QLatin1String("sessions"))  return doListSessions(args);
        if (action == QLatin1String("agents"))    return doListAgents(args);
        if (action == QLatin1String("search") || action.isEmpty())
            return doSearch(args);

        return makeError(QStringLiteral("bad_args"),
            QStringLiteral("未知 action: '%1'。可选: search, sessions, agents").arg(action));
    }

private:
    static QString doSearch(const QJsonObject& args)
    {
        QJsonObject adjusted = args;
        adjusted.remove(QStringLiteral("action"));
        if (!adjusted.contains(QStringLiteral("format")))
            adjusted.insert(QStringLiteral("format"), QStringLiteral("json"));
        if (!adjusted.contains(QStringLiteral("limit")))
            adjusted.insert(QStringLiteral("limit"), 20);

        QString error;
        const auto query = LogQueryEngine::queryFromJson(adjusted, &error);
        if (!error.isEmpty())
            return makeError(QStringLiteral("bad_args"), error);

        const auto result = LogQueryEngine::execute(query);

        if (result.hits.isEmpty()) {
            QJsonObject root;
            root[QStringLiteral("status")] = QStringLiteral("successful");
            root[QStringLiteral("data")] = QJsonArray();
            root[QStringLiteral("hint")] = QStringLiteral(
                "无匹配结果。search 支持的过滤参数: "
                "session_id, trace_id, turn_id, run_id, request_id, "
                "tool_call_id, actor_id, tool_name, event_type, keyword, "
                "time_from, time_to, limit(默认20), order(desc/asc), "
                "source(all/events/messages), include_raw(bool)。"
                "建议先用 action=sessions 查看可用会话。");
            return QString::fromUtf8(
                QJsonDocument(root).toJson(QJsonDocument::Compact));
        }

        return makeSuccess(LogQueryEngine::formatResult(result));
    }

    static QString doListSessions(const QJsonObject& args)
    {
        const QString dataRoot = args.value(QStringLiteral("data_root"))
                                     .toString().trimmed();
        const auto result = LogCatalog::listSessions(dataRoot);
        return makeSuccess(LogCatalog::formatJson(result));
    }

    static QString doListAgents(const QJsonObject& args)
    {
        LogCatalog::AgentQueryOptions opts;
        opts.dataRootPath  = args.value(QStringLiteral("data_root")).toString().trimmed();
        opts.filterAgentId   = args.value(QStringLiteral("agent_id")).toString().trimmed();
        opts.filterAgentName = args.value(QStringLiteral("agent_name")).toString().trimmed();
        opts.detail = !opts.filterAgentId.isEmpty() || !opts.filterAgentName.isEmpty();

        const auto result = LogCatalog::listAgents(opts);
        return makeSuccess(LogCatalog::formatJson(result));
    }

    static QString makeSuccess(const QString& data)
    {
        QJsonObject root;
        root[QStringLiteral("status")] = QStringLiteral("successful");
        const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
        if (doc.isArray())
            root[QStringLiteral("data")] = doc.array();
        else if (doc.isObject())
            root[QStringLiteral("data")] = doc.object();
        else
            root[QStringLiteral("data")] = data;
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    static QString makeError(const QString& category, const QString& message)
    {
        QJsonObject errObj;
        errObj[QStringLiteral("category")] = category;
        errObj[QStringLiteral("message")]  = message;
        QJsonObject root;
        root[QStringLiteral("status")] = QStringLiteral("failed");
        root[QStringLiteral("error")]  = errObj;
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
};

#endif // EVENTLOGTOOL_H
