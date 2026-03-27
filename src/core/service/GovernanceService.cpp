#include "GovernanceService.h"

#include "ApplicationServices.h"
#include "ChatStateRepository.h"
#include "ConversationService.h"
#include "ConfigService.h"
#include "RuntimeManager.h"
#include "core/agent/McpToolProvider.h"
#include "core/agent/ToolDispatcher.h"
#include "core/agent/ToolPluginManager.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/utils/DefaultPrompts.h"
#include "llm/ModelFactory.h"
#include <QDateTime>
#include <algorithm>

namespace {

LLMConfig mergeDefaultAgentConfig(const LLMConfig& baseConfig, const LLMConfig& overrideConfig)
{
    LLMConfig merged = baseConfig;

    auto applyString = [&merged](QString LLMConfig::*member, const QString& value) {
        if (!value.trimmed().isEmpty())
            merged.*member = value.trimmed();
    };

    applyString(&LLMConfig::providerInstanceId, overrideConfig.providerInstanceId);
    applyString(&LLMConfig::selectedModelId, overrideConfig.selectedModelId);

    const QString normalizedOverrideConfigId = overrideConfig.configId.trimmed();
    if (!normalizedOverrideConfigId.isEmpty()) {
        merged.configId = normalizedOverrideConfigId;
        if (overrideConfig.providerInstanceId.trimmed().isEmpty())
            merged.providerInstanceId = normalizedOverrideConfigId;
    }

    applyString(&LLMConfig::userName, overrideConfig.userName);
    applyString(&LLMConfig::systemPrompt, overrideConfig.systemPrompt);
    applyString(&LLMConfig::workspaceDir, overrideConfig.workspaceDir);

    if (!overrideConfig.executionMode.trimmed().isEmpty())
        merged.executionMode = DefaultPrompts::normalizeExecutionMode(overrideConfig.executionMode);
    else
        merged.executionMode = DefaultPrompts::normalizeExecutionMode(merged.executionMode);

    if (overrideConfig.recursionDepth > 0)
        merged.recursionDepth = overrideConfig.recursionDepth;

    if (!overrideConfig.uuid.trimmed().isEmpty())
        merged.uuid = overrideConfig.uuid.trimmed();

    if (merged.configId.trimmed().isEmpty() && !merged.providerInstanceId.trimmed().isEmpty())
        merged.configId = merged.providerInstanceId.trimmed();
    if (merged.systemPrompt.trimmed().isEmpty())
        merged.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt(merged.executionMode);
    return merged;
}

} // namespace

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
    const LLMConfig merged = mergeDefaultAgentConfig(defaultAgentConfig(), config);
    if (m_configService)
        m_configService->saveDefaultAgentConfig(merged, nullptr);
    if (m_toolDispatcher)
        m_toolDispatcher->setDefaultAgentConfig(merged);
    if (m_app.m_conversationService && m_app.m_conversationService->runtimeManager())
        m_app.m_conversationService->runtimeManager()->setDefaultAgentConfig(merged);
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

QString GovernanceService::toolPluginConfigPath() const
{
    return m_configService ? m_configService->toolPluginConfigPath() : QString();
}

QJsonObject GovernanceService::defaultToolPluginConfigObject() const
{
    return m_configService ? m_configService->defaultToolPluginConfigObject() : QJsonObject();
}

QJsonObject GovernanceService::normalizeToolPluginConfigObject(const QJsonObject& raw) const
{
    return m_configService ? m_configService->normalizeToolPluginConfigObject(raw) : QJsonObject();
}

QJsonObject GovernanceService::loadToolPluginConfigObject() const
{
    return m_configService ? m_configService->loadToolPluginConfigObject() : QJsonObject();
}

bool GovernanceService::saveToolPluginConfigObject(const QJsonObject& raw, QString* errOut) const
{
    return m_configService && m_configService->saveToolPluginConfigObject(raw, errOut);
}

QList<ToolPluginInfo> GovernanceService::toolPluginInfos() const
{
    QList<ToolPluginInfo> infos;
    if (m_toolPluginManager)
        infos = m_toolPluginManager->pluginInfos();

    if (m_mcpProvider) {
        ToolPluginInfo mcpInfo;
        mcpInfo.enabled = true;
        mcpInfo.loaded = true;
        mcpInfo.externalProvider = true;
        mcpInfo.sourcePath = m_configService ? m_configService->mcpConfigPath() : QString();
        mcpInfo.descriptor.pluginId = QStringLiteral("mcp_provider");
        mcpInfo.descriptor.displayName = QStringLiteral("MCP 外部工具提供者");
        mcpInfo.descriptor.version = QStringLiteral("runtime");
        mcpInfo.descriptor.category = QStringLiteral("external");
        mcpInfo.descriptor.description = QStringLiteral("由 MCP 配置动态发现的外部工具提供者。");
        const QList<Tool> mcpTools = m_mcpProvider->listTools();
        for (const Tool& tool : mcpTools)
            mcpInfo.descriptor.toolNames.append(tool.name);
        mcpInfo.descriptor.toolNames.removeDuplicates();
        mcpInfo.health.state = QStringLiteral("ok");
        mcpInfo.health.toolCount = mcpTools.size();
        mcpInfo.health.checkedAtUtc = QDateTime::currentDateTimeUtc();
        infos.append(mcpInfo);
    }

    std::sort(infos.begin(), infos.end(), [](const ToolPluginInfo& a, const ToolPluginInfo& b) {
        return a.descriptor.displayName.compare(b.descriptor.displayName, Qt::CaseInsensitive) < 0;
    });
    return infos;
}

void GovernanceService::reloadToolPlugins()
{
    if (!m_toolPluginManager)
        return;
    m_toolPluginManager->setConfigObject(loadToolPluginConfigObject());
    m_toolPluginManager->reload();
    if (m_configService)
        m_configService->saveToolPluginConfigObject(m_toolPluginManager->configObject(), nullptr);
    rebuildToolProviders();
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
    m_toolDispatcher->setDefaultAgentConfig(defaultAgentConfig());

    m_mcpProvider.reset(new McpToolProvider(m_toolDispatcher));

    m_configService->setPersistence(m_app.m_persistence.get());
    m_configService->setModelFactory(m_modelFactory);
    m_configService->setMcpProvider(m_mcpProvider.get());
    m_configService->setToolDispatcher(m_toolDispatcher);
    m_configService->setRuntimeManager(runtimeManager);

    m_toolPluginManager.reset(new ToolPluginManager(m_toolDispatcher, nullptr));
    m_toolPluginManager->setConfigObject(m_configService->loadToolPluginConfigObject());
    m_toolPluginManager->initialize();
    m_configService->saveToolPluginConfigObject(m_toolPluginManager->configObject(), nullptr);
    rebuildToolProviders();

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

ToolPluginManager* GovernanceService::toolPluginManager() const
{
    return m_toolPluginManager.get();
}

ConfigService* GovernanceService::configService() const
{
    return m_configService;
}

void GovernanceService::rebuildToolProviders()
{
    if (!m_toolDispatcher)
        return;

    m_toolDispatcher->clearProviders();

    if (m_toolPluginManager) {
        const QList<ToolPluginManager::ProviderBinding> bindings =
            m_toolPluginManager->activeProviders();
        for (const ToolPluginManager::ProviderBinding& binding : bindings)
            m_toolDispatcher->registerProvider(binding.provider, binding.providerName);
    }

    if (m_mcpProvider)
        m_toolDispatcher->registerProvider(m_mcpProvider.get(), QStringLiteral("mcp"));
}
