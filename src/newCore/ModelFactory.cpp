#include "ModelFactory.h"
#include "AnthropicProvider.h"
#include "OpenAICompatibleProvider.h"
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

ModelFactory::ModelFactory(QObject* parent)
    : QObject(parent)
{
}

ModelFactory::~ModelFactory() = default;

// ========== 配置管理 ==========

void ModelFactory::registerModelConfig(const ModelConfig& config)
{
    if (!config.isValid()) {
        qWarning() << "ModelFactory: Invalid model config for" << config.modelId;
        return;
    }

    m_modelConfigs.insert(config.modelId, config);

    const bool isAnthropic = (config.provider.toLower() == QStringLiteral("anthropic") || config.provider.toLower() == QStringLiteral("claude"));

    registerProviderFactory(config.modelId, [this, modelId = config.modelId, isAnthropic](QObject* parent) -> LLMProvider* {
        ModelConfig cfg = m_modelConfigs.value(modelId);
        LLMProvider* provider = isAnthropic
            ? static_cast<LLMProvider*>(new AnthropicProvider(modelId, parent))
            : static_cast<LLMProvider*>(new OpenAICompatibleProvider(modelId, parent));
        provider->applyConfig(cfg);
        return provider;
    });

    qDebug() << "ModelFactory: Registered model config:" << config.modelId
             << "provider:" << config.provider;
}

ModelConfig ModelFactory::getModelConfig(const QString& modelId) const
{
    return m_modelConfigs.value(modelId);
}

void ModelFactory::updateModelConfig(const QString& modelId, const ModelConfig& config)
{
    if (!m_modelConfigs.contains(modelId)) {
        qWarning() << "ModelFactory: Cannot update non-existent model:" << modelId;
        return;
    }

    m_modelConfigs[modelId] = config;
    emit modelConfigUpdated(modelId);

    qDebug() << "ModelFactory: Updated model config:" << modelId;
}

bool ModelFactory::hasModelConfig(const QString& modelId) const
{
    return m_modelConfigs.contains(modelId);
}

// ========== Provider 创建 ==========

void ModelFactory::registerProviderFactory(const QString& modelId, ProviderFactory factory)
{
    if (modelId.isEmpty() || !factory) {
        qWarning() << "ModelFactory: Invalid modelId or factory function";
        return;
    }
    m_providerFactories.insert(modelId, factory);
}

LLMProvider* ModelFactory::createProvider(const QString& modelId, QObject* parent)
{
    if (!m_providerFactories.contains(modelId)) {
        qWarning() << "ModelFactory: No factory registered for model:" << modelId;
        return nullptr;
    }
    return m_providerFactories[modelId](parent);
}

bool ModelFactory::hasModel(const QString& modelId) const
{
    return m_providerFactories.contains(modelId);
}

void ModelFactory::unregisterModel(const QString& modelId)
{
    m_providerFactories.remove(modelId);
    m_modelConfigs.remove(modelId);
}

QStringList ModelFactory::registeredModelIds() const
{
    return m_providerFactories.keys();
}
