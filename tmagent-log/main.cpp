#include "core/logging/LogQueryEngine.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonObject>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("tmagent-log"));
    app.setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("TmAgent 日志筛选工具（按条件检索 events/messages）"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption sourceOpt(QStringList() << QStringLiteral("s")
                                                << QStringLiteral("source"),
        QStringLiteral("日志源: all/events/messages（默认 all）"),
        QStringLiteral("source"),
        QStringLiteral("all"));
    QCommandLineOption sessionOpt(QStringList() << QStringLiteral("session-id"),
        QStringLiteral("会话 ID"),
        QStringLiteral("session_id"));
    QCommandLineOption traceOpt(QStringList() << QStringLiteral("trace-id"),
        QStringLiteral("trace_id"),
        QStringLiteral("trace_id"));
    QCommandLineOption turnOpt(QStringList() << QStringLiteral("turn-id"),
        QStringLiteral("turn_id"),
        QStringLiteral("turn_id"));
    QCommandLineOption runOpt(QStringList() << QStringLiteral("run-id"),
        QStringLiteral("run_id"),
        QStringLiteral("run_id"));
    QCommandLineOption reqOpt(QStringList() << QStringLiteral("request-id"),
        QStringLiteral("request_id"),
        QStringLiteral("request_id"));
    QCommandLineOption toolCallOpt(QStringList() << QStringLiteral("tool-call-id"),
        QStringLiteral("tool_call_id"),
        QStringLiteral("tool_call_id"));
    QCommandLineOption actorOpt(QStringList() << QStringLiteral("actor-id"),
        QStringLiteral("actor_id（执行者身份）"),
        QStringLiteral("actor_id"));
    QCommandLineOption toolNameOpt(QStringList() << QStringLiteral("tool-name"),
        QStringLiteral("工具名"),
        QStringLiteral("tool_name"));
    QCommandLineOption eventTypeOpt(QStringList() << QStringLiteral("event-type"),
        QStringLiteral("事件类型"),
        QStringLiteral("event_type"));
    QCommandLineOption keywordOpt(QStringList() << QStringLiteral("k")
                                                 << QStringLiteral("keyword"),
        QStringLiteral("关键词模糊检索"),
        QStringLiteral("keyword"));
    QCommandLineOption fromOpt(QStringList() << QStringLiteral("time-from"),
        QStringLiteral("开始时间（ISO8601 或 epoch 秒/毫秒）"),
        QStringLiteral("time_from"));
    QCommandLineOption toOpt(QStringList() << QStringLiteral("time-to"),
        QStringLiteral("结束时间（ISO8601 或 epoch 秒/毫秒）"),
        QStringLiteral("time_to"));
    QCommandLineOption limitOpt(QStringList() << QStringLiteral("n")
                                               << QStringLiteral("limit"),
        QStringLiteral("返回条数上限（默认 50）"),
        QStringLiteral("limit"),
        QStringLiteral("50"));
    QCommandLineOption orderOpt(QStringList() << QStringLiteral("order"),
        QStringLiteral("排序: desc/asc（默认 desc）"),
        QStringLiteral("order"),
        QStringLiteral("desc"));
    QCommandLineOption formatOpt(QStringList() << QStringLiteral("f")
                                                << QStringLiteral("format"),
        QStringLiteral("输出格式: report/table/json/raw（默认 report）"),
        QStringLiteral("format"),
        QStringLiteral("report"));
    QCommandLineOption includeRawOpt(QStringList() << QStringLiteral("include-raw"),
        QStringLiteral("附带 raw JSON 输出"));
    QCommandLineOption dataRootOpt(QStringList() << QStringLiteral("data-root"),
        QStringLiteral("数据根目录（默认 ~/.tmagent）"),
        QStringLiteral("data_root"));

    parser.addOptions(QList<QCommandLineOption>()
                      << sourceOpt
                      << sessionOpt
                      << traceOpt
                      << turnOpt
                      << runOpt
                      << reqOpt
                      << toolCallOpt
                      << actorOpt
                      << toolNameOpt
                      << eventTypeOpt
                      << keywordOpt
                      << fromOpt
                      << toOpt
                      << limitOpt
                      << orderOpt
                      << formatOpt
                      << includeRawOpt
                      << dataRootOpt);

    parser.process(app);

    QJsonObject args;
    args.insert(QStringLiteral("source"), parser.value(sourceOpt));
    args.insert(QStringLiteral("session_id"), parser.value(sessionOpt));
    args.insert(QStringLiteral("trace_id"), parser.value(traceOpt));
    args.insert(QStringLiteral("turn_id"), parser.value(turnOpt));
    args.insert(QStringLiteral("run_id"), parser.value(runOpt));
    args.insert(QStringLiteral("request_id"), parser.value(reqOpt));
    args.insert(QStringLiteral("tool_call_id"), parser.value(toolCallOpt));
    args.insert(QStringLiteral("actor_id"), parser.value(actorOpt));
    args.insert(QStringLiteral("tool_name"), parser.value(toolNameOpt));
    args.insert(QStringLiteral("event_type"), parser.value(eventTypeOpt));
    args.insert(QStringLiteral("keyword"), parser.value(keywordOpt));
    args.insert(QStringLiteral("time_from"), parser.value(fromOpt));
    args.insert(QStringLiteral("time_to"), parser.value(toOpt));
    args.insert(QStringLiteral("limit"), parser.value(limitOpt).toInt());
    args.insert(QStringLiteral("order"), parser.value(orderOpt));
    args.insert(QStringLiteral("format"), parser.value(formatOpt));
    args.insert(QStringLiteral("include_raw"), parser.isSet(includeRawOpt));
    args.insert(QStringLiteral("data_root"), parser.value(dataRootOpt));

    QString error;
    const LogQueryEngine::Query query = LogQueryEngine::queryFromJson(args, &error);
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (!error.isEmpty()) {
        err << "参数错误: " << error << "\n";
        return 2;
    }

    const LogQueryEngine::Result result = LogQueryEngine::execute(query);
    out << LogQueryEngine::formatResult(result) << "\n";
    return 0;
}
