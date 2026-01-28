#include "ModelFactory.h"
#include "OpenAICompatibleProvider.h"
#include <QDebug>

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
