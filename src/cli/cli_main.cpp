#include "CliRunner.h"
#include "CodexInteractiveCli.h"
#include "InteractiveCli.h"
#include "CodexAppServerClient.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/utils/ModelConfigLoader.h"
#include "llm/LLMTypes.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QEventLoop>
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

namespace {

struct ProviderCommandOptions {
    bool addProvider = false;
    QString instanceId;
    QString providerType;
    QString displayName;
    QString baseUrl;
    QString apiKey;
    QString apiKeyEnv;
    QString authType = QStringLiteral("Bearer");
    QString modelId;
    bool setDefault = false;
    bool toolCalling = false;
};

struct ParsedCliOptions {
    CliRunner::Options runner;
    ProviderCommandOptions provider;
    struct CodexCommandOptions {
        bool appServerProbe = false;
        bool interactive = false;
        QString codexBin;
        QString threadId;
        bool viaWsl = false;
    } codex;
    bool ok = true;
    bool interactive = false;
    bool showHelp = false;
    QString errorMessage;
};

QString resolveModelConfigPath(const QString& rawPath)
{
    if (rawPath.trimmed().isEmpty())
        return ChatPersistenceService::defaultModelConfigPath();

    return QDir::isAbsolutePath(rawPath)
        ? rawPath
        : QDir(QCoreApplication::applicationDirPath()).filePath(rawPath);
}

void printJsonToStdout(const QJsonObject& root)
{
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    std::fputs(json.constData(), stdout);
    std::fflush(stdout);
}

int printJsonResult(bool success, const QString& response, int exitCode, const QJsonObject& extra = QJsonObject())
{
    QJsonObject root = extra;
    root.insert(QStringLiteral("success"), success);
    root.insert(QStringLiteral("response"), response);
    printJsonToStdout(root);
    return exitCode;
}

bool ensureSchemaV2Config(const QString& filePath, QString* errorMessage)
{
    if (!QFile::exists(filePath))
        return true;

    if (ModelConfigLoader::detectSchemaVersion(filePath) >= 2)
        return true;

    const QVector<ModelConfig> legacyModels = ModelConfigLoader::loadFromFile(filePath, false);
    QString defaultProvider = ModelConfigLoader::getDefaultConfigId(filePath);
    QString defaultModel;
    if (!defaultProvider.isEmpty()) {
        defaultModel = ModelConfigLoader::getModelConfig(filePath, defaultProvider, false).modelId.trimmed();
    } else if (!legacyModels.isEmpty()) {
        defaultProvider = legacyModels.first().configId.trimmed();
        defaultModel = legacyModels.first().modelId.trimmed();
    }

    const QVector<ProviderInstanceConfig> migrated = ModelConfigLoader::migrateFromV1(legacyModels);
    if (!ModelConfigLoader::saveProviderInstances(filePath, migrated, defaultProvider, defaultModel)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to migrate legacy model config to schema v2");
        }
        return false;
    }

    return true;
}

int handleAddProvider(const ParsedCliOptions& parsed)
{
    const QString configPath = resolveModelConfigPath(parsed.runner.modelConfigPath);
    QString schemaError;
    if (!ensureSchemaV2Config(configPath, &schemaError)) {
        return printJsonResult(false, schemaError, CliRunner::ExitError);
    }

    QVector<ProviderInstanceConfig> instances;
    QString defaultProvider;
    QString defaultModel;
    if (QFile::exists(configPath)) {
        instances = ModelConfigLoader::loadProviderInstances(configPath, false);
        defaultProvider = ModelConfigLoader::getDefaultProvider(configPath);
        defaultModel = ModelConfigLoader::getDefaultModel(configPath);
    }

    ProviderInstanceConfig instance;
    instance.instanceId = parsed.provider.instanceId.trimmed();
    instance.providerType = parsed.provider.providerType.trimmed();
    instance.displayName = parsed.provider.displayName.trimmed().isEmpty()
        ? instance.instanceId
        : parsed.provider.displayName.trimmed();
    instance.baseUrl = parsed.provider.baseUrl.trimmed();
    instance.authType = parsed.provider.authType.trimmed().isEmpty()
        ? QStringLiteral("Bearer")
        : parsed.provider.authType.trimmed();
    instance.toolCalling = parsed.provider.toolCalling;
    if (!parsed.provider.apiKeyEnv.trimmed().isEmpty()) {
        instance.apiKey = QStringLiteral("$ENV{%1}").arg(parsed.provider.apiKeyEnv.trimmed());
    } else {
        instance.apiKey = parsed.provider.apiKey;
    }

    bool updated = false;
    for (int index = 0; index < instances.size(); ++index) {
        if (instances[index].instanceId == instance.instanceId) {
            instances[index] = instance;
            updated = true;
            break;
        }
    }
    if (!updated)
        instances.append(instance);

    const bool becomesDefault = parsed.provider.setDefault || defaultProvider.trimmed().isEmpty();
    if (becomesDefault) {
        defaultProvider = instance.instanceId;
        defaultModel = parsed.provider.modelId.trimmed();
    }

    if (!ModelConfigLoader::saveProviderInstances(configPath, instances, defaultProvider, defaultModel)) {
        return printJsonResult(false, QStringLiteral("Failed to save provider config"), CliRunner::ExitError);
    }

    QString response = updated
        ? QStringLiteral("Provider updated")
        : QStringLiteral("Provider added");
    QString warning;
    if (!becomesDefault && !parsed.provider.modelId.trimmed().isEmpty()) {
        warning = QStringLiteral("model_id is not persisted for non-default providers; pass --model-id when running tasks");
    } else if (becomesDefault && defaultModel.trimmed().isEmpty()) {
        warning = QStringLiteral("default provider saved without default_model; pass --model-id when running tasks");
    }

    QJsonObject providerJson;
    providerJson.insert(QStringLiteral("instance_id"), instance.instanceId);
    providerJson.insert(QStringLiteral("provider_type"), instance.providerType);
    providerJson.insert(QStringLiteral("display_name"), instance.displayName);
    providerJson.insert(QStringLiteral("base_url"), instance.baseUrl);
    providerJson.insert(QStringLiteral("auth_type"), instance.authType);
    providerJson.insert(QStringLiteral("tool_calling"), instance.toolCalling);
    providerJson.insert(QStringLiteral("api_key_source"),
                        !parsed.provider.apiKeyEnv.trimmed().isEmpty()
                            ? QStringLiteral("env")
                            : (instance.apiKey.trimmed().isEmpty()
                                   ? QStringLiteral("empty")
                                   : QStringLiteral("inline")));
    if (!parsed.provider.apiKeyEnv.trimmed().isEmpty())
        providerJson.insert(QStringLiteral("api_key_env"), parsed.provider.apiKeyEnv.trimmed());
    if (!parsed.provider.modelId.trimmed().isEmpty())
        providerJson.insert(QStringLiteral("requested_model_id"), parsed.provider.modelId.trimmed());

    QJsonObject extra;
    extra.insert(QStringLiteral("action"), QStringLiteral("add_provider"));
    extra.insert(QStringLiteral("config_path"), configPath);
    extra.insert(QStringLiteral("provider"), providerJson);
    extra.insert(QStringLiteral("default_provider"), defaultProvider);
    extra.insert(QStringLiteral("default_model"), defaultModel);
    if (!warning.isEmpty())
        extra.insert(QStringLiteral("warning"), warning);

    return printJsonResult(true, response, CliRunner::ExitSuccess, extra);
}

int handleCodexAppServerProbe(const ParsedCliOptions& parsed)
{
    CodexAppServerClient client;
    CodexAppServerClient::LaunchOptions options = CodexAppServerClient::defaultLaunchOptions();
    if (!parsed.codex.codexBin.trimmed().isEmpty())
        options.program = parsed.codex.codexBin.trimmed();
    options.viaWsl = parsed.codex.viaWsl;
    options.workingDirectory = parsed.runner.workspaceDir.trimmed().isEmpty()
        ? QDir::currentPath()
        : QDir::cleanPath(parsed.runner.workspaceDir.trimmed());
    client.setLaunchOptions(options);

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QString initializeRequestId;
    QJsonValue initializeResponse(QJsonValue::Undefined);
    QStringList stderrTail;
    QString failureReason;
    bool success = false;

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        failureReason = QStringLiteral("Codex app-server 握手超时");
        loop.quit();
    });
    QObject::connect(&client, &CodexAppServerClient::started, &loop, [&]() {
        initializeRequestId = client.requestInitialize();
    });
    QObject::connect(&client, &CodexAppServerClient::stderrLineReceived, &loop, [&](const QString& line) {
        stderrTail.append(line);
        while (stderrTail.size() > 40)
            stderrTail.removeFirst();
    });
    QObject::connect(&client, &CodexAppServerClient::transportError, &loop, [&](const QString& message) {
        if (failureReason.isEmpty())
            failureReason = message;
        loop.quit();
    });
    QObject::connect(&client,
                     &CodexAppServerClient::responseErrorReceived,
                     &loop,
                     [&](const QString& requestId, int code, const QString& message, const QJsonObject&) {
                         if (requestId != initializeRequestId)
                             return;
                         failureReason = QStringLiteral("initialize 失败: [%1] %2").arg(code).arg(message);
                         loop.quit();
                     });
    QObject::connect(&client, &CodexAppServerClient::responseReceived, &loop, [&](const QString& requestId, const QJsonValue& result) {
        if (requestId != initializeRequestId)
            return;
        initializeResponse = result;
        client.completeInitializeHandshake();
        success = true;
        loop.quit();
    });

    timeoutTimer.start(parsed.runner.timeoutMs <= 0 ? 15000 : parsed.runner.timeoutMs);
    QTimer::singleShot(0, &client, [&client]() { client.start(); });
    loop.exec();
    timeoutTimer.stop();
    client.shutdown();

    QJsonObject launch;
    launch.insert(QStringLiteral("program"), client.programDisplayName());
    launch.insert(QStringLiteral("working_directory"), client.effectiveServerWorkingDirectory());
    launch.insert(QStringLiteral("via_wsl"), options.viaWsl);

    QJsonObject extra;
    extra.insert(QStringLiteral("action"), QStringLiteral("codex_app_server_probe"));
    extra.insert(QStringLiteral("launch"), launch);
    extra.insert(QStringLiteral("stderr_tail"), QJsonArray::fromStringList(stderrTail));
    if (!initializeRequestId.isEmpty())
        extra.insert(QStringLiteral("initialize_request_id"), initializeRequestId);
    if (initializeResponse.isObject())
        extra.insert(QStringLiteral("initialize_response"), initializeResponse.toObject());
    else if (!initializeResponse.isUndefined())
        extra.insert(QStringLiteral("initialize_response_raw"), initializeResponse);

    if (!success) {
        return printJsonResult(false,
                               failureReason.isEmpty() ? QStringLiteral("Codex app-server 握手失败") : failureReason,
                               CliRunner::ExitError,
                               extra);
    }

    return printJsonResult(true,
                           QStringLiteral("Codex app-server initialize 握手成功"),
                           CliRunner::ExitSuccess,
                           extra);
}

void setParseError(ParsedCliOptions* parsed, const QString& message)
{
    if (!parsed)
        return;
    parsed->ok = false;
    parsed->errorMessage = message;
}

} // namespace

// 自定义日志处理器
static void cliMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Q_UNUSED(context);

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
    const QString defaultModelConfigPath = QDir::toNativeSeparators(ChatPersistenceService::defaultModelConfigPath());
    const QString usage = QStringLiteral(
        "Usage: TmAgentCli [options] <task>\n"
        "\n"
        "Run options:\n"
        "  --model-config <path>   Model config YAML (default: %1)\n"
        "  --config-id <id>        Provider/config ID (default: YAML default)\n"
        "  --model-id <id>         Model ID to use (required for schema v2 unless default_model exists)\n"
        "  --system-prompt <text>  System prompt override\n"
        "  --workspace <dir>       Workspace directory (default: current dir)\n"
        "  --timeout <ms>          Timeout in milliseconds (default: 300000)\n"
        "  --allowed-tools <list>  Comma-separated tool whitelist\n"
        "  --no-tools              Disable all tools\n"
        "  --verbose               Verbose logging to stderr\n"
        "  --stdin                 Read task from stdin\n"
        "  --interactive           Interactive conversation mode\n"
        "\n"
        "Provider management:\n"
        "  --add-provider          Add or update a provider instance in models.yaml and exit\n"
        "  --instance-id <id>      Provider instance ID for --add-provider\n"
        "  --provider-type <type>  Provider type, e.g. openai/deepseek/anthropic/minimax\n"
        "  --display-name <name>   Display name for --add-provider\n"
        "  --base-url <url>        Base URL for --add-provider\n"
        "  --api-key <key>         Inline API key to save\n"
        "  --api-key-env <name>    Save API key as $ENV{<name>}\n"
        "  --auth-type <type>      Bearer / X-API-Key / api-key (default: Bearer)\n"
        "  --set-default           Set the added provider as default_provider\n"
        "  --tool-calling          Mark the added provider as tool-calling capable\n"
        "\n"
        "Codex app-server scaffold:\n"
        "  --codex-app-server-probe  Start `codex app-server` and complete initialize handshake\n"
        "  --codex-interactive       Start a direct Codex chat session over app-server\n"
        "  --codex-bin <path>        Codex executable path (default: TMAGENT_CODEX_BIN or codex)\n"
        "  --codex-thread-id <id>    Resume an existing Codex thread in --codex-interactive mode\n"
        "  --codex-via-wsl           Launch Codex through `wsl.exe -e` (Windows only)\n"
        "\n"
        "Misc:\n"
        "  --help                  Show this help\n"
        "\n"
        "Exit codes: 0=success, 1=error, 2=timeout, 3=bad arguments\n")
                              .arg(defaultModelConfigPath);

    std::fputs(
        usage.toLocal8Bit().constData(),
        stderr);
}

static ParsedCliOptions parseArgs(const QStringList& args)
{
    ParsedCliOptions parsed;

    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args[i];

        if (arg == QStringLiteral("--model-config") && i + 1 < args.size()) {
            parsed.runner.modelConfigPath = args[++i];
        } else if (arg == QStringLiteral("--config-id") && i + 1 < args.size()) {
            parsed.runner.configId = args[++i];
        } else if (arg == QStringLiteral("--model-id") && i + 1 < args.size()) {
            const QString value = args[++i];
            parsed.runner.modelId = value;
            parsed.provider.modelId = value;
        } else if (arg == QStringLiteral("--system-prompt") && i + 1 < args.size()) {
            parsed.runner.systemPrompt = args[++i];
        } else if (arg == QStringLiteral("--workspace") && i + 1 < args.size()) {
            parsed.runner.workspaceDir = args[++i];
        } else if (arg == QStringLiteral("--timeout") && i + 1 < args.size()) {
            parsed.runner.timeoutMs = args[++i].toInt();
        } else if (arg == QStringLiteral("--allowed-tools") && i + 1 < args.size()) {
            parsed.runner.allowedTools = args[++i].split(QLatin1Char(','), Qt::SkipEmptyParts);
        } else if (arg == QStringLiteral("--no-tools")) {
            parsed.runner.noTools = true;
        } else if (arg == QStringLiteral("--verbose")) {
            parsed.runner.verbose = true;
        } else if (arg == QStringLiteral("--stdin")) {
            parsed.runner.readStdin = true;
        } else if (arg == QStringLiteral("--interactive")) {
            parsed.interactive = true;
        } else if (arg == QStringLiteral("--add-provider")) {
            parsed.provider.addProvider = true;
        } else if (arg == QStringLiteral("--instance-id") && i + 1 < args.size()) {
            parsed.provider.instanceId = args[++i];
        } else if (arg == QStringLiteral("--provider-type") && i + 1 < args.size()) {
            parsed.provider.providerType = args[++i];
        } else if (arg == QStringLiteral("--display-name") && i + 1 < args.size()) {
            parsed.provider.displayName = args[++i];
        } else if (arg == QStringLiteral("--base-url") && i + 1 < args.size()) {
            parsed.provider.baseUrl = args[++i];
        } else if (arg == QStringLiteral("--api-key") && i + 1 < args.size()) {
            parsed.provider.apiKey = args[++i];
        } else if (arg == QStringLiteral("--api-key-env") && i + 1 < args.size()) {
            parsed.provider.apiKeyEnv = args[++i];
        } else if (arg == QStringLiteral("--auth-type") && i + 1 < args.size()) {
            parsed.provider.authType = args[++i];
        } else if (arg == QStringLiteral("--set-default")) {
            parsed.provider.setDefault = true;
        } else if (arg == QStringLiteral("--tool-calling")) {
            parsed.provider.toolCalling = true;
        } else if (arg == QStringLiteral("--codex-app-server-probe")) {
            parsed.codex.appServerProbe = true;
        } else if (arg == QStringLiteral("--codex-interactive")) {
            parsed.codex.interactive = true;
        } else if (arg == QStringLiteral("--codex-bin") && i + 1 < args.size()) {
            parsed.codex.codexBin = args[++i];
        } else if (arg == QStringLiteral("--codex-thread-id") && i + 1 < args.size()) {
            parsed.codex.threadId = args[++i];
        } else if (arg == QStringLiteral("--codex-via-wsl")) {
            parsed.codex.viaWsl = true;
        } else if (arg == QStringLiteral("--help")) {
            parsed.showHelp = true;
            return parsed;
        } else if (!arg.startsWith(QLatin1Char('-'))) {
            if (!parsed.runner.task.isEmpty())
                parsed.runner.task += QLatin1Char(' ');
            parsed.runner.task += arg;
        } else {
            setParseError(&parsed, QStringLiteral("Unknown option: %1").arg(arg));
            return parsed;
        }
    }

    if (parsed.runner.readStdin) {
        QTextStream in(stdin);
        parsed.runner.task = in.readAll().trimmed();
    }

    if (parsed.provider.addProvider) {
        if (parsed.codex.appServerProbe || parsed.codex.interactive) {
            setParseError(&parsed, QStringLiteral("--add-provider cannot be combined with Codex app-server modes"));
            return parsed;
        }
        if (parsed.interactive) {
            setParseError(&parsed, QStringLiteral("--add-provider cannot be combined with --interactive"));
            return parsed;
        }
        if (!parsed.runner.task.trimmed().isEmpty()) {
            setParseError(&parsed, QStringLiteral("--add-provider does not accept a task payload"));
            return parsed;
        }
        if (parsed.provider.instanceId.trimmed().isEmpty()) {
            setParseError(&parsed, QStringLiteral("--instance-id is required with --add-provider"));
            return parsed;
        }
        if (parsed.provider.providerType.trimmed().isEmpty()) {
            setParseError(&parsed, QStringLiteral("--provider-type is required with --add-provider"));
            return parsed;
        }
        if (parsed.provider.baseUrl.trimmed().isEmpty()) {
            setParseError(&parsed, QStringLiteral("--base-url is required with --add-provider"));
            return parsed;
        }
        if (!parsed.provider.apiKey.trimmed().isEmpty() && !parsed.provider.apiKeyEnv.trimmed().isEmpty()) {
            setParseError(&parsed, QStringLiteral("--api-key and --api-key-env cannot be used together"));
            return parsed;
        }
        return parsed;
    }

    if (parsed.codex.appServerProbe || parsed.codex.interactive) {
        if (parsed.interactive) {
            setParseError(&parsed, QStringLiteral("Codex app-server modes cannot be combined with --interactive"));
            return parsed;
        }
        if (parsed.codex.appServerProbe && !parsed.runner.task.trimmed().isEmpty()) {
            setParseError(&parsed, QStringLiteral("--codex-app-server-probe does not accept a task payload"));
            return parsed;
        }
        if (parsed.codex.interactive && !parsed.runner.task.trimmed().isEmpty()) {
            setParseError(&parsed, QStringLiteral("--codex-interactive does not accept a task payload"));
            return parsed;
        }
        return parsed;
    }

    if (!parsed.interactive && parsed.runner.task.isEmpty()) {
        setParseError(&parsed, QStringLiteral("No task specified. Use --interactive for conversation mode or --help for usage."));
    }

    return parsed;
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

    const ParsedCliOptions parsed = parseArgs(app.arguments());

    // 解析完参数后，更新 verbose 标志，此后 verbose 模式的日志才会显示
    g_verbose = parsed.runner.verbose;

    if (parsed.showHelp) {
        printUsage();
        return CliRunner::ExitSuccess;
    }

    if (!parsed.ok) {
        std::fprintf(stderr, "Error: %s\n", qPrintable(parsed.errorMessage));
        return CliRunner::ExitBadArgs;
    }

    if (parsed.provider.addProvider)
        return handleAddProvider(parsed);
    if (parsed.codex.appServerProbe)
        return handleCodexAppServerProbe(parsed);
    if (parsed.codex.interactive) {
        int exitCode = CliRunner::ExitError;

        CodexInteractiveCli::Options options;
        options.codexBin = parsed.codex.codexBin;
        options.workspaceDir = parsed.runner.workspaceDir.isEmpty() ? QDir::currentPath() : parsed.runner.workspaceDir;
        options.resumeThreadId = parsed.codex.threadId;
        options.viaWsl = parsed.codex.viaWsl;
        options.verbose = parsed.runner.verbose;

        CodexInteractiveCli cli(options);
        QObject::connect(&cli, &CodexInteractiveCli::done, [&](int code) {
            exitCode = code;
            QCoreApplication::exit(code);
        });
        QTimer::singleShot(0, &cli, &CodexInteractiveCli::run);
        app.exec();
        return exitCode;
    }

    int exitCode = CliRunner::ExitError;

    if (parsed.interactive) {
        InteractiveCli::Options iOpts;
        iOpts.modelConfigPath = parsed.runner.modelConfigPath;
        iOpts.verbose = parsed.runner.verbose;

        InteractiveCli cli(iOpts);
        QObject::connect(&cli, &InteractiveCli::done, [&](int code) {
            exitCode = code;
            QCoreApplication::exit(code);
        });
        QTimer::singleShot(0, &cli, &InteractiveCli::run);
        app.exec();
    } else {
        CliRunner runner(parsed.runner);
        QObject::connect(&runner, &CliRunner::done, [&](int code) {
            exitCode = code;
            QCoreApplication::exit(code);
        });
        QTimer::singleShot(0, &runner, &CliRunner::run);
        app.exec();
    }

    return exitCode;
}
