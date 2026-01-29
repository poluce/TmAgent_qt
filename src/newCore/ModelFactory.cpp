#include "ModelFactory.h"
#include "OpenAICompatibleProvider.h"
#include <QDebug>

ModelFactory* ModelFactory::instance()
{
    static ModelFactory factory;
    return &factory;
}

ModelFactory::ParsedModelId ModelFactory::parseModelKey(const QString& id)
{
    ParsedModelId parsed;
    const QString norm = id.trimmed().toLower();
    if (norm.isEmpty()) {
        parsed.model = ModelId::Unknown;
        return parsed;
    }
    if (norm == QStringLiteral("deepseek-chat")) {
        parsed.model = ModelId::DeepSeekChat;
        return parsed;
    }
    if (norm == QStringLiteral("gpt-4o")) {
        parsed.model = ModelId::GPT4o;
        return parsed;
    }
    if (norm == QStringLiteral("claude-3-5-sonnet")) {
        parsed.model = ModelId::Claude35Sonnet;
        return parsed;
    }
    if (norm == QStringLiteral("llama3")) {
        parsed.model = ModelId::Llama3;
        return parsed;
    }
    if (norm == QStringLiteral("gemini-1.5-pro")) {
        parsed.model = ModelId::Gemini15Pro;
        return parsed;
    }
    parsed.model = ModelId::Custom;
    parsed.customModelId = id.trimmed();
    return parsed;
}

QString ModelFactory::modelIdToString(ModelId id)
{
    switch (id) {
    case ModelId::DeepSeekChat:
        return QStringLiteral("deepseek-chat");
    case ModelId::GPT4o:
        return QStringLiteral("gpt-4o");
    case ModelId::Claude35Sonnet:
        return QStringLiteral("claude-3-5-sonnet");
    case ModelId::Llama3:
        return QStringLiteral("llama3");
    case ModelId::Gemini15Pro:
        return QStringLiteral("gemini-1.5-pro");
    case ModelId::Custom:
        return QString();
    case ModelId::Unknown:
    default:
        return QString();
    }
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

ModelFactory::~ModelFactory()
{
    m_providerFactories.clear();
    m_modelConfigs.clear();
}

// ========== 配置管理实现 ==========

void ModelFactory::registerModelConfig(const ModelConfig& config)
{
    if (!config.isValid()) {
        qWarning() << "ModelFactory: Invalid model config for" << config.modelId;
        return;
    }
    
    // 保存配置
    m_modelConfigs.insert(config.modelId, config);
    
    // 自动注册工厂函数
    registerProviderFactory(config.modelId, 
        [this, modelId = config.modelId](QObject* parent) -> LLMProvider* {
            // 从配置创建 Provider
            ModelConfig cfg = m_modelConfigs.value(modelId);
            auto* provider = new OpenAICompatibleProvider(modelId, parent);
            
            // 将配置注入到 Provider
            provider->applyConfig(cfg);
            
            return provider;
        });
    
    qDebug() << "ModelFactory: Registered model config:" << config.modelId;
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

// ========== Provider 创建实现 ==========

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
    
    // 调用工厂函数创建新实例
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
