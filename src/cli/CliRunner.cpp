#include "CliRunner.h"
#include "core/agent/LLMAgent.h"
#include "core/agent/ToolDispatcher.h"
#include "core/utils/ModelConfigLoader.h"
#include "core/utils/ToolSchemaLoader.h"
#include "llm/ModelFactory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>

CliRunner::CliRunner(const Options& opts, QObject* parent)
    : QObject(parent)
    , m_opts(opts)
{
}

void CliRunner::run()
{
    m_startTimeMs = QDateTime::currentMSecsSinceEpoch();

    if (!initModelFactory()) {
        outputJson(false, QStringLiteral("Failed to initialize model factory"), ExitError);
        return;
    }
    if (!initToolDispatcher()) {
        outputJson(false, QStringLiteral("Failed to initialize tool dispatcher"), ExitError);
        return;
    }
    if (!initAgent()) {
        outputJson(false, QStringLiteral("Failed to initialize agent"), ExitError);
        return;
    }

    // 超时定时器
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &CliRunner::onTimeout);
    m_timer->start(m_opts.timeoutMs);

    log(QStringLiteral("Sending task: %1").arg(m_opts.task));
    m_agent->sendMessage(m_opts.task);
}

// ─── 初始化 ───

bool CliRunner::initModelFactory()
{
    const QString configPath = QDir::isAbsolutePath(m_opts.modelConfigPath)
        ? m_opts.modelConfigPath
        : QDir(QCoreApplication::applicationDirPath()).filePath(m_opts.modelConfigPath);

    log(QStringLiteral("Loading model config: %1").arg(configPath));

    const auto models = ModelConfigLoader::loadFromFile(configPath, /*resolveEnv=*/true);
    if (models.isEmpty()) {
        log(QStringLiteral("ERROR: No models loaded from %1").arg(configPath));
        return false;
    }

    m_factory = ModelFactory::instance();
    for (const auto& mc : models) {
        m_factory->registerModelConfig(mc);
        log(QStringLiteral("  Registered model: %1 (%2)").arg(mc.configId, mc.displayName));
    }

    // 确定 configId
    if (m_opts.configId.isEmpty()) {
        m_opts.configId = ModelConfigLoader::getDefaultConfigId(configPath);
        if (m_opts.configId.isEmpty() && !models.isEmpty()) {
            m_opts.configId = models.first().configId;
        }
    }

    if (!m_factory->hasModelConfig(m_opts.configId)) {
        log(QStringLiteral("ERROR: configId '%1' not found").arg(m_opts.configId));
        return false;
    }

    log(QStringLiteral("Using configId: %1").arg(m_opts.configId));
    return true;
}

bool CliRunner::initToolDispatcher()
{
    m_dispatcher = ToolDispatcher::instance();
    if (!m_opts.noTools) {
        m_dispatcher->registerDefaultTools();
        const auto schemas = m_dispatcher->getAllToolSchemas();
        log(QStringLiteral("Registered %1 tools").arg(schemas.size()));
    } else {
        log(QStringLiteral("Tools disabled (--no-tools)"));
    }
    return true;
}

bool CliRunner::initAgent()
{
    m_agent = new LLMAgent(this);
    m_agent->setModelFactory(m_factory);

    // 配置
    LLMConfig config;
    config.configId = m_opts.configId;
    config.workspaceDir = m_opts.workspaceDir.isEmpty()
        ? QDir::currentPath()
        : m_opts.workspaceDir;

    if (!m_opts.systemPrompt.isEmpty()) {
        config.systemPrompt = m_opts.systemPrompt;
    }

    m_agent->setConfig(config);

    if (!m_opts.systemPrompt.isEmpty()) {
        m_agent->setSystemPrompt(m_opts.systemPrompt);
    }

    // 工具
    if (!m_opts.noTools) {
        m_agent->setToolDispatcher(m_dispatcher, m_opts.allowedTools);
    }

    // 连接信号
    connect(m_agent, &LLMAgent::finished, this, &CliRunner::onFinished);
    connect(m_agent, &LLMAgent::errorOccurred, this, &CliRunner::onError);
    connect(m_agent, &LLMAgent::toolEvent, this, &CliRunner::onToolEvent);

    log(QStringLiteral("Agent initialized, workspace: %1").arg(config.workspaceDir));
    return true;
}

// ─── 信号处理 ───

void CliRunner::onFinished(const QString& fullContent)
{
    if (m_timer)
        m_timer->stop();
    outputJson(true, fullContent, ExitSuccess);
}

void CliRunner::onError(const QString& errorMsg)
{
    if (m_timer)
        m_timer->stop();
    outputJson(false, errorMsg, ExitError);
}

void CliRunner::onToolEvent(const ToolExecutionEvent& event)
{
    QJsonObject entry;
    entry[QStringLiteral("tool")] = event.toolName;
    entry[QStringLiteral("status")] = event.status;
    entry[QStringLiteral("timestamp_ms")] =
        QDateTime::currentMSecsSinceEpoch() - m_startTimeMs;

    if (event.status == QStringLiteral("started")) {
        entry[QStringLiteral("input")] = event.data;
    } else if (event.status == QStringLiteral("completed")) {
        entry[QStringLiteral("success")] = event.success;
    }

    m_toolCalls.append(entry);
    log(QStringLiteral("[tool] %1 %2").arg(event.toolName, event.status));
}

void CliRunner::onTimeout()
{
    log(QStringLiteral("Timeout after %1 ms").arg(m_opts.timeoutMs));
    if (m_agent)
        m_agent->abort();
    outputJson(false, QStringLiteral("Timeout after %1 ms").arg(m_opts.timeoutMs), ExitTimeout);
}

// ─── 输出 ───

void CliRunner::outputJson(bool success, const QString& response, int exitCode)
{
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_startTimeMs;

    QJsonObject root;
    root[QStringLiteral("success")] = success;
    root[QStringLiteral("response")] = response;
    root[QStringLiteral("tool_calls")] = m_toolCalls;
    root[QStringLiteral("elapsed_ms")] = elapsed;

    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    std::fputs(json.constData(), stdout);
    std::fflush(stdout);

    emit done(exitCode);
}

void CliRunner::log(const QString& msg) const
{
    if (m_opts.verbose) {
        std::fprintf(stderr, "[cli] %s\n", qPrintable(msg));
    }
}
