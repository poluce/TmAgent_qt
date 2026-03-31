#include "CliRunner.h"
#include "core/agent/LLMAgent.h"
#include "core/agent/ToolDispatcher.h"
#include "core/agent/ToolPluginManager.h"
#include "core/memory/MemoryManager.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/tools/MemoryTool.h"
#include "core/utils/ModelConfigLoader.h"
#include "llm/ModelFactory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <cstdio>

namespace {

QString resolveCliModelConfigPath(const QString& rawPath)
{
    if (rawPath.trimmed().isEmpty())
        return ChatPersistenceService::defaultModelConfigPath();

    return QDir::isAbsolutePath(rawPath)
        ? rawPath
        : QDir(QCoreApplication::applicationDirPath()).filePath(rawPath);
}

} // namespace

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
    const QString configPath = resolveCliModelConfigPath(m_opts.modelConfigPath);

    log(QStringLiteral("Loading model config: %1").arg(configPath));

    const int schemaVersion = ModelConfigLoader::detectSchemaVersion(configPath);
    if (schemaVersion >= 2) {
        const auto instances = ModelConfigLoader::loadProviderInstances(configPath, /*resolveEnv=*/true);
        if (instances.isEmpty()) {
            log(QStringLiteral("ERROR: No provider instances loaded from %1").arg(configPath));
            return false;
        }

        m_factory = ModelFactory::instance();
        for (const auto& inst : instances) {
            m_factory->registerProviderInstance(inst);
            log(QStringLiteral("  Registered provider: %1 (%2)")
                    .arg(inst.instanceId, inst.displayName));
        }

        const QString defaultProvider = ModelConfigLoader::getDefaultProvider(configPath);
        const QString defaultModel = ModelConfigLoader::getDefaultModel(configPath);

        if (m_opts.configId.isEmpty()) {
            m_opts.configId = !defaultProvider.isEmpty()
                ? defaultProvider
                : instances.first().instanceId;
        }

        if (m_opts.modelId.isEmpty()
            && !defaultModel.isEmpty()
            && m_opts.configId == defaultProvider) {
            m_opts.modelId = defaultModel;
        }

        if (!m_factory->hasProviderInstance(m_opts.configId)) {
            log(QStringLiteral("ERROR: provider instance '%1' not found").arg(m_opts.configId));
            return false;
        }

        if (m_opts.modelId.isEmpty()) {
            log(QStringLiteral("ERROR: no model selected for provider '%1'; use --model-id or set default_model")
                    .arg(m_opts.configId));
            return false;
        }

        log(QStringLiteral("Using provider: %1").arg(m_opts.configId));
        log(QStringLiteral("Using modelId: %1").arg(m_opts.modelId));
        return true;
    }

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

    if (m_opts.modelId.isEmpty()) {
        const ModelConfig config = m_factory->getModelConfig(m_opts.configId);
        if (!config.modelId.trimmed().isEmpty())
            m_opts.modelId = config.modelId.trimmed();
    }

    log(QStringLiteral("Using configId: %1").arg(m_opts.configId));
    if (!m_opts.modelId.isEmpty())
        log(QStringLiteral("Using modelId: %1").arg(m_opts.modelId));
    return true;
}

bool CliRunner::initToolDispatcher()
{
    m_dispatcher = ToolDispatcher::instance();
    m_dispatcher->clearProviders();
    if (!m_memoryPersistence)
        m_memoryPersistence = std::make_unique<ChatPersistenceService>();
    if (!m_memoryManager)
        m_memoryManager = std::make_unique<MemoryManager>(m_memoryPersistence.get());
    MemoryTool::setWriteHandler([this](const QJsonObject& args) {
        return executeMemoryWriteTool(args);
    });
    if (!m_opts.noTools) {
        LLMConfig defaultConfig;
        defaultConfig.configId = m_opts.configId;
        defaultConfig.providerInstanceId = m_opts.configId;
        defaultConfig.selectedModelId = m_opts.modelId;
        defaultConfig.workspaceDir = m_opts.workspaceDir.isEmpty()
            ? QDir::currentPath()
            : m_opts.workspaceDir;
        m_dispatcher->setDefaultAgentConfig(defaultConfig);

        m_toolPluginManager = std::make_unique<ToolPluginManager>(m_dispatcher, nullptr);
        m_toolPluginManager->setConfigObject(m_memoryPersistence->loadToolPluginConfigObject());
        m_toolPluginManager->initialize();
        m_memoryPersistence->saveToolPluginConfigObject(m_toolPluginManager->configObject());

        const QList<ToolPluginManager::ProviderBinding> bindings =
            m_toolPluginManager->activeProviders();
        for (const ToolPluginManager::ProviderBinding& binding : bindings)
            m_dispatcher->registerProvider(binding.provider, binding.providerName);

        const auto schemas = m_dispatcher->getAllToolSchemas();
        log(QStringLiteral("Registered %1 tools").arg(schemas.size()));
    } else {
        log(QStringLiteral("Tools disabled (--no-tools)"));
    }
    return true;
}

ToolResult CliRunner::executeMemoryWriteTool(const QJsonObject& args) const
{
    if (!m_memoryManager) {
        return ToolResult(
            QStringLiteral("错误: memory manager unavailable"),
            QStringLiteral("记忆写入失败"),
            false);
    }

    const QString agentId = args.value(QStringLiteral("_agent_id")).toString().trimmed();
    const QString memoryText = args.value(QStringLiteral("memory")).toString().trimmed();
    const QString reason = args.value(QStringLiteral("reason")).toString().trimmed();
    const QString toolCallId = args.value(QStringLiteral("_tool_call_id")).toString().trimmed();
    if (agentId.isEmpty()) {
        return ToolResult(
            QStringLiteral("错误: 缺少 _agent_id"),
            QStringLiteral("记忆写入失败：缺少助手上下文"),
            false);
    }

    QString summary;
    QString writtenPath;
    QJsonObject metadata;
    QString error;
    const bool ok = m_memoryManager->rememberToolRequested(
        agentId,
        QString(),
        QString(),
        toolCallId,
        memoryText,
        reason,
        &summary,
        &writtenPath,
        &metadata,
        &error);
    if (!ok) {
        return ToolResult(
            error.isEmpty() ? QStringLiteral("错误: memory_write 执行失败") : error,
            QStringLiteral("记忆写入失败"),
            false);
    }

    QJsonObject resultData = metadata;
    resultData.insert(QStringLiteral("agent_id"), agentId);
    resultData.insert(QStringLiteral("path"), writtenPath);
    const bool duplicateOnly = metadata.value(QStringLiteral("longMemoryAdded")).toInt() == 0
        && metadata.value(QStringLiteral("longMemoryDuplicate")).toInt() > 0;
    const QString raw = duplicateOnly
        ? QStringLiteral("memory_write: 已存在相同长期记忆，无需重复写入\nagent_id: %1\npath: %2\nmemory: %3")
              .arg(agentId, writtenPath, summary)
        : QStringLiteral("memory_write: 已写入长期记忆\nagent_id: %1\npath: %2\nmemory: %3")
              .arg(agentId, writtenPath, summary);
    const QString userSummary = duplicateOnly
        ? QStringLiteral("记忆已存在，无需重复写入")
        : QStringLiteral("已写入长期记忆");
    return ToolResult(raw, userSummary, true, resultData);
}

bool CliRunner::initAgent()
{
    m_agent = new LLMAgent(this);
    m_agent->setModelFactory(m_factory);

    // 配置
    LLMConfig config;
    config.configId = m_opts.configId;
    config.providerInstanceId = m_opts.configId;
    config.selectedModelId = m_opts.modelId;
    config.workspaceDir = m_opts.workspaceDir.isEmpty()
        ? QDir::currentPath()
        : m_opts.workspaceDir;

    if (!m_opts.systemPrompt.isEmpty()) {
        config.systemPrompt = m_opts.systemPrompt;
    }

    if (m_dispatcher)
        m_dispatcher->setDefaultAgentConfig(config);

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
