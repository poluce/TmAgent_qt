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
 * 管理 Provider 工厂函数，为每个 Agent 创建独立的 LLMProvider 实例。
 * Provider 的生命周期由调用方管理。
 */
class ModelFactory : public QObject {
    Q_OBJECT
public:
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

    // ========== 配置管理 ==========
    void registerModelConfig(const ModelConfig& config);
    ModelConfig getModelConfig(const QString& modelId) const;
    void updateModelConfig(const QString& modelId, const ModelConfig& config);
    bool hasModelConfig(const QString& modelId) const;

    // ========== Provider 创建 ==========
    void registerProviderFactory(const QString& modelId, ProviderFactory factory);
    LLMProvider* createProvider(const QString& modelId, QObject* parent = nullptr);
    bool hasModel(const QString& modelId) const;
    void unregisterModel(const QString& modelId);
    QStringList registeredModelIds() const;

signals:
    void modelConfigUpdated(const QString& modelId);

private:
    explicit ModelFactory(QObject* parent = nullptr);
    ~ModelFactory() override;
    ModelFactory(const ModelFactory&) = delete;
    ModelFactory& operator=(const ModelFactory&) = delete;

    QMap<QString, ProviderFactory> m_providerFactories;
    QMap<QString, ModelConfig> m_modelConfigs;
};

#endif // MODELFACTORY_H
