#include "GovernanceService.h"

#include "ApplicationServices.h"
#include "ChatStateRepository.h"
#include "ConversationService.h"
#include "ConfigService.h"
#include "RuntimeManager.h"
#include "core/agent/McpToolProvider.h"
#include "core/agent/ToolDispatcher.h"
#include "core/persistence/ChatPersistenceService.h"
#include "llm/ModelFactory.h"

GovernanceService::GovernanceService(ApplicationServices& app)
    : m_app(app)
    , m_configService(new ConfigService(&app))
{
}

GovernanceService::~GovernanceService() = default;

void GovernanceService::registerModelConfig(const ModelConfig& config)
{
    if (m_modelFactory)
        m_modelFactory->registerModelConfig(config);
}

void GovernanceService::setDefaultAgentConfig(const LLMConfig& config)
{
    if (m_app.m_conversationService && m_app.m_conversationService->runtimeManager())
        m_app.m_conversationService->runtimeManager()->setDefaultAgentConfig(config);
}

LLMConfig GovernanceService::defaultAgentConfig() const
{
    return (m_app.m_conversationService && m_app.m_conversationService->runtimeManager())
        ? m_app.m_conversationService->runtimeManager()->defaultAgentConfig()
        : LLMConfig();
}

void GovernanceService::applyConfigToAllRuntimes()
{
    if (m_app.m_conversationService && m_app.m_conversationService->runtimeManager())
        m_app.m_conversationService->runtimeManager()->applyConfigToAllRuntimes();
}

void GovernanceService::applyToolDispatcherToAllRuntimes()
{
    if (m_app.m_conversationService && m_app.m_conversationService->runtimeManager())
        m_app.m_conversationService->runtimeManager()->applyToolDispatcherToAllRuntimes();
}

void GovernanceService::applyMcpConfig(const QStringList& specs)
{
    if (m_configService)
        m_configService->applyMcpConfig(specs);
}

QStringList GovernanceService::loadMcpConfigSpecs() const
{
    return m_configService ? m_configService->loadMcpConfigSpecs() : QStringList();
}

bool GovernanceService::saveMcpConfigSpecs(const QStringList& specs) const
{
    return m_configService && m_configService->saveMcpConfigSpecs(specs);
}

bool GovernanceService::saveToolLoopPolicyObject(const QJsonObject& raw, QString* errOut) const
{
    return m_configService && m_configService->saveToolLoopPolicyObject(raw, errOut);
}

QString GovernanceService::mcpConfigPath() const
{
    return m_configService ? m_configService->mcpConfigPath() : QString();
}

QString GovernanceService::modelConfigPath() const
{
    return m_configService ? m_configService->modelConfigPath() : QString();
}

QJsonObject GovernanceService::defaultToolLoopPolicyObject() const
{
    return m_configService ? m_configService->defaultToolLoopPolicyObject() : QJsonObject();
}

QJsonObject GovernanceService::normalizeToolLoopPolicyObject(const QJsonObject& raw) const
{
    return m_configService ? m_configService->normalizeToolLoopPolicyObject(raw) : QJsonObject();
}

QJsonObject GovernanceService::loadToolLoopPolicyObject() const
{
    return m_configService ? m_configService->loadToolLoopPolicyObject() : QJsonObject();
}

QStringList GovernanceService::registeredModelConfigIds() const
{
    return m_modelFactory ? m_modelFactory->registeredConfigIds() : QStringList();
}

QStringList GovernanceService::enabledProviderInstanceIds() const
{
    return m_modelFactory ? m_modelFactory->enabledInstanceIds() : QStringList();
}

QString GovernanceService::displayNameForProviderInstance(const QString& instanceId) const
{
    return m_modelFactory ? m_modelFactory->displayNameForInstance(instanceId) : QString();
}

QList<AvailableModel> GovernanceService::cachedModelsForProviderInstance(const QString& instanceId) const
{
    return m_modelFactory ? m_modelFactory->cachedModels(instanceId) : QList<AvailableModel>();
}

void GovernanceService::fetchModelsForProviderInstanceAsync(const QString& instanceId)
{
    if (m_modelFactory)
        m_modelFactory->fetchModelsAsync(instanceId);
}

QStringList GovernanceService::registeredToolNames() const
{
    return ChatStateRepository::collectToolNamesFrom(m_toolDispatcher);
}

void GovernanceService::initialize(RuntimeManager* runtimeManager)
{
    m_modelFactory = ModelFactory::instance();
    QObject::connect(m_modelFactory,
                     &ModelFactory::modelCacheUpdated,
                     &m_app,
                     &ApplicationServices::modelCatalogUpdated,
                     Qt::UniqueConnection);

    m_toolDispatcher = ToolDispatcher::instance();
    m_toolDispatcher->registerDefaultTools();

    m_mcpProvider.reset(new McpToolProvider(m_toolDispatcher));
    m_toolDispatcher->registerProvider(m_mcpProvider.get(), "mcp");

    m_configService->setPersistence(m_app.m_persistence.get());
    m_configService->setModelFactory(m_modelFactory);
    m_configService->setMcpProvider(m_mcpProvider.get());
    m_configService->setToolDispatcher(m_toolDispatcher);
    m_configService->setRuntimeManager(runtimeManager);
    m_configService->applyMcpConfig(m_configService->loadMcpConfigSpecs());
}

void GovernanceService::setModelConfigPathOverride(const QString& filePath)
{
    if (m_app.m_persistence)
        m_app.m_persistence->setModelConfigPathOverride(filePath);
}

void GovernanceService::loadConfig()
{
    if (m_configService)
        m_configService->loadConfig();
}

ModelFactory* GovernanceService::modelFactory() const
{
    return m_modelFactory;
}

ToolDispatcher* GovernanceService::toolDispatcher() const
{
    return m_toolDispatcher;
}

McpToolProvider* GovernanceService::mcpProvider() const
{
    return m_mcpProvider.get();
}

ConfigService* GovernanceService::configService() const
{
    return m_configService;
}
