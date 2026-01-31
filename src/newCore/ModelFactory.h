#ifndef MODELFACTORY_H
#define MODELFACTORY_H

#include "LLMTypes.h"
#include "LLMProvider.h"
#include "ModelId.h"
#include <QObject>
#include <QMap>
#include <functional>

/**
 * @brief 模型工厂，动态创建 LLMProvider 实例
 *
 * 定位：应用级服务，提供模型能力出借。
 * 职责：
 *   - 管理 Provider 工厂函数（按 modelId 索引）
 *   - 为每个 Agent 创建独立的 LLMProvider 实例
 *   - 支持多 Agent 并发请求同一模型
 * 约束：
 *   - 不保存会话状态
 *   - 不管理工具
 *   - 不编辑或裁剪历史
 *   - Provider 的生命周期由调用方（通常是 LLMAgent）管理
 */
class ModelFactory : public QObject {
    Q_OBJECT
public:
    /// Provider 工厂函数类型：接收 parent，返回新的 Provider 实例
    using ProviderFactory = std::function<LLMProvider*(QObject* parent)>;

    static ModelFactory* instance();

    struct ParsedModelId {
        ModelId model = ModelId::Unknown;
        QString customModelId;
    };

    // ========== 模型枚举解析 ==========
    static ParsedModelId parseModelKey(const QString& id);
    static QString modelIdToString(ModelId id);
    static QString resolveModelKey(ModelId model, const QString& customModelId);

    // ========== 配置管理接口（推荐使用） ==========
    
    /**
     * @brief 注册模型配置（推荐方式）
     * @param config 模型配置信息
     * 
     * 自动注册工厂函数，Agent 只需调用 createProvider(modelId) 即可获取配置好的 Provider
     */
    void registerModelConfig(const ModelConfig& config);

    /**
     * @brief 获取模型配置（只读）
     * @param modelId 模型 ID
     * @return 配置信息，若不存在则返回空配置
     */
    ModelConfig getModelConfig(const QString& modelId) const;

    /**
     * @brief 更新模型配置（运行时修改）
     * @param modelId 模型 ID
     * @param config 新的配置
     */
    void updateModelConfig(const QString& modelId, const ModelConfig& config);

    /**
     * @brief 检查模型配置是否存在
     */
    bool hasModelConfig(const QString& modelId) const;

    // ========== Provider 创建接口 ==========

    /**
     * @brief 注册 Provider 工厂函数（高级用法）
     * @param modelId 模型 ID（如 "deepseek-chat", "gpt-4o"）
     * @param factory 工厂函数，每次调用返回新的 Provider 实例
     */
    void registerProviderFactory(const QString& modelId, ProviderFactory factory);

    /**
     * @brief 为指定模型创建新的 Provider 实例
     * @param modelId 模型 ID
     * @param parent Provider 的父对象（通常是 LLMAgent），负责管理其生命周期
     * @return 新创建的 Provider 实例，若模型未注册则返回 nullptr
     */
    LLMProvider* createProvider(const QString& modelId, QObject* parent = nullptr);

    /**
     * @brief 检查模型是否已注册
     */
    bool hasModel(const QString& modelId) const;

    /**
     * @brief 注销指定模型的工厂函数
     */
    void unregisterModel(const QString& modelId);

    /**
     * @brief 已注册的 model_id 列表
     */
    QStringList registeredModelIds() const;

signals:
    /**
     * @brief 模型配置更新信号
     * @param modelId 被更新的模型 ID
     */
    void modelConfigUpdated(const QString& modelId);

private:
    explicit ModelFactory(QObject* parent = nullptr);
    ~ModelFactory() override;
    ModelFactory(const ModelFactory&) = delete;
    ModelFactory& operator=(const ModelFactory&) = delete;

    /// model_id → Provider 工厂函数
    QMap<QString, ProviderFactory> m_providerFactories;
    
    /// model_id → 模型配置
    QMap<QString, ModelConfig> m_modelConfigs;
};

#endif // MODELFACTORY_H
