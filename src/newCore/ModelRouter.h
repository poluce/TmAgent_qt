#ifndef MODELROUTER_H
#define MODELROUTER_H

#include "LLMTypes.h"
#include <QList>
#include <QObject>

class LLMProvider;

/**
 * @brief 模型选择策略组件（设计文档 6.2 ModelRouter）
 *
 * 定位：ModelFactory 内部组件，作为模型选择策略入口。
 * 职责：
 *   - 根据任务与约束选择模型
 *   - 支持成本、延迟、可用性等策略
 *   - 支持失败回退与降级链
 * 约束：
 *   - 不持有会话历史
 *   - 不直接发起模型调用
 */
class ModelRouter : public QObject {
    Q_OBJECT
public:
    explicit ModelRouter(QObject* parent = nullptr);
    ~ModelRouter() override;

    /**
     * @brief 根据请求条件选择模型
     * @param request 任务类型、必需能力、成本/延迟偏好等
     * @return 选定的 model_id、decision_reason、fallback_chain
     */
    RouterResult selectModel(const RouterRequest& request) const;

    /**
     * @brief 注册模型：将 model_id 与能力描述关联，供选择使用
     * @param descriptor 模型能力描述（只读，内部做拷贝）
     */
    void registerModel(const CapabilityDescriptor& descriptor);

    /**
     * @brief 按 model_id 移除注册
     */
    void unregisterModel(const QString& modelId);

    /**
     * @brief 获取已注册的 model_id 列表
     */
    QStringList registeredModelIds() const;

private:
    /// 内部模型目录：model_id -> CapabilityDescriptor
    QList<CapabilityDescriptor> m_descriptors;
};

#endif // MODELROUTER_H
