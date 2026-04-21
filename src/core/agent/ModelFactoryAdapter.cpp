#include "ModelFactoryAdapter.h"
#include "llm/ModelFactory.h"
#include "llm/LLMProvider.h"

ModelFactoryAdapter::ModelFactoryAdapter(ModelFactory* factory, QObject* parent)
    : QObject(parent)
    , m_factory(factory)
{
    Q_ASSERT(factory != nullptr);
}

ModelFactoryAdapter::~ModelFactoryAdapter() = default;

QObject* ModelFactoryAdapter::createProvider(const TmAgent::AgentConfig& config,
                                            QString* error)
{
    if (!m_factory) {
        if (error) {
            *error = "ModelFactory 未初始化";
        }
        return nullptr;
    }
    
    // 优先使用新路径：providerInstanceId + selectedModelId
    if (!config.providerInstanceId.trimmed().isEmpty() 
        && !config.selectedModelId.trimmed().isEmpty()) {
        
        LLMProvider* provider = m_factory->createProvider(
            config.providerInstanceId,
            config.selectedModelId,
            this  // 使用 adapter 作为父对象
        );
        
        if (!provider && error) {
            *error = QString("无法创建 Provider：接入点 '%1'，模型 '%2'")
                .arg(config.providerInstanceId)
                .arg(config.selectedModelId);
        }
        
        return provider;
    }
    
    // 回退到旧路径：configId
    if (!config.configId.trimmed().isEmpty()) {
        LLMProvider* provider = m_factory->createProvider(
            config.configId,
            this  // 使用 adapter 作为父对象
        );
        
        if (!provider && error) {
            *error = QString("无法创建 Provider：配置 ID '%1'")
                .arg(config.configId);
        }
        
        return provider;
    }
    
    // 配置无效
    if (error) {
        *error = "AgentConfig 无效：缺少 providerInstanceId/selectedModelId 或 configId";
    }
    return nullptr;
}

QStringList ModelFactoryAdapter::getModelCapabilities(const QString& modelId) const
{
    if (!m_factory) {
        return QStringList();
    }
    
    // 注意：当前 ModelFactory 没有 getModelCapabilities 方法
    // 这是一个占位实现，未来需要在 ModelFactory 中添加此功能
    // 目前返回空列表，表示能力未知
    
    // TODO: 在 ModelFactory 中实现 getModelCapabilities 方法
    // 可能的实现方式：
    // 1. 从模型配置中读取能力列表
    // 2. 从缓存的模型信息中获取
    // 3. 查询 Provider 实例的能力
    
    Q_UNUSED(modelId);
    return QStringList();
}
