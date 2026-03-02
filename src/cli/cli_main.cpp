#include "CliRunner.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>
#include <cstdio>

static void printUsage()
{
    std::fputs(
        "Usage: TmAgentCli [options] <task>\n"
        "\n"
        "Options:\n"
        "  --model-config <path>   Model config YAML (default: ./resources/models.yaml)\n"
        "  --config-id <id>        Model config ID (default: YAML default)\n"
        "  --system-prompt <text>  System prompt override\n"
        "  --workspace <dir>       Workspace directory (default: current dir)\n"
        "  --timeout <ms>          Timeout in milliseconds (default: 300000)\n"
        "  --allowed-tools <list>  Comma-separated tool whitelist\n"
        "  --no-tools              Disable all tools\n"
        "  --verbose               Verbose logging to stderr\n"
        "  --stdin                 Read task from stdin\n"
        "  --help                  Show this help\n"
        "\n"
        "Exit codes: 0=success, 1=error, 2=timeout, 3=bad arguments\n",
        stderr);
}

static CliRunner::Options parseArgs(const QStringList& args, bool* ok)
{
    CliRunner::Options opts;
    *ok = true;

    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args[i];

        if (arg == QStringLiteral("--model-config") && i + 1 < args.size()) {
            opts.modelConfigPath = args[++i];
        } else if (arg == QStringLiteral("--config-id") && i + 1 < args.size()) {
            opts.configId = args[++i];
        } else if (arg == QStringLiteral("--system-prompt") && i + 1 < args.size()) {
            opts.systemPrompt = args[++i];
        } else if (arg == QStringLiteral("--workspace") && i + 1 < args.size()) {
            opts.workspaceDir = args[++i];
        } else if (arg == QStringLiteral("--timeout") && i + 1 < args.size()) {
            opts.timeoutMs = args[++i].toInt();
        } else if (arg == QStringLiteral("--allowed-tools") && i + 1 < args.size()) {
            opts.allowedTools = args[++i].split(QLatin1Char(','), Qt::SkipEmptyParts);
        } else if (arg == QStringLiteral("--no-tools")) {
            opts.noTools = true;
        } else if (arg == QStringLiteral("--verbose")) {
            opts.verbose = true;
        } else if (arg == QStringLiteral("--stdin")) {
            opts.readStdin = true;
        } else if (arg == QStringLiteral("--help")) {
            printUsage();
            *ok = false;
            return opts;
        } else if (!arg.startsWith(QLatin1Char('-'))) {
            // 位置参数：任务文本（支持多个词拼接）
            if (!opts.task.isEmpty())
                opts.task += QLatin1Char(' ');
            opts.task += arg;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", qPrintable(arg));
            *ok = false;
            return opts;
        }
    }

    // 从 stdin 读取任务
    if (opts.readStdin) {
        QTextStream in(stdin);
        opts.task = in.readAll().trimmed();
    }

    if (opts.task.isEmpty()) {
        std::fputs("Error: No task specified. Use --help for usage.\n", stderr);
        *ok = false;
    }

    return opts;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("TmAgentCli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    bool ok = false;
    const auto opts = parseArgs(app.arguments(), &ok);
    if (!ok)
        return CliRunner::ExitBadArgs;

    CliRunner runner(opts);
    int exitCode = CliRunner::ExitError;

    QObject::connect(&runner, &CliRunner::done, [&](int code) {
        exitCode = code;
        QCoreApplication::exit(code);
    });

    // 延迟启动，确保事件循环已运行
    QTimer::singleShot(0, &runner, &CliRunner::run);

    app.exec();
    return exitCode;
}
