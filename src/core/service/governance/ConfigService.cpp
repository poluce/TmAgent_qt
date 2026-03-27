#include "ConfigService.h"
#include "RuntimeManager.h"
#include "core/agent/McpToolProvider.h"
#include "core/agent/ToolDispatcher.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "core/utils/DefaultPrompts.h"
#include "core/utils/ModelConfigLoader.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QProcessEnvironment>

namespace {

bool containsCjk(const QString& text)
{
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF))
            return true;
    }
    return false;
}

int latinMojibakeCharCount(const QString& text)
{
    int count = 0;
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if ((u >= 0x00C0 && u <= 0x00FF) || (u >= 0x00A1 && u <= 0x00BF))
            ++count;
    }
    return count;
}

QString decodePossiblyMojibakeUtf8(const QByteArray& bytes)
{
    const QString utf8Text = QString::fromUtf8(bytes);
    if (utf8Text.isEmpty())
        return utf8Text;
    if (containsCjk(utf8Text))
        return utf8Text;
    if (latinMojibakeCharCount(utf8Text) < 8)
        return utf8Text;

    const QString repaired = QString::fromUtf8(utf8Text.toLatin1());
    if (containsCjk(repaired))
        return repaired;
    return utf8Text;
}

} // namespace

ConfigService::ConfigService(QObject* parent)
    : QObject(parent)
{
}

ConfigService::~ConfigService() = default;

void ConfigService::setPersistence(ChatPersistenceService* persistence) { m_persistence = persistence; }
void ConfigService::setModelFactory(ModelFactory* factory) { m_modelFactory = factory; }
void ConfigService::setMcpProvider(McpToolProvider* provider) { m_mcpProvider = provider; }
void ConfigService::setToolDispatcher(ToolDispatcher* dispatcher) { m_toolDispatcher = dispatcher; }
void ConfigService::setRuntimeManager(RuntimeManager* runtimeManager) { m_runtimeManager = runtimeManager; }

void ConfigService::applyMcpConfig(const QStringList& specs)
{
    if (!m_mcpProvider)
        return;

    m_mcpProvider->clearServers();
    for (const QString& spec : specs) {
        if (!m_mcpProvider->addServerFromSpec(spec)) {
            qWarning() << "MCP server spec 无效:" << spec;
        }
    }

    const QString envSpec = QProcessEnvironment::systemEnvironment().value("TMAGENT_MCP_SERVERS");
    if (!envSpec.trimmed().isEmpty()) {
        const QStringList servers = envSpec.split(';', Qt::SkipEmptyParts);
        for (const QString& serverSpec : servers) {
            if (!m_mcpProvider->addServerFromSpec(serverSpec)) {
                qWarning() << "MCP server spec 无效(ENV):" << serverSpec;
            }
        }
    }

    if (m_toolDispatcher) {
        m_toolDispatcher->refreshProvider(QStringLiteral("mcp"));
    }
}

QStringList ConfigService::loadMcpConfigSpecs() const
{
    return m_persistence ? m_persistence->loadMcpConfigSpecs() : QStringList();
}

bool ConfigService::saveMcpConfigSpecs(const QStringList& specs) const
{
    return m_persistence && m_persistence->saveMcpConfigSpecs(specs);
}

QString ConfigService::mcpConfigPath() const
{
    return m_persistence ? m_persistence->mcpConfigPath() : QString();
}

QString ConfigService::toolPluginConfigPath() const
{
    return m_persistence
        ? m_persistence->toolPluginConfigPath()
        : QDir(configDirPath()).filePath(QStringLiteral("tool_plugins.json"));
}

QJsonObject ConfigService::defaultToolPluginConfigObject() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), 1);
    obj.insert(QStringLiteral("search_dirs"), QJsonArray());
    obj.insert(QStringLiteral("plugins"), QJsonObject());
    return obj;
}

QJsonObject ConfigService::normalizeToolPluginConfigObject(const QJsonObject& raw) const
{
    QJsonObject out = defaultToolPluginConfigObject();
    out.insert(QStringLiteral("schema_version"), 1);

    QJsonArray dirs;
    const QJsonArray rawDirs = raw.value(QStringLiteral("search_dirs")).toArray();
    QSet<QString> seen;
    for (const QJsonValue& value : rawDirs) {
        const QString dir = value.toString().trimmed();
        if (dir.isEmpty() || seen.contains(dir))
            continue;
        seen.insert(dir);
        dirs.append(dir);
    }
    out.insert(QStringLiteral("search_dirs"), dirs);

    QJsonObject pluginsOut;
    const QJsonObject pluginsIn = raw.value(QStringLiteral("plugins")).toObject();
    for (auto it = pluginsIn.constBegin(); it != pluginsIn.constEnd(); ++it) {
        QJsonObject entry;
        const QJsonObject rawEntry = it.value().toObject();
        entry.insert(QStringLiteral("enabled"), rawEntry.value(QStringLiteral("enabled")).toBool(true));
        entry.insert(QStringLiteral("config"),
                     rawEntry.value(QStringLiteral("config")).isObject()
                         ? rawEntry.value(QStringLiteral("config")).toObject()
                         : QJsonObject());
        entry.insert(QStringLiteral("last_health"),
                     rawEntry.value(QStringLiteral("last_health")).isObject()
                         ? rawEntry.value(QStringLiteral("last_health")).toObject()
                         : QJsonObject());
        pluginsOut.insert(it.key(), entry);
    }
    out.insert(QStringLiteral("plugins"), pluginsOut);
    return out;
}

QJsonObject ConfigService::loadToolPluginConfigObject() const
{
    bool ok = false;
    const QJsonObject raw = m_persistence
        ? m_persistence->loadToolPluginConfigObject()
        : readJsonObject(toolPluginConfigPath(), &ok);
    return normalizeToolPluginConfigObject(raw);
}

bool ConfigService::saveToolPluginConfigObject(const QJsonObject& raw, QString* errOut) const
{
    if (errOut)
        errOut->clear();
    const QJsonObject normalized = normalizeToolPluginConfigObject(raw);
    const bool ok = m_persistence
        ? m_persistence->saveToolPluginConfigObject(normalized)
        : writeJsonObject(toolPluginConfigPath(), normalized);
    if (!ok && errOut)
        *errOut = tr("写入工具插件配置文件失败");
    return ok;
}

QString ConfigService::modelConfigPath() const
{
    return m_persistence ? m_persistence->modelConfigPath() : QString();
}

QString ConfigService::dataRootPath() const
{
    return m_persistence ? m_persistence->dataRootPath() : ChatPersistenceService::defaultDataRootPath();
}

QString ConfigService::configDirPath() const
{
    return m_persistence ? m_persistence->configDirPath() : ChatPersistenceService::defaultConfigDirPath();
}

QJsonObject ConfigService::readJsonObject(const QString& filePath, bool* ok) const
{
    if (m_persistence)
        return m_persistence->readJsonObject(filePath, ok);

    if (ok)
        *ok = false;
    QFile file(filePath);
    if (!file.exists()) {
        if (ok)
            *ok = true;
        return QJsonObject();
    }
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QJsonObject();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    if (ok)
        *ok = true;
    return doc.object();
}

bool ConfigService::writeJsonObject(const QString& filePath, const QJsonObject& obj) const
{
    if (m_persistence)
        return m_persistence->writeJsonObject(filePath, obj);

    if (!QDir().mkpath(QFileInfo(filePath).absolutePath()))
        return false;
    QFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    const bool ok = (file.write(bytes) == bytes.size());
    file.close();
    return ok;
}

QString ConfigService::memoryPolicyPath() const
{
    return m_persistence ? m_persistence->memoryPolicyPath()
                         : QDir(configDirPath()).filePath(QStringLiteral("memory_policy.json"));
}

QJsonObject ConfigService::loadMemoryPolicyObject(bool* ok) const
{
    return readJsonObject(memoryPolicyPath(), ok);
}

bool ConfigService::saveMemoryPolicyObject(const QJsonObject& obj) const
{
    return writeJsonObject(memoryPolicyPath(), obj);
}

QString ConfigService::toolLoopPolicyPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("tool_loop_policy.json"));
}

QJsonObject ConfigService::defaultToolLoopPolicyObject() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), 5);
    obj.insert(QStringLiteral("max_consecutive_no_progress_rounds"), 3);
    obj.insert(QStringLiteral("max_consecutive_failed_tool_rounds"), 3);
    obj.insert(QStringLiteral("max_total_tool_calls_per_turn"), 64);
    obj.insert(QStringLiteral("max_web_fetch_calls_per_turn"), 16);
    obj.insert(QStringLiteral("max_tool_loop_time_ms"), 300000);
    return obj;
}

QJsonObject ConfigService::normalizeToolLoopPolicyObject(const QJsonObject& raw) const
{
    QJsonObject out = defaultToolLoopPolicyObject();
    out.insert(QStringLiteral("schema_version"), 5);
    out.insert(QStringLiteral("max_consecutive_no_progress_rounds"),
               qBound(1,
                      raw.value(QStringLiteral("max_consecutive_no_progress_rounds"))
                          .toInt(out.value(QStringLiteral("max_consecutive_no_progress_rounds")).toInt()),
                      32));
    out.insert(QStringLiteral("max_consecutive_failed_tool_rounds"),
               qBound(1,
                      raw.value(QStringLiteral("max_consecutive_failed_tool_rounds"))
                          .toInt(out.value(QStringLiteral("max_consecutive_failed_tool_rounds")).toInt()),
                      32));
    out.insert(QStringLiteral("max_total_tool_calls_per_turn"),
               qBound(4,
                      raw.value(QStringLiteral("max_total_tool_calls_per_turn"))
                          .toInt(out.value(QStringLiteral("max_total_tool_calls_per_turn")).toInt()),
                      256));
    out.insert(QStringLiteral("max_web_fetch_calls_per_turn"),
               qBound(1,
                      raw.value(QStringLiteral("max_web_fetch_calls_per_turn"))
                          .toInt(out.value(QStringLiteral("max_web_fetch_calls_per_turn")).toInt()),
                      128));
    out.insert(QStringLiteral("max_tool_loop_time_ms"),
               qBound<qint64>(5000,
                              raw.value(QStringLiteral("max_tool_loop_time_ms")).toVariant().toLongLong(),
                              300000));
    return out;
}

QJsonObject ConfigService::loadToolLoopPolicyObject() const
{
    bool ok = false;
    const QJsonObject raw = readJsonObject(toolLoopPolicyPath(), &ok);
    if (!ok)
        return defaultToolLoopPolicyObject();
    return normalizeToolLoopPolicyObject(raw);
}

bool ConfigService::saveToolLoopPolicyObject(const QJsonObject& raw, QString* errOut) const
{
    if (errOut)
        errOut->clear();
    if (!writeJsonObject(toolLoopPolicyPath(), normalizeToolLoopPolicyObject(raw))) {
        if (errOut)
            *errOut = tr("写入工具循环策略文件失败");
        return false;
    }
    return true;
}

QString ConfigService::defaultAgentConfigPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("default_agent_config.json"));
}

LLMConfig ConfigService::loadDefaultAgentConfig(const LLMConfig& fallback) const
{
    LLMConfig cfg = fallback;
    cfg.executionMode = DefaultPrompts::normalizeExecutionMode(cfg.executionMode);

    bool ok = false;
    const QJsonObject raw = readJsonObject(defaultAgentConfigPath(), &ok);
    if (!ok) {
        if (cfg.systemPrompt.trimmed().isEmpty())
            cfg.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt(cfg.executionMode);
        if (cfg.configId.trimmed().isEmpty() && !cfg.providerInstanceId.trimmed().isEmpty())
            cfg.configId = cfg.providerInstanceId.trimmed();
        return cfg;
    }

    const QString providerInstanceId = raw.value(QStringLiteral("providerInstanceId")).toString().trimmed();
    const QString selectedModelId = raw.value(QStringLiteral("selectedModelId")).toString().trimmed();
    const QString configId = raw.value(QStringLiteral("configId")).toString().trimmed();
    const QString userName = raw.value(QStringLiteral("userName")).toString().trimmed();
    const QString systemPrompt = raw.value(QStringLiteral("systemPrompt")).toString().trimmed();
    const QString executionMode = raw.value(QStringLiteral("executionMode")).toString().trimmed();
    const int recursionDepth = raw.value(QStringLiteral("recursionDepth")).toInt(cfg.recursionDepth);

    if (!providerInstanceId.isEmpty())
        cfg.providerInstanceId = providerInstanceId;
    if (!selectedModelId.isEmpty())
        cfg.selectedModelId = selectedModelId;
    if (!configId.isEmpty())
        cfg.configId = configId;
    if (!userName.isEmpty())
        cfg.userName = userName;
    if (!systemPrompt.isEmpty())
        cfg.systemPrompt = systemPrompt;
    cfg.executionMode = DefaultPrompts::normalizeExecutionMode(executionMode.isEmpty()
                                                                   ? cfg.executionMode
                                                                   : executionMode);
    cfg.recursionDepth = recursionDepth > 0 ? recursionDepth : cfg.recursionDepth;

    if (cfg.configId.trimmed().isEmpty() && !cfg.providerInstanceId.trimmed().isEmpty())
        cfg.configId = cfg.providerInstanceId.trimmed();
    if (cfg.systemPrompt.trimmed().isEmpty())
        cfg.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt(cfg.executionMode);
    return cfg;
}

bool ConfigService::saveDefaultAgentConfig(const LLMConfig& config, QString* errOut) const
{
    if (errOut)
        errOut->clear();

    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), 1);
    obj.insert(QStringLiteral("providerInstanceId"), config.providerInstanceId.trimmed());
    obj.insert(QStringLiteral("selectedModelId"), config.selectedModelId.trimmed());
    obj.insert(QStringLiteral("configId"), config.configId.trimmed());
    obj.insert(QStringLiteral("userName"), config.userName.trimmed());
    obj.insert(QStringLiteral("systemPrompt"), config.systemPrompt);
    obj.insert(QStringLiteral("executionMode"), DefaultPrompts::normalizeExecutionMode(config.executionMode));
    obj.insert(QStringLiteral("recursionDepth"), config.recursionDepth);
    if (!writeJsonObject(defaultAgentConfigPath(), obj)) {
        if (errOut)
            *errOut = tr("写入默认助手配置失败");
        return false;
    }
    return true;
}

QString ConfigService::userMemoryPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("user.md"));
}

QString ConfigService::loadUserMemoryMarkdown(bool* ok) const
{
    if (ok)
        *ok = false;
    QFile file(userMemoryPath());
    if (!file.exists()) {
        if (ok)
            *ok = true;
        return QString();
    }
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QString();
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    if (ok)
        *ok = true;
    return text;
}

bool ConfigService::saveUserMemoryMarkdown(const QString& markdown, QString* errOut) const
{
    if (errOut)
        errOut->clear();
    const QString filePath = userMemoryPath();
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        if (errOut)
            *errOut = tr("创建用户记忆目录失败");
        return false;
    }
    QFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        if (errOut)
            *errOut = tr("写入 user.md 失败");
        return false;
    }
    const QByteArray bytes = markdown.toUtf8();
    const bool ok = (file.write(bytes) == bytes.size());
    file.close();
    if (!ok && errOut)
        *errOut = tr("写入 user.md 内容失败");
    return ok;
}

QString ConfigService::agentHeartbeatInstructionPath(const QString& agentId) const
{
    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return QString();
    if (m_persistence)
        return m_persistence->agentHeartbeatInstructionPath(trimmedAgentId);
    return QDir(dataRootPath()).filePath(QStringLiteral("agents/%1/HEARTBEAT.md").arg(trimmedAgentId));
}

QString ConfigService::agentHeartbeatStatePath(const QString& agentId) const
{
    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return QString();
    return QStringLiteral("SQLite app_state :: heartbeat_runtime:%1").arg(trimmedAgentId);
}

QString ConfigService::heartbeatRuntimeStateLocation(const QString& agentId) const
{
    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return QString();

    return QStringLiteral("SQLite app_state :: heartbeat_runtime:%1").arg(trimmedAgentId);
}

QJsonObject ConfigService::loadHeartbeatRuntimeState(const QString& agentId, bool* ok) const
{
    if (ok)
        *ok = false;

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return QJsonObject();

    if (!m_persistence || !DatabaseManager::instance()->isReady())
        return QJsonObject();

    const QString raw = m_persistence->getAppState(QStringLiteral("heartbeat_runtime:") + trimmedAgentId);
    if (raw.trimmed().isEmpty()) {
        if (ok)
            *ok = true;
        return QJsonObject();
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        if (ok)
            *ok = true;
        return doc.object();
    }
    return QJsonObject();
}

QString ConfigService::readPossiblyMojibakeUtf8File(const QString& filePath, bool* ok) const
{
    if (ok)
        *ok = false;
    QFile file(filePath);
    if (!file.exists()) {
        if (ok)
            *ok = true;
        return QString();
    }
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QString();
    const QString text = decodePossiblyMojibakeUtf8(file.readAll());
    file.close();
    if (ok)
        *ok = true;
    return text;
}

bool ConfigService::writeUtf8TextFile(const QString& filePath, const QString& text, QString* errOut) const
{
    if (errOut)
        errOut->clear();
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        if (errOut)
            *errOut = tr("创建目录失败");
        return false;
    }
    QFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        if (errOut)
            *errOut = tr("打开文件失败");
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    const bool ok = (file.write(bytes) == bytes.size());
    file.close();
    if (!ok && errOut)
        *errOut = tr("写入文件内容失败");
    return ok;
}

void ConfigService::loadConfig()
{
    if (!m_modelFactory) {
        emit configLoaded();
        return;
    }

    QString yamlPath = modelConfigPath();
    if (!QFile::exists(yamlPath)) {
        QDir().mkpath(QFileInfo(yamlPath).absolutePath());
        QString bundledPath = QCoreApplication::applicationDirPath() + "/resources/models.yaml";
        if (QFile::exists(bundledPath)) {
            QFile::copy(bundledPath, yamlPath);
        } else {
            QVector<ModelConfig> emptyModels;
            ModelConfigLoader::saveToFile(yamlPath, emptyModels, "");
        }
    }

    const int schemaVersion = ModelConfigLoader::detectSchemaVersion(yamlPath);
    QString defaultProviderId;
    QString defaultModelId;

    if (schemaVersion >= 2) {
        // 新格式：直接加载 ProviderInstance
        QVector<ProviderInstanceConfig> instances = ModelConfigLoader::loadProviderInstances(yamlPath, true);
        if (instances.isEmpty()) {
            emit configLoaded();
            return;
        }
        for (const ProviderInstanceConfig& inst : instances) {
            m_modelFactory->registerProviderInstance(inst);
        }
        defaultProviderId = ModelConfigLoader::getDefaultProvider(yamlPath);
        defaultModelId = ModelConfigLoader::getDefaultModel(yamlPath);
        if (defaultProviderId.isEmpty())
            defaultProviderId = instances.first().instanceId;
    } else {
        // 旧格式：加载 → 迁移 → 注册 → 保存新格式
        QVector<ModelConfig> models = ModelConfigLoader::loadFromFile(yamlPath, true);
        if (models.isEmpty()) {
            emit configLoaded();
            return;
        }

        for (const ModelConfig& config : models) {
            m_modelFactory->registerModelConfig(config);
        }

        QString oldDefaultConfigId = ModelConfigLoader::getDefaultConfigId(yamlPath);
        if (oldDefaultConfigId.isEmpty())
            oldDefaultConfigId = models.first().configId;

        defaultProviderId = oldDefaultConfigId;

        // 从旧默认配置中提取 modelId 作为默认模型
        ModelConfig defaultOldConfig = ModelConfigLoader::getModelConfig(yamlPath, oldDefaultConfigId, true);
        defaultModelId = defaultOldConfig.modelId;

        // 迁移旧格式到新格式（使用未解析 apiKey 的版本保存）
        QVector<ModelConfig> rawModels = ModelConfigLoader::loadFromFile(yamlPath, false);
        QVector<ProviderInstanceConfig> migrated = ModelConfigLoader::migrateFromV1(rawModels);
        if (!migrated.isEmpty()) {
            ModelConfigLoader::saveProviderInstances(yamlPath, migrated, defaultProviderId, defaultModelId);
            qInfo() << "已将旧格式配置迁移为 schema_version 2";
        }
    }

    LLMConfig agentConfig;
    agentConfig.providerInstanceId = defaultProviderId;
    agentConfig.selectedModelId = defaultModelId;
    agentConfig.configId = defaultProviderId; // 兼容旧路径
    agentConfig.userName = QStringLiteral("TM Agent");
    agentConfig.executionMode = DefaultPrompts::executionModeContinuous();

    // 获取系统提示词
    ProviderInstanceConfig defaultInst = m_modelFactory->getProviderInstance(defaultProviderId);
    Q_UNUSED(defaultInst);
    agentConfig.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt(agentConfig.executionMode);
    agentConfig = loadDefaultAgentConfig(agentConfig);
    if (agentConfig.providerInstanceId.trimmed().isEmpty())
        agentConfig.providerInstanceId = defaultProviderId;
    if (agentConfig.selectedModelId.trimmed().isEmpty())
        agentConfig.selectedModelId = defaultModelId;
    if (agentConfig.configId.trimmed().isEmpty())
        agentConfig.configId = agentConfig.providerInstanceId.trimmed().isEmpty()
            ? defaultProviderId
            : agentConfig.providerInstanceId.trimmed();

    if (m_runtimeManager) {
        m_runtimeManager->setDefaultAgentConfig(agentConfig);
        m_runtimeManager->applyConfigToAllRuntimes();
    }
    if (m_toolDispatcher)
        m_toolDispatcher->setDefaultAgentConfig(agentConfig);

    qInfo() << "已加载配置，默认接入点:" << defaultProviderId << "默认模型:" << defaultModelId;
    emit configLoaded();
}

void ConfigService::saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId)
{
    if (!m_persistence)
        return;
    m_persistence->saveTabState(openAgentIds, activeIdentityId);
}

ConfigService::TabState ConfigService::loadTabState() const
{
    TabState state;
    if (!m_persistence)
        return state;
    const ChatPersistenceService::TabState persistedState = m_persistence->loadTabState();
    state.openAgentIds = persistedState.openAgentIds;
    state.activeIdentityId = persistedState.activeIdentityId;
    return state;
}
