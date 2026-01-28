#ifndef MODELCONFIGLOADER_H
#define MODELCONFIGLOADER_H

#include <QString>
#include <QVector>
#include "newCore/LLMTypes.h"

/**
 * @brief YAML 模型配置加载器
 * 
 * 负责从 models.yaml 文件加载和保存模型配置。
 * 支持多模型管理、默认模型设置等功能。
 */
class ModelConfigLoader
{
public:
    /**
     * @brief 从 YAML 文件加载所有模型配置
     * @param filePath YAML 文件路径
     * @return 模型配置列表
     */
    static QVector<ModelConfig> loadFromFile(const QString& filePath);
    
    /**
     * @brief 保存所有模型配置到 YAML 文件
     * @param filePath YAML 文件路径
     * @param models 模型配置列表
     * @param defaultModelId 默认模型 ID
     * @return 是否保存成功
     */
    static bool saveToFile(const QString& filePath, 
                          const QVector<ModelConfig>& models, 
                          const QString& defaultModelId);
    
    /**
     * @brief 添加或更新单个模型配置
     * @param filePath YAML 文件路径
     * @param config 模型配置
     * @return 是否操作成功
     */
    static bool addOrUpdateModel(const QString& filePath, const ModelConfig& config);
    
    /**
     * @brief 删除指定模型配置
     * @param filePath YAML 文件路径
     * @param modelId 模型 ID
     * @return 是否删除成功
     */
    static bool removeModel(const QString& filePath, const QString& modelId);
    
    /**
     * @brief 获取默认模型 ID
     * @param filePath YAML 文件路径
     * @return 默认模型 ID
     */
    static QString getDefaultModelId(const QString& filePath);
    
    /**
     * @brief 设置默认模型 ID
     * @param filePath YAML 文件路径
     * @param modelId 模型 ID
     * @return 是否设置成功
     */
    static bool setDefaultModelId(const QString& filePath, const QString& modelId);
    
    /**
     * @brief 获取指定模型的配置
     * @param filePath YAML 文件路径
     * @param modelId 模型 ID
     * @return 模型配置（如果不存在则返回空配置）
     */
    static ModelConfig getModelConfig(const QString& filePath, const QString& modelId);

private:
    /**
     * @brief 解析 YAML 内容为 JSON 对象
     * @param yamlContent YAML 文本内容
     * @return JSON 对象
     */
    static QJsonObject parseYamlToJson(const QString& yamlContent);
    
    /**
     * @brief 将 JSON 对象转换为 YAML 文本
     * @param json JSON 对象
     * @return YAML 文本内容
     */
    static QString convertJsonToYaml(const QJsonObject& json);
};

#endif // MODELCONFIGLOADER_H
