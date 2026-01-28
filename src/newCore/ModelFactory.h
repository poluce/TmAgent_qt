#ifndef MODELFACTORY_H
#define MODELFACTORY_H

#include "LLMTypes.h"
#include "LLMProvider.h"
#include <QObject>
#include <QMap>

/**
 * @brief 模型工厂，按模型类型分配 LLMProvider（不做路由）
 *
 * 定位：应用级服务，提供模型能力出借。
 * 职责：
 *   - 统一创建与管理 LLMProvider 实例
 *   - 根据 Agent 的模型类型（modelId）查表分配对应的 LLMProvider
 *   - 屏蔽模型生命周期与复用策略
 * 约束：
 *   - 不保存会话状态
 *   - 不管理工具
 *   - 不编辑或裁剪历史
 *   - 不含路由逻辑，仅按 modelId 查表
 */
class ModelFactory : public QObject {
    Q_OBJECT
public:
    explicit ModelFactory(QObject* parent = nullptr);
    ~ModelFactory() override;

    /**
     * @brief 按 model_id 直接获取已注册的 Provider
     * @return 若该 model_id 已注册则返回其 Provider，否则 nullptr
     */
    LLMProvider* getProviderByModelId(const QString& modelId) const;

    /**
     * @brief 注册模型：绑定 model_id 与 LLMProvider
     * @param descriptor 能力描述，descriptor.modelId 作为查表 key
     * @param provider Provider 实例，ownership 由 ModelFactory 接管（parent 设为 this）
     */
    void registerProvider(const CapabilityDescriptor& descriptor, LLMProvider* provider);

    /**
     * @brief 按 model_id 注销 Provider
     */
    void unregisterProvider(const QString& modelId);

    /**
     * @brief 已注册的 model_id 列表
     */
    QStringList registeredModelIds() const;

private:
    /// model_id -> LLMProvider（工厂负责其生命周期）
    QMap<QString, LLMProvider*> m_providers;
};

#endif // MODELFACTORY_H
