#include "CliRunner.h"
#include "InteractiveCli.h"

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QTimer>
#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// 全局控制台锁，防止多线程输出交错
QMutex g_consoleMutex;

// 是否开启 verbose 日志的全局标志
static bool g_verbose = false;

// 自定义日志处理器
static void cliMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // 如果不是 verbose 模式，忽略 Debug 和 Info 级别的日志
    if (!g_verbose && (type == QtDebugMsg || type == QtInfoMsg)) {
        return;
    }

    QByteArray localMsg = msg.toLocal8Bit();

    QMutexLocker locker(&g_consoleMutex);
    switch (type) {
    case QtDebugMsg:
        std::fprintf(stderr, "%s\n", localMsg.constData());
        break;
    case QtInfoMsg:
        std::fprintf(stderr, "%s\n", localMsg.constData());
        break;
    case QtWarningMsg:
        std::fprintf(stderr, "Warning: %s\n", localMsg.constData());
        break;
    case QtCriticalMsg:
        std::fprintf(stderr, "Critical: %s\n", localMsg.constData());
        break;
    case QtFatalMsg:
        std::fprintf(stderr, "Fatal: %s\n", localMsg.constData());
        abort();
    }
    std::fflush(stderr);
}

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
        "  --interactive           Interactive conversation mode\n"
        "  --help                  Show this help\n"
        "\n"
        "Exit codes: 0=success, 1=error, 2=timeout, 3=bad arguments\n",
        stderr);
}

static CliRunner::Options parseArgs(const QStringList& args, bool* ok, bool* interactive)
{
    CliRunner::Options opts;
    *ok = true;
    *interactive = false;

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
        } else if (arg == QStringLiteral("--interactive")) {
            *interactive = true;
        } else if (arg == QStringLiteral("--help")) {
            printUsage();
            *ok = false;
            return opts;
        } else if (!arg.startsWith(QLatin1Char('-'))) {
            if (!opts.task.isEmpty())
                opts.task += QLatin1Char(' ');
            opts.task += arg;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", qPrintable(arg));
            *ok = false;
            return opts;
        }
    }

    if (opts.readStdin) {
        QTextStream in(stdin);
        opts.task = in.readAll().trimmed();
    }

    // interactive 模式不需要 task
    if (!*interactive && opts.task.isEmpty()) {
        std::fputs("Error: No task specified. Use --interactive for conversation mode or --help for usage.\n", stderr);
        *ok = false;
    }

    return opts;
}

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    // 强制控制台使用 UTF-8 输入输出，以防中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("TmAgentCli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    // 尽早安装拦截器，避免服务初始化阶段的 qDebug 漏出
    // 注意：此时 g_verbose 默认 false，因此所有 Debug/Info 级别日志会被静默过滤
    qInstallMessageHandler(cliMessageHandler);

    bool ok = false;
    bool interactive = false;
    const auto opts = parseArgs(app.arguments(), &ok, &interactive);

    // 解析完参数后，更新 verbose 标志，此后 verbose 模式的日志才会显示
    g_verbose = opts.verbose;

    if (!ok)
        return CliRunner::ExitBadArgs;

    int exitCode = CliRunner::ExitError;

    if (interactive) {
        InteractiveCli::Options iOpts;
        iOpts.modelConfigPath = opts.modelConfigPath;
        iOpts.verbose = opts.verbose;

        InteractiveCli cli(iOpts);
        QObject::connect(&cli, &InteractiveCli::done, [&](int code) {
            exitCode = code;
            QCoreApplication::exit(code);
        });
        QTimer::singleShot(0, &cli, &InteractiveCli::run);
        app.exec();
    } else {
        CliRunner runner(opts);
        QObject::connect(&runner, &CliRunner::done, [&](int code) {
            exitCode = code;
            QCoreApplication::exit(code);
        });
        QTimer::singleShot(0, &runner, &CliRunner::run);
        app.exec();
    }

    return exitCode;
}
