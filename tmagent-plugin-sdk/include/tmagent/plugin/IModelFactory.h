#ifndef TMAGENT_IMODELFACTORY_H
#define TMAGENT_IMODELFACTORY_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include "../types/CommonTypes.h"

namespace TmAgent {

/**
 * @brief 模型工厂接口
 * 
 * 提供给后端插件用于创建 LLM Provider 的回调接口。
 * 后端插件通过此接口创建模型实例，而无需直接依赖 ModelFactory。
 */
class IModelFactory {
public:
    virtual ~IModelFactory() = default;
    
    /**
     * @brief 创建 LLM Provider 实例
     * 
     * 根据 Agent 配置创建对应的模型提供者。
     * 
     * @param config Agent 配置，包含模型 ID 和接入点信息
     * @param error 错误信息输出参数（可选）
     * @return QObject* Provider 实例，失败返回 nullptr
     * 
     * @note 返回的对象由调用者负责管理生命周期
     */
    virtual QObject* createProvider(const AgentConfig& config,
                                   QString* error = nullptr) = 0;
    
    /**
     * @brief 获取模型能力列表
     * 
     * 查询指定模型支持的功能特性。
     * 
     * @param modelId 模型 ID
     * @return QStringList 能力列表（例如："function_calling", "streaming", "vision"）
     */
    virtual QStringList getModelCapabilities(const QString& modelId) const = 0;
};

} // namespace TmAgent

#endif // TMAGENT_IMODELFACTORY_H
