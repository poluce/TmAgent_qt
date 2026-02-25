#ifndef MODELCONFIGLOADER_H
#define MODELCONFIGLOADER_H

#include "llm/LLMTypes.h"
#include <QString>
#include <QVector>

/**
 * @brief YAML 模型配置加载器
 *
 * 负责从 models.yaml 文件加载和保存模型配置。
 * 支持 schema_version 1（旧格式 models[]）和 2（新格式 providers[]）。
 * 以 configId / instanceId 作为配置条目唯一键。
 */
class ModelConfigLoader {
public:
    // ========== Schema 版本检测 ==========
    static int detectSchemaVersion(const QString& filePath);

    // ========== 新格式（schema_version 2）：Provider 接入点 ==========
    static QVector<ProviderInstanceConfig> loadProviderInstances(const QString& filePath, bool resolveEnv = false);
    static bool saveProviderInstances(const QString& filePath,
                                      const QVector<ProviderInstanceConfig>& instances,
                                      const QString& defaultProvider,
                                      const QString& defaultModel);
    static bool addOrUpdateProviderInstance(const QString& filePath, const ProviderInstanceConfig& instance);
    static bool removeProviderInstance(const QString& filePath, const QString& instanceId);
    static ProviderInstanceConfig getProviderInstance(const QString& filePath, const QString& instanceId, bool resolveEnv = false);

    static QString getDefaultProvider(const QString& filePath);
    static QString getDefaultModel(const QString& filePath);
    static bool setDefaultProvider(const QString& filePath, const QString& instanceId, const QString& modelId = QString());
    static bool setProviderEnabled(const QString& filePath, const QString& instanceId, bool enabled);

    // ========== 迁移 ==========
    static QVector<ProviderInstanceConfig> migrateFromV1(const QVector<ModelConfig>& oldModels);

    // ========== 旧格式（schema_version 1）：ModelConfig ==========
    static QVector<ModelConfig> loadFromFile(const QString& filePath, bool resolveEnv = false);
    static bool saveToFile(const QString& filePath, const QVector<ModelConfig>& models, const QString& defaultConfigId);
    static bool addOrUpdateModel(const QString& filePath, const ModelConfig& config);
    static bool removeModel(const QString& filePath, const QString& configId);
    static QString getDefaultConfigId(const QString& filePath);
    static bool setDefaultConfigId(const QString& filePath, const QString& configId);
    static bool setModelEnabled(const QString& filePath, const QString& configId, bool enabled);
    static ModelConfig getModelConfig(const QString& filePath, const QString& configId, bool resolveEnv = false);

private:
    Q_DISABLE_COPY(ModelConfigLoader)
};

#endif // MODELCONFIGLOADER_H
