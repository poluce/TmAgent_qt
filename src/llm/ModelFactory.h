#ifndef MODELFACTORY_H
#define MODELFACTORY_H

#include "LLMProvider.h"
#include "LLMTypes.h"
#include <QList>
#include <QMap>
#include <QObject>
#include <functional>

/**
 * @brief 模型工厂，动态创建 LLMProvider 实例
 *
 * 管理 Provider 工厂函数，为每个 Agent 创建独立的 LLMProvider 实例。
 * Provider 的生命周期由调用方管理。
 * 支持新路径（ProviderInstance + modelId）和旧路径（configId）。
 */
class ModelFactory : public QObject {
    Q_OBJECT
public:
    using ProviderFactory = std::function<LLMProvider*(QObject* parent)>;

    static ModelFactory* instance();

    /**
     * @brief 从 LLMConfig 解析 configId（旧路径兼容键）
     */
    static QString resolveConfigKey(const LLMConfig& llmConfig);

    /**
     * @brief 从 LLMConfig 解析接入点实例 ID（优先 providerInstanceId，回退 configId）
     */
    static QString resolveInstanceId(const LLMConfig& llmConfig);

    /**
     * @brief 从 LLMConfig 解析真实模型 ID（优先 selectedModelId，回退从旧 ModelConfig 查 modelId）
     */
    QString resolveModelId(const LLMConfig& llmConfig) const;

    // ========== 接入点实例管理（新路径）==========
    void registerProviderInstance(const ProviderInstanceConfig& config);
    ProviderInstanceConfig getProviderInstance(const QString& instanceId) const;
    bool hasProviderInstance(const QString& instanceId) const;
    QStringList registeredInstanceIds() const;
    QStringList enabledInstanceIds() const;
    QString displayNameForInstance(const QString& instanceId) const;

    // ========== 模型缓存 ==========
    QList<AvailableModel> cachedModels(const QString& instanceId) const;
    void setCachedModels(const QString& instanceId, const QList<AvailableModel>& models);
    void clearModelCache(const QString& instanceId);

    /**
     * @brief 异步拉取指定接入点的模型列表，结果写入缓存并发射 modelCacheUpdated
     */
    void fetchModelsAsync(const QString& instanceId);

    // ========== Provider 创建（新路径：instanceId + modelId）==========
    LLMProvider* createProvider(const QString& instanceId, const QString& modelId, QObject* parent = nullptr);

    // ========== 旧 API（兼容，内部转发到新 API）==========
    void registerModelConfig(const ModelConfig& config);
    ModelConfig getModelConfig(const QString& configId) const;
    void updateModelConfig(const QString& configId, const ModelConfig& config);
    bool hasModelConfig(const QString& configId) const;
    QStringList enabledConfigIds() const;
    QString displayNameForConfig(const QString& configId) const;

    void registerProviderFactory(const QString& configId, ProviderFactory factory);
    LLMProvider* createProvider(const QString& configId, QObject* parent = nullptr);
    bool hasModel(const QString& configId) const;
    void unregisterModel(const QString& configId);
    QStringList registeredModelIds() const;
    QStringList registeredConfigIds() const { return registeredModelIds(); }

signals:
    void modelConfigUpdated(const QString& configId);
    void modelCacheUpdated(const QString& instanceId);

private:
    explicit ModelFactory(QObject* parent = nullptr);
    ~ModelFactory() override;
    ModelFactory(const ModelFactory&) = delete;
    ModelFactory& operator=(const ModelFactory&) = delete;

    QMap<QString, ProviderFactory> m_providerFactories;
    QMap<QString, ModelConfig> m_modelConfigs;

    // 新路径数据
    QMap<QString, ProviderInstanceConfig> m_providerInstances;
    QMap<QString, QList<AvailableModel>> m_modelCache;
};

#endif // MODELFACTORY_H
