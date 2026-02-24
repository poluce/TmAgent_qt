#include "ConfigService.h"
#include "RuntimeManager.h"
#include "core/agent/McpToolProvider.h"
#include "core/agent/ToolDispatcher.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/utils/DefaultPrompts.h"
#include "core/utils/ModelConfigLoader.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>

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

QString ConfigService::modelConfigPath() const
{
    return m_persistence ? m_persistence->modelConfigPath() : QString();
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

    QVector<ModelConfig> models = ModelConfigLoader::loadFromFile(yamlPath, true);
    if (models.isEmpty()) {
        emit configLoaded();
        return;
    }

    for (const ModelConfig& config : models) {
        m_modelFactory->registerModelConfig(config);
    }

    QString defaultConfigId = ModelConfigLoader::getDefaultConfigId(yamlPath);
    if (defaultConfigId.isEmpty()) {
        defaultConfigId = models.first().configId;
    }

    ModelConfig defaultConfig = ModelConfigLoader::getModelConfig(yamlPath, defaultConfigId, true);
    if (defaultConfig.systemPrompt.trimmed().isEmpty()) {
        defaultConfig.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt();
    }

    LLMConfig agentConfig;
    agentConfig.configId = defaultConfigId;
    agentConfig.systemPrompt = defaultConfig.systemPrompt;
    agentConfig.userName = QStringLiteral("TM Agent");

    if (m_runtimeManager) {
        m_runtimeManager->setDefaultAgentConfig(agentConfig);
        m_runtimeManager->applyConfigToAllRuntimes();
    }

    qInfo() << "已加载" << models.size() << "个模型，默认:" << defaultConfigId;
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
