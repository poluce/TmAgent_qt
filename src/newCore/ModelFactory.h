#ifndef MODELFACTORY_H
#define MODELFACTORY_H

#include "LLMProvider.h"
#include "LLMTypes.h"
#include "ModelId.h"
#include <QMap>
#include <QObject>
#include <functional>

struct LLMConfig; // forward declaration from ToolTypes.h

/**
 * @brief 模型工厂，动态创建 LLMProvider 实例
 *
 * 管理 Provider 工厂函数，为每个 Agent 创建独立的 LLMProvider 实例。
 * Provider 的生命周期由调用方管理。
 * 内部以 configId 作为唯一键。
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

    /**
     * @brief 从 LLMConfig 解析出 configId 或回退到旧的 modelKey
     *
     * 优先使用 configId；为空时回退到 resolveModelKey(model, customModelId)。
     */
    static QString resolveConfigKey(const LLMConfig& llmConfig);

    // ========== 配置管理（以 configId 为键）==========
    void registerModelConfig(const ModelConfig& config);
    ModelConfig getModelConfig(const QString& configId) const;
    void updateModelConfig(const QString& configId, const ModelConfig& config);
    bool hasModelConfig(const QString& configId) const;

    /**
     * @brief 返回所有已启用的 configId 列表
     */
    QStringList enabledConfigIds() const;

    /**
     * @brief 返回指定 configId 的 displayName
     */
    QString displayNameForConfig(const QString& configId) const;

    // ========== Provider 创建 ==========
    void registerProviderFactory(const QString& configId, ProviderFactory factory);
    LLMProvider* createProvider(const QString& configId, QObject* parent = nullptr);
    bool hasModel(const QString& configId) const;
    void unregisterModel(const QString& configId);
    QStringList registeredModelIds() const;
    QStringList registeredConfigIds() const { return registeredModelIds(); }

signals:
    void modelConfigUpdated(const QString& configId);

private:
    explicit ModelFactory(QObject* parent = nullptr);
    ~ModelFactory() override;
    ModelFactory(const ModelFactory&) = delete;
    ModelFactory& operator=(const ModelFactory&) = delete;

    QMap<QString, ProviderFactory> m_providerFactories;
    QMap<QString, ModelConfig> m_modelConfigs;
};

#endif // MODELFACTORY_H
