#ifndef MODELFACTORY_H
#define MODELFACTORY_H

#include "LLMTypes.h"
#include "LLMProvider.h"
#include "ModelRouter.h"
#include <QObject>
#include <QMap>

/**
 * @brief 模型工厂，统一入口（设计文档 6.1 ModelFactory）
 *
 * 定位：应用级服务，提供模型能力出借。
 * 职责：
 *   - 统一创建与管理 LLMProvider 实例
 *   - 按请求条件返回可用的 LLMProvider
 *   - 屏蔽模型生命周期与复用策略
 *   - 内部持有 ModelRouter 与模型目录（只读）
 * 约束：
 *   - 不保存会话状态
 *   - 不管理工具
 *   - 不编辑或裁剪历史
 *
 * 典型形态：QObject 单例或 ApplicationContext 服务。
 */
class ModelFactory : public QObject {
    Q_OBJECT
public:
    explicit ModelFactory(QObject* parent = nullptr);
    ~ModelFactory() override;

    /**
     * @brief 按能力与约束获取可用的 LLMProvider
     * @param capabilities 本任务需要的能力标签
     * @param constraints 可选的路由约束（成本、延迟、偏好模型等）
     * @return 对应 model_id 的 Provider，若无可选则返回 nullptr
     */
    LLMProvider* getProvider(const QStringList& capabilities,
                             const RouterRequest& constraints = RouterRequest()) const;

    /**
     * @brief 按 model_id 直接获取已注册的 Provider
     * @return 若该 model_id 已注册则返回其 Provider，否则 nullptr
     */
    LLMProvider* getProviderByModelId(const QString& modelId) const;

    /**
     * @brief 注册模型：绑定 model_id 与 LLMProvider，并写入 Router 目录
     * @param descriptor 能力描述（供 ModelRouter 选择）
     * @param provider Provider 实例， ownership 由 ModelFactory 接管（parent 设为 this）
     */
    void registerProvider(const CapabilityDescriptor& descriptor, LLMProvider* provider);

    /**
     * @brief 按 model_id 注销 Provider，并从 Router 移除
     */
    void unregisterProvider(const QString& modelId);

    /**
     * @brief 获取内部路由组件（供配置加载时批量注册模型描述用）
     */
    ModelRouter* router() const { return m_router; }

    /**
     * @brief 已注册的 model_id 列表
     */
    QStringList registeredModelIds() const;

private:
    ModelRouter* m_router = nullptr;
    /// model_id -> LLMProvider（工厂负责其生命周期）
    QMap<QString, LLMProvider*> m_providers;
};

#endif // MODELFACTORY_H
