#ifndef MODELCONFIGLOADER_H
#define MODELCONFIGLOADER_H

#include "newCore/LLMTypes.h"
#include <QString>
#include <QVector>

/**
 * @brief YAML 模型配置加载器
 *
 * 负责从 models.yaml 文件加载和保存模型配置。
 * 支持多模型管理、默认模型设置等功能。
 * 以 configId 作为配置条目唯一键（与 modelId 解耦）。
 */
class ModelConfigLoader {
public:
    /**
     * @brief 从 YAML 文件加载所有模型配置
     * @param filePath YAML 文件路径
     * @return 模型配置列表
     */
    static QVector<ModelConfig> loadFromFile(const QString& filePath, bool resolveEnv = false);

    /**
     * @brief 保存所有模型配置到 YAML 文件
     * @param filePath YAML 文件路径
     * @param models 模型配置列表
     * @param defaultConfigId 默认配置 ID（configId）
     * @return 是否保存成功
     */
    static bool saveToFile(const QString& filePath, const QVector<ModelConfig>& models, const QString& defaultConfigId);

    /**
     * @brief 添加或更新单个模型配置（以 configId 为键）
     * @param filePath YAML 文件路径
     * @param config 模型配置
     * @return 是否操作成功
     */
    static bool addOrUpdateModel(const QString& filePath, const ModelConfig& config);

    /**
     * @brief 删除指定模型配置
     * @param filePath YAML 文件路径
     * @param configId 配置 ID
     * @return 是否删除成功
     */
    static bool removeModel(const QString& filePath, const QString& configId);

    /**
     * @brief 获取默认配置 ID
     * @param filePath YAML 文件路径
     * @return 默认配置 ID（configId）
     */
    static QString getDefaultConfigId(const QString& filePath);

    /**
     * @brief 设置默认配置 ID
     * @param filePath YAML 文件路径
     * @param configId 配置 ID
     * @return 是否设置成功
     */
    static bool setDefaultConfigId(const QString& filePath, const QString& configId);

    /**
     * @brief 设置指定配置的启用状态
     * @param filePath YAML 文件路径
     * @param configId 配置 ID
     * @param enabled 是否启用
     * @return 是否设置成功
     */
    static bool setModelEnabled(const QString& filePath, const QString& configId, bool enabled);

    /**
     * @brief 获取指定配置（以 configId 查找）
     * @param filePath YAML 文件路径
     * @param configId 配置 ID
     * @return 模型配置（如果不存在则返回空配置）
     */
    static ModelConfig getModelConfig(const QString& filePath, const QString& configId, bool resolveEnv = false);

private:
    Q_DISABLE_COPY(ModelConfigLoader)
};

#endif // MODELCONFIGLOADER_H
