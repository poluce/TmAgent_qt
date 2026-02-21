#include "ModelFactory.h"
#include "AnthropicProvider.h"
#include "OpenAICompatibleProvider.h"
#include "core/agent/ToolTypes.h"
#include <QDebug>

namespace {
struct ModelIdEntry {
    const char* key;
    ModelId id;
};

static const ModelIdEntry s_modelIdTable[] = {
    { "deepseek-chat", ModelId::DeepSeekChat },
    { "gpt-4o", ModelId::GPT4o },
    { "claude-3-5-sonnet", ModelId::Claude35Sonnet },
    { "llama3", ModelId::Llama3 },
    { "gemini-1.5-pro", ModelId::Gemini15Pro },
};
static constexpr int s_modelIdTableSize = sizeof(s_modelIdTable) / sizeof(s_modelIdTable[0]);

bool isAnthropicProviderId(const QString& providerId)
{
    const QString id = providerId.trimmed().toLower();
    return id == QStringLiteral("anthropic")
        || id == QStringLiteral("claude")
        || id == QStringLiteral("claudeai");
}
} // namespace

ModelFactory* ModelFactory::instance()
{
    static ModelFactory factory;
    return &factory;
}

ModelFactory::ParsedModelId ModelFactory::parseModelKey(const QString& id)
{
    ParsedModelId parsed;
    const QString norm = id.trimmed().toLower();
    if (norm.isEmpty())
        return parsed;

    for (int i = 0; i < s_modelIdTableSize; ++i) {
        if (norm == QLatin1String(s_modelIdTable[i].key)) {
            parsed.model = s_modelIdTable[i].id;
            return parsed;
        }
    }
    parsed.model = ModelId::Custom;
    parsed.customModelId = id.trimmed();
    return parsed;
}

QString ModelFactory::modelIdToString(ModelId id)
{
    for (int i = 0; i < s_modelIdTableSize; ++i) {
        if (s_modelIdTable[i].id == id)
            return QString::fromLatin1(s_modelIdTable[i].key);
    }
    return QString();
}

QString ModelFactory::resolveModelKey(ModelId model, const QString& customModelId)
{
    if (model == ModelId::Custom)
        return customModelId.trimmed();
    return modelIdToString(model);
}

QString ModelFactory::resolveConfigKey(const LLMConfig& llmConfig)
{
    if (!llmConfig.configId.isEmpty())
        return llmConfig.configId;
    return resolveModelKey(llmConfig.model, llmConfig.customModelId);
}

ModelFactory::ModelFactory(QObject* parent)
    : QObject(parent)
{
}

ModelFactory::~ModelFactory() = default;

// ========== 配置管理（以 configId 为键）==========

void ModelFactory::registerModelConfig(const ModelConfig& config)
{
    if (!config.isValid()) {
        qWarning() << "ModelFactory: Invalid model config for" << config.configId;
        return;
    }

    const QString key = config.configId;
    m_modelConfigs.insert(key, config);

    registerProviderFactory(key, [this, key](QObject* parent) -> LLMProvider* {
        ModelConfig cfg = m_modelConfigs.value(key);
        // Provider 构造仍传 modelId（实际发给 API 的模型名）
        LLMProvider* provider = isAnthropicProviderId(cfg.provider)
            ? static_cast<LLMProvider*>(new AnthropicProvider(cfg.modelId, parent))
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

// ========== Provider 创建 ==========

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
    if (!m_providerFactories.contains(configId)) {
        qWarning() << "ModelFactory: No factory registered for config:" << configId;
        return nullptr;
    }
    return m_providerFactories[configId](parent);
}

bool ModelFactory::hasModel(const QString& configId) const
{
    return m_providerFactories.contains(configId);
}

void ModelFactory::unregisterModel(const QString& configId)
{
    m_providerFactories.remove(configId);
    m_modelConfigs.remove(configId);
}

QStringList ModelFactory::registeredModelIds() const
{
    return m_providerFactories.keys();
}
