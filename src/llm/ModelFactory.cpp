#include "ModelFactory.h"
#include "AnthropicProvider.h"
#include "DeepSeekProvider.h"
#include "LLMTypes.h"
#include "OpenAICompatibleProvider.h"
#include <QDebug>

namespace {
bool isAnthropicProviderId(const QString& providerId)
{
    const QString id = providerId.trimmed().toLower();
    return id == QStringLiteral("anthropic")
        || id == QStringLiteral("claude")
        || id == QStringLiteral("claudeai");
}

bool isDeepSeekProviderId(const QString& providerId)
{
    const QString id = providerId.trimmed().toLower();
    return id == QStringLiteral("deepseek")
        || id.startsWith(QStringLiteral("deepseek-"));
}
} // namespace

ModelFactory* ModelFactory::instance()
{
    static ModelFactory factory;
    return &factory;
}

QString ModelFactory::resolveConfigKey(const LLMConfig& llmConfig)
{
    return llmConfig.configId.trimmed();
}

QString ModelFactory::resolveInstanceId(const LLMConfig& llmConfig)
{
    const QString inst = llmConfig.providerInstanceId.trimmed();
    if (!inst.isEmpty())
        return inst;
    return llmConfig.configId.trimmed();
}

QString ModelFactory::resolveModelId(const LLMConfig& llmConfig) const
{
    const QString sel = llmConfig.selectedModelId.trimmed();
    if (!sel.isEmpty())
        return sel;
    // 回退：从旧 ModelConfig 查 modelId
    const QString key = llmConfig.configId.trimmed();
    if (!key.isEmpty()) {
        const ModelConfig cfg = m_modelConfigs.value(key);
        if (!cfg.modelId.isEmpty())
            return cfg.modelId;
    }
    return QString();
}

ModelFactory::ModelFactory(QObject* parent)
    : QObject(parent)
{
}

ModelFactory::~ModelFactory() = default;

// ========== 接入点实例管理（新路径）==========

void ModelFactory::registerProviderInstance(const ProviderInstanceConfig& config)
{
    if (!config.isValid()) {
        qWarning() << "ModelFactory: Invalid provider instance:" << config.instanceId;
        return;
    }

    const QString key = config.instanceId.trimmed();
    m_providerInstances.insert(key, config);

    qDebug() << "ModelFactory: Registered provider instance:" << key
             << "type:" << config.providerType
             << "url:" << config.baseUrl;
}

ProviderInstanceConfig ModelFactory::getProviderInstance(const QString& instanceId) const
{
    return m_providerInstances.value(instanceId.trimmed());
}

bool ModelFactory::hasProviderInstance(const QString& instanceId) const
{
    return m_providerInstances.contains(instanceId.trimmed());
}

QStringList ModelFactory::registeredInstanceIds() const
{
    return m_providerInstances.keys();
}

QStringList ModelFactory::enabledInstanceIds() const
{
    QStringList result;
    for (auto it = m_providerInstances.constBegin(); it != m_providerInstances.constEnd(); ++it) {
        if (it.value().enabled)
            result.append(it.key());
    }
    return result;
}

QString ModelFactory::displayNameForInstance(const QString& instanceId) const
{
    const ProviderInstanceConfig inst = m_providerInstances.value(instanceId.trimmed());
    if (!inst.displayName.isEmpty())
        return inst.displayName;
    return instanceId;
}

// ========== 模型缓存 ==========

QList<AvailableModel> ModelFactory::cachedModels(const QString& instanceId) const
{
    return m_modelCache.value(instanceId.trimmed());
}

void ModelFactory::setCachedModels(const QString& instanceId, const QList<AvailableModel>& models)
{
    m_modelCache.insert(instanceId.trimmed(), models);
    emit modelCacheUpdated(instanceId.trimmed());
}

void ModelFactory::clearModelCache(const QString& instanceId)
{
    m_modelCache.remove(instanceId.trimmed());
}

void ModelFactory::fetchModelsAsync(const QString& instanceId)
{
    const QString key = instanceId.trimmed();
    if (key.isEmpty())
        return;

    if (!m_providerInstances.contains(key)) {
        qWarning() << "ModelFactory::fetchModelsAsync: unknown instance:" << key;
        return;
    }

    // 用一个占位 modelId 创建临时 provider，仅用于拉取模型列表
    LLMProvider* tmpProvider = createProvider(key, QStringLiteral("_list_models_"), this);
    if (!tmpProvider) {
        qWarning() << "ModelFactory::fetchModelsAsync: failed to create provider for:" << key;
        return;
    }

    connect(tmpProvider, &LLMProvider::modelListReceived, this, [this, key, tmpProvider](const QList<AvailableModel>& models) {
        tmpProvider->deleteLater();
        if (!models.isEmpty()) {
            setCachedModels(key, models);
        } else {
            qDebug() << "ModelFactory::fetchModelsAsync: empty model list for:" << key;
        }
    });

    tmpProvider->fetchModelList();
}

// ========== Provider 创建（新路径：instanceId + modelId）==========

LLMProvider* ModelFactory::createProvider(const QString& instanceId, const QString& modelId, QObject* parent)
{
    const QString key = instanceId.trimmed();

    // 优先从新路径查找接入点实例
    if (m_providerInstances.contains(key)) {
        const ProviderInstanceConfig inst = m_providerInstances.value(key);
        // 构造一个临时 ModelConfig 用于 applyConfig
        ModelConfig tmpCfg;
        tmpCfg.configId = inst.instanceId;
        tmpCfg.modelId = modelId;
        tmpCfg.displayName = inst.displayName;
        tmpCfg.provider = inst.providerType;
        tmpCfg.apiKey = inst.apiKey;
        tmpCfg.baseUrl = inst.baseUrl;
        tmpCfg.authType = inst.authType;
        tmpCfg.temperature = inst.defaultTemperature;
        tmpCfg.maxTokens = inst.defaultMaxTokens;
        tmpCfg.timeoutMs = inst.defaultTimeoutMs;
        tmpCfg.capabilities = inst.capabilities;
        tmpCfg.toolCalling = inst.toolCalling;
        tmpCfg.contextLength = inst.contextLength;
        tmpCfg.extraConfig = inst.extraConfig;

        LLMProvider* provider = isAnthropicProviderId(inst.providerType)
            ? static_cast<LLMProvider*>(new AnthropicProvider(modelId, parent))
            : isDeepSeekProviderId(inst.providerType)
            ? static_cast<LLMProvider*>(new DeepSeekProvider(modelId, parent))
            : static_cast<LLMProvider*>(new OpenAICompatibleProvider(modelId, parent));
        provider->applyConfig(tmpCfg);
        return provider;
    }

    // 回退：旧路径 factory
    if (m_providerFactories.contains(key)) {
        return m_providerFactories[key](parent);
    }

    qWarning() << "ModelFactory: No provider instance or factory for:" << key << "modelId:" << modelId;
    return nullptr;
}

// ========== 旧 API（兼容，内部桥接到新路径）==========

void ModelFactory::registerModelConfig(const ModelConfig& config)
{
    if (!config.isValid()) {
        qWarning() << "ModelFactory: Invalid model config for" << config.configId;
        return;
    }

    const QString key = config.configId;
    m_modelConfigs.insert(key, config);

    // 桥接：同时构造 ProviderInstanceConfig 并注册
    ProviderInstanceConfig inst;
    inst.instanceId = config.configId;
    inst.enabled = config.enabled;
    inst.displayName = config.displayName;
    inst.providerType = config.provider;
    inst.baseUrl = config.baseUrl;
    inst.apiKey = config.apiKey;
    inst.authType = config.authType;
    inst.defaultTemperature = config.temperature;
    inst.defaultMaxTokens = config.maxTokens;
    inst.defaultTimeoutMs = config.timeoutMs;
    inst.capabilities = config.capabilities;
    inst.toolCalling = config.toolCalling;
    inst.contextLength = config.contextLength;
    inst.extraConfig = config.extraConfig;
    m_providerInstances.insert(key, inst);

    // 把 modelId 存入缓存作为初始可用模型
    if (!config.modelId.isEmpty()) {
        AvailableModel model;
        model.modelId = config.modelId;
        model.displayName = config.displayName;
        QList<AvailableModel> models = m_modelCache.value(key);
        bool found = false;
        for (const AvailableModel& m : models) {
            if (m.modelId == config.modelId) {
                found = true;
                break;
            }
        }
        if (!found) {
            models.append(model);
            m_modelCache.insert(key, models);
        }
    }

    // 旧路径：注册 factory（保持向后兼容）
    registerProviderFactory(key, [this, key](QObject* parent) -> LLMProvider* {
        ModelConfig cfg = m_modelConfigs.value(key);
        LLMProvider* provider = isAnthropicProviderId(cfg.provider)
            ? static_cast<LLMProvider*>(new AnthropicProvider(cfg.modelId, parent))
            : isDeepSeekProviderId(cfg.provider)
            ? static_cast<LLMProvider*>(new DeepSeekProvider(cfg.modelId, parent))
            : static_cast<LLMProvider*>(new OpenAICompatibleProvider(cfg.modelId, parent));
        provider->applyConfig(cfg);
        return provider;
    });

    qDebug() << "ModelFactory: Registered config:" << key
             << "modelId:" << config.modelId
             << "provider:" << config.provider;
}

ModelConfig ModelFactory::getModelConfig(const QString& configId) const
{
    return m_modelConfigs.value(configId);
}

void ModelFactory::updateModelConfig(const QString& configId, const ModelConfig& config)
{
    if (!m_modelConfigs.contains(configId)) {
        qWarning() << "ModelFactory: Cannot update non-existent config:" << configId;
        return;
    }

    m_modelConfigs[configId] = config;
    emit modelConfigUpdated(configId);

    qDebug() << "ModelFactory: Updated config:" << configId;
}

bool ModelFactory::hasModelConfig(const QString& configId) const
{
    return m_modelConfigs.contains(configId);
}

QStringList ModelFactory::enabledConfigIds() const
{
    QStringList result;
    for (auto it = m_modelConfigs.constBegin(); it != m_modelConfigs.constEnd(); ++it) {
        if (it.value().enabled)
            result.append(it.key());
    }
    return result;
}

QString ModelFactory::displayNameForConfig(const QString& configId) const
{
    const ModelConfig cfg = m_modelConfigs.value(configId);
    if (!cfg.displayName.isEmpty())
        return cfg.displayName;
    if (!cfg.modelId.isEmpty())
        return cfg.modelId;
    return configId;
}

// ========== Provider 创建（旧路径）==========

void ModelFactory::registerProviderFactory(const QString& configId, ProviderFactory factory)
{
    if (configId.isEmpty() || !factory) {
        qWarning() << "ModelFactory: Invalid configId or factory function";
        return;
    }
    m_providerFactories.insert(configId, factory);
}

LLMProvider* ModelFactory::createProvider(const QString& configId, QObject* parent)
{
    // 旧路径内部转发到新路径
    const QString key = configId.trimmed();
    if (m_providerInstances.contains(key)) {
        // 从旧 ModelConfig 获取 modelId
        const ModelConfig cfg = m_modelConfigs.value(key);
        const QString modelId = cfg.modelId.isEmpty() ? key : cfg.modelId;
        return createProvider(key, modelId, parent);
    }

    if (!m_providerFactories.contains(key)) {
        qWarning() << "ModelFactory: No factory registered for config:" << key;
        return nullptr;
    }
    return m_providerFactories[key](parent);
}

bool ModelFactory::hasModel(const QString& configId) const
{
    return m_providerFactories.contains(configId)
        || m_providerInstances.contains(configId.trimmed());
}

void ModelFactory::unregisterModel(const QString& configId)
{
    m_providerFactories.remove(configId);
    m_modelConfigs.remove(configId);
    m_providerInstances.remove(configId);
    m_modelCache.remove(configId);
}

QStringList ModelFactory::registeredModelIds() const
{
    return m_providerFactories.keys();
}
