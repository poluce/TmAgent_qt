#include "core/logging/LogAgentLister.h"
#include "core/logging/LogFollower.h"
#include "core/logging/LogQueryEngine.h"
#include "core/logging/LogSessionLister.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonObject>
#include <QTextStream>
#include <csignal>

// ── SIGINT 优雅退出 ──────────────────────────────────────────────
static void signalHandler(int) { QCoreApplication::quit(); }

// ── 子命令枚举 ──────────────────────────────────────────────────
enum class SubCommand { Search, Sessions, Agents, Follow, Reindex, Cleanup, Help };

static SubCommand parseSubCommand(const QString& arg)
{
    const QString lower = arg.trimmed().toLower();
    if (lower == QLatin1String("search"))   return SubCommand::Search;
    if (lower == QLatin1String("sessions")) return SubCommand::Sessions;
    if (lower == QLatin1String("agents"))   return SubCommand::Agents;
    if (lower == QLatin1String("follow"))   return SubCommand::Follow;
    if (lower == QLatin1String("reindex"))  return SubCommand::Reindex;
    if (lower == QLatin1String("cleanup"))  return SubCommand::Cleanup;
    if (lower == QLatin1String("help"))     return SubCommand::Help;
    return SubCommand::Search; // 未知子命令时默认 search
}

static bool isKnownSubCommand(const QString& arg)
{
    static const QStringList known = {
        QStringLiteral("search"), QStringLiteral("sessions"),
        QStringLiteral("agents"),
        QStringLiteral("follow"), QStringLiteral("reindex"),
        QStringLiteral("cleanup"), QStringLiteral("help")
    };
    return known.contains(arg.trimmed().toLower());
}

// ── 通用帮助 ─────────────────────────────────────────────────────
static void printUsage(QTextStream& out)
{
    out << QStringLiteral("Usage: tmagent-log <command> [options]\n\n");
    out << QStringLiteral("Commands:\n");
    out << QStringLiteral("  search     按条件检索 events/messages（默认子命令）\n");
    out << QStringLiteral("  sessions   列出所有会话\n");
    out << QStringLiteral("  agents     查询 Agent 信息\n");
    out << QStringLiteral("  follow     实时跟踪 SQLite events 增量记录\n");
    out << QStringLiteral("  reindex    重建索引（预留）\n");
    out << QStringLiteral("  cleanup    清理旧数据（预留）\n");
    out << QStringLiteral("  help       显示此帮助\n\n");
    out << QStringLiteral("Run 'tmagent-log <command> --help' for command-specific options.\n");
    out.flush();
}

// ── main ─────────────────────────────────────────────────────────
int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tmagent-log"));
    app.setApplicationVersion(QStringLiteral("2.0"));

    QTextStream out(stdout);
    QTextStream err(stderr);
    out.setCodec("UTF-8");
    err.setCodec("UTF-8");

    // 确定子命令：argv[1] 如果是已知子命令则消费它，否则默认 search
    SubCommand subCmd = SubCommand::Search;
    bool hasExplicitSubCmd = false;

    if (argc >= 2) {
        const QString firstArg = QString::fromLocal8Bit(argv[1]);
        if (isKnownSubCommand(firstArg)) {
            subCmd = parseSubCommand(firstArg);
            hasExplicitSubCmd = true;
        }
    }

    // 构建去掉子命令名的参数列表，供 QCommandLineParser 使用
    auto buildArgs = [&]() {
        QStringList args;
        args << QString::fromLocal8Bit(argv[0]);
        for (int i = (hasExplicitSubCmd ? 2 : 1); i < argc; ++i)
            args << QString::fromLocal8Bit(argv[i]);
        return args;
    };

    switch (subCmd) {
    case SubCommand::Search: {
        const QStringList args = buildArgs();

        QCommandLineParser parser;
        parser.setApplicationDescription(
            QStringLiteral("tmagent-log search - 按条件检索 events/messages"));
        parser.addHelpOption();

        QCommandLineOption sourceOpt(QStringList() << QStringLiteral("s") << QStringLiteral("source"),
            QStringLiteral("日志源: all/events/messages（默认 all）"),
            QStringLiteral("source"), QStringLiteral("all"));
        QCommandLineOption sessionOpt(QStringList() << QStringLiteral("session-id"),
            QStringLiteral("会话 ID"), QStringLiteral("session_id"));
        QCommandLineOption traceOpt(QStringList() << QStringLiteral("trace-id"),
            QStringLiteral("trace_id"), QStringLiteral("trace_id"));
        QCommandLineOption turnOpt(QStringList() << QStringLiteral("turn-id"),
            QStringLiteral("turn_id"), QStringLiteral("turn_id"));
        QCommandLineOption runOpt(QStringList() << QStringLiteral("run-id"),
            QStringLiteral("run_id"), QStringLiteral("run_id"));
        QCommandLineOption reqOpt(QStringList() << QStringLiteral("request-id"),
            QStringLiteral("request_id"), QStringLiteral("request_id"));
        QCommandLineOption toolCallOpt(QStringList() << QStringLiteral("tool-call-id"),
            QStringLiteral("tool_call_id"), QStringLiteral("tool_call_id"));
        QCommandLineOption actorOpt(QStringList() << QStringLiteral("actor-id"),
            QStringLiteral("actor_id（执行者身份）"), QStringLiteral("actor_id"));
        QCommandLineOption toolNameOpt(QStringList() << QStringLiteral("tool-name"),
            QStringLiteral("工具名"), QStringLiteral("tool_name"));
        QCommandLineOption eventTypeOpt(QStringList() << QStringLiteral("event-type"),
            QStringLiteral("事件类型"), QStringLiteral("event_type"));
        QCommandLineOption levelOpt(QStringList() << QStringLiteral("level"),
            QStringLiteral("日志级别过滤"), QStringLiteral("level"));
        QCommandLineOption keywordOpt(QStringList() << QStringLiteral("k") << QStringLiteral("keyword"),
            QStringLiteral("关键词模糊检索"), QStringLiteral("keyword"));
        QCommandLineOption fromOpt(QStringList() << QStringLiteral("time-from"),
            QStringLiteral("开始时间（ISO8601 或 epoch 秒/毫秒）"), QStringLiteral("time_from"));
        QCommandLineOption toOpt(QStringList() << QStringLiteral("time-to"),
            QStringLiteral("结束时间（ISO8601 或 epoch 秒/毫秒）"), QStringLiteral("time_to"));
        QCommandLineOption minDurOpt(QStringList() << QStringLiteral("min-duration"),
            QStringLiteral("最小耗时（毫秒）"), QStringLiteral("ms"));
        QCommandLineOption maxDurOpt(QStringList() << QStringLiteral("max-duration"),
            QStringLiteral("最大耗时（毫秒）"), QStringLiteral("ms"));
        QCommandLineOption limitOpt(QStringList() << QStringLiteral("n") << QStringLiteral("limit"),
            QStringLiteral("返回条数上限（默认 50）"), QStringLiteral("limit"), QStringLiteral("50"));
        QCommandLineOption orderOpt(QStringList() << QStringLiteral("order"),
            QStringLiteral("排序: desc/asc（默认 desc）"), QStringLiteral("order"), QStringLiteral("desc"));
        QCommandLineOption formatOpt(QStringList() << QStringLiteral("f") << QStringLiteral("format"),
            QStringLiteral("输出格式: report/table/json/raw（默认 report）"),
            QStringLiteral("format"), QStringLiteral("report"));
        QCommandLineOption includeRawOpt(QStringList() << QStringLiteral("include-raw"),
            QStringLiteral("附带 raw JSON 输出"));
        QCommandLineOption dataRootOpt(QStringList() << QStringLiteral("data-root"),
            QStringLiteral("数据根目录（默认 ~/.tmagent）"), QStringLiteral("data_root"));

        parser.addOptions(QList<QCommandLineOption>()
            << sourceOpt << sessionOpt << traceOpt << turnOpt << runOpt
            << reqOpt << toolCallOpt << actorOpt << toolNameOpt << eventTypeOpt
            << levelOpt << keywordOpt << fromOpt << toOpt
            << minDurOpt << maxDurOpt
            << limitOpt << orderOpt << formatOpt << includeRawOpt << dataRootOpt);

        parser.process(args);

        QJsonObject jsonArgs;
        jsonArgs.insert(QStringLiteral("source"),       parser.value(sourceOpt));
        jsonArgs.insert(QStringLiteral("session_id"),   parser.value(sessionOpt));
        jsonArgs.insert(QStringLiteral("trace_id"),     parser.value(traceOpt));
        jsonArgs.insert(QStringLiteral("turn_id"),      parser.value(turnOpt));
        jsonArgs.insert(QStringLiteral("run_id"),       parser.value(runOpt));
        jsonArgs.insert(QStringLiteral("request_id"),   parser.value(reqOpt));
        jsonArgs.insert(QStringLiteral("tool_call_id"), parser.value(toolCallOpt));
        jsonArgs.insert(QStringLiteral("actor_id"),     parser.value(actorOpt));
        jsonArgs.insert(QStringLiteral("tool_name"),    parser.value(toolNameOpt));
        jsonArgs.insert(QStringLiteral("event_type"),   parser.value(eventTypeOpt));
        jsonArgs.insert(QStringLiteral("level"),        parser.value(levelOpt));
        jsonArgs.insert(QStringLiteral("keyword"),      parser.value(keywordOpt));
        jsonArgs.insert(QStringLiteral("time_from"),    parser.value(fromOpt));
        jsonArgs.insert(QStringLiteral("time_to"),      parser.value(toOpt));
        if (parser.isSet(minDurOpt))
            jsonArgs.insert(QStringLiteral("min_duration"), parser.value(minDurOpt).toLongLong());
        if (parser.isSet(maxDurOpt))
            jsonArgs.insert(QStringLiteral("max_duration"), parser.value(maxDurOpt).toLongLong());
        jsonArgs.insert(QStringLiteral("limit"),        parser.value(limitOpt).toInt());
        jsonArgs.insert(QStringLiteral("order"),        parser.value(orderOpt));
        jsonArgs.insert(QStringLiteral("format"),       parser.value(formatOpt));
        jsonArgs.insert(QStringLiteral("include_raw"),  parser.isSet(includeRawOpt));
        jsonArgs.insert(QStringLiteral("data_root"),    parser.value(dataRootOpt));

        QString error;
        const LogQueryEngine::Query query = LogQueryEngine::queryFromJson(jsonArgs, &error);
        if (!error.isEmpty()) {
            err << QStringLiteral("参数错误: ") << error << QStringLiteral("\n");
            return 2;
        }

        const LogQueryEngine::Result result = LogQueryEngine::execute(query);
        out << LogQueryEngine::formatResult(result) << QStringLiteral("\n");
        return 0;
    }

    case SubCommand::Sessions: {
        const QStringList args = buildArgs();

        QCommandLineParser parser;
        parser.setApplicationDescription(
            QStringLiteral("tmagent-log sessions - 列出所有会话"));
        parser.addHelpOption();

        QCommandLineOption formatOpt(QStringList() << QStringLiteral("f") << QStringLiteral("format"),
            QStringLiteral("输出格式: table/json（默认 table）"),
            QStringLiteral("format"), QStringLiteral("table"));
        QCommandLineOption dataRootOpt(QStringList() << QStringLiteral("data-root"),
            QStringLiteral("数据根目录（默认 ~/.tmagent）"), QStringLiteral("data_root"));

        parser.addOptions(QList<QCommandLineOption>() << formatOpt << dataRootOpt);
        parser.process(args);

        const QString dataRoot = parser.value(dataRootOpt);
        const QString format = parser.value(formatOpt).trimmed().toLower();

        const LogSessionLister::ListResult result = LogSessionLister::listSessions(dataRoot);

        if (format == QLatin1String("json"))
            out << LogSessionLister::formatJson(result);
        else
            out << LogSessionLister::formatTable(result);
        out << QStringLiteral("\n");
        return 0;
    }

    case SubCommand::Agents: {
        const QStringList args = buildArgs();

        QCommandLineParser parser;
        parser.setApplicationDescription(
            QStringLiteral("tmagent-log agents - 查询 Agent 信息"));
        parser.addHelpOption();

        QCommandLineOption formatOpt(QStringList() << QStringLiteral("f") << QStringLiteral("format"),
            QStringLiteral("输出格式: table/json/report（默认 table）"),
            QStringLiteral("format"), QStringLiteral("table"));
        QCommandLineOption agentIdOpt(QStringList() << QStringLiteral("agent-id"),
            QStringLiteral("按 Agent UUID 查询详情"),
            QStringLiteral("agent_id"));
        QCommandLineOption agentNameOpt(QStringList() << QStringLiteral("agent-name"),
            QStringLiteral("按 Agent 名称模糊查询"),
            QStringLiteral("agent_name"));
        QCommandLineOption dataRootOpt(QStringList() << QStringLiteral("data-root"),
            QStringLiteral("数据根目录（默认 ~/.tmagent）"),
            QStringLiteral("data_root"));

        parser.addOptions(QList<QCommandLineOption>()
            << formatOpt << agentIdOpt << agentNameOpt << dataRootOpt);
        parser.process(args);

        LogAgentLister::QueryOptions opts;
        opts.dataRootPath = parser.value(dataRootOpt);
        opts.filterAgentId = parser.value(agentIdOpt);
        opts.filterAgentName = parser.value(agentNameOpt);
        opts.detail = !opts.filterAgentId.isEmpty() || !opts.filterAgentName.isEmpty();

        const LogAgentLister::ListResult result = LogAgentLister::listAgents(opts);
        const QString format = parser.value(formatOpt).trimmed().toLower();

        if (format == QLatin1String("json"))
            out << LogAgentLister::formatJson(result);
        else if (format == QLatin1String("report"))
            out << LogAgentLister::formatReport(result);
        else
            out << LogAgentLister::formatTable(result);
        out << QStringLiteral("\n");
        return 0;
    }

    case SubCommand::Follow: {
        const QStringList args = buildArgs();

        QCommandLineParser parser;
        parser.setApplicationDescription(
            QStringLiteral("tmagent-log follow - 实时跟踪 SQLite events 增量记录"));
        parser.addHelpOption();

        QCommandLineOption sessionOpt(QStringList() << QStringLiteral("session-id"),
            QStringLiteral("会话 ID"), QStringLiteral("session_id"));
        QCommandLineOption toolNameOpt(QStringList() << QStringLiteral("tool-name"),
            QStringLiteral("工具名"), QStringLiteral("tool_name"));
        QCommandLineOption eventTypeOpt(QStringList() << QStringLiteral("event-type"),
            QStringLiteral("事件类型"), QStringLiteral("event_type"));
        QCommandLineOption levelOpt(QStringList() << QStringLiteral("level"),
            QStringLiteral("日志级别过滤"), QStringLiteral("level"));
        QCommandLineOption actorOpt(QStringList() << QStringLiteral("actor-id"),
            QStringLiteral("actor_id"), QStringLiteral("actor_id"));
        QCommandLineOption keywordOpt(QStringList() << QStringLiteral("k") << QStringLiteral("keyword"),
            QStringLiteral("关键词模糊检索"), QStringLiteral("keyword"));
        QCommandLineOption dataRootOpt(QStringList() << QStringLiteral("data-root"),
            QStringLiteral("数据根目录（默认 ~/.tmagent）"), QStringLiteral("data_root"));

        parser.addOptions(QList<QCommandLineOption>()
            << sessionOpt << toolNameOpt << eventTypeOpt
            << levelOpt << actorOpt << keywordOpt << dataRootOpt);

        parser.process(args);

        LogQueryEngine::Query filter;
        filter.sessionId = parser.value(sessionOpt);
        filter.toolName  = parser.value(toolNameOpt);
        filter.eventType = parser.value(eventTypeOpt);
        filter.level     = parser.value(levelOpt);
        filter.actorId   = parser.value(actorOpt);
        filter.keyword   = parser.value(keywordOpt);

        const QString dataRoot = parser.value(dataRootOpt);

        signal(SIGINT, signalHandler);
#ifdef SIGTERM
        signal(SIGTERM, signalHandler);
#endif

        LogFollower follower(filter, dataRoot);
        follower.start();
        return app.exec();
    }

    case SubCommand::Reindex:
        err << QStringLiteral("reindex 子命令尚未实现（预留）。\n");
        return 1;

    case SubCommand::Cleanup:
        err << QStringLiteral("cleanup 子命令尚未实现（预留）。\n");
        return 1;

    case SubCommand::Help:
        printUsage(out);
        return 0;
    }

    printUsage(out);
    return 0;
}
