#include "ModelConfigLoader.h"
#include "KeychainHelper.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

namespace {
bool extractEnvVarName(const QString& value, QString* varName)
{
    if (!varName)
        return false;
    const QString trimmed = value.trimmed();
    if (trimmed.startsWith(QStringLiteral("$ENV{")) && trimmed.endsWith('}')) {
        *varName = trimmed.mid(5, trimmed.size() - 6).trimmed();
        return !varName->isEmpty();
    }
    if (trimmed.startsWith(QStringLiteral("${")) && trimmed.endsWith('}')) {
        *varName = trimmed.mid(2, trimmed.size() - 3).trimmed();
        return !varName->isEmpty();
    }
    if (trimmed.startsWith('$') && trimmed.size() > 1 && !trimmed.contains(' ')) {
        *varName = trimmed.mid(1).trimmed();
        return !varName->isEmpty();
    }
    return false;
}

QString resolveApiKeyFromEnv(const QString& apiKey, const QString& provider)
{
    QString keychainId;
    if (KeychainHelper::parseKeyRef(apiKey, &keychainId)) {
        bool ok = false;
        QString error;
        QString secret = KeychainHelper::readPasswordSync(keychainId, &ok, &error);
        if (!ok) {
            qWarning() << "Keychain read failed:" << keychainId << error;
            return QString();
        }
        return secret;
    }
    QString explicitVar;
    if (extractEnvVarName(apiKey, &explicitVar)) {
        return QProcessEnvironment::systemEnvironment().value(explicitVar);
    }
    if (!apiKey.isEmpty()) {
        return apiKey;
    }
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString providerTag = provider.trimmed().toUpper();
    if (!providerTag.isEmpty()) {
        const QString providerKey = env.value(QStringLiteral("TMAGENT_%1_API_KEY").arg(providerTag));
        if (!providerKey.isEmpty())
            return providerKey;
    }
    return env.value(QStringLiteral("TMAGENT_API_KEY"));
}
} // namespace

QVector<ModelConfig> ModelConfigLoader::loadFromFile(const QString& filePath, bool resolveEnv)
{
    QVector<ModelConfig> models;
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "无法打开模型配置文件:" << filePath;
        return models;
    }
    
    QString yamlContent = QString::fromUtf8(file.readAll());
    file.close();
    
    // 解析 YAML 为 JSON
    QJsonObject root = parseYamlToJson(yamlContent);
    
    // 读取模型列表
    QJsonArray modelsArray = root["models"].toArray();
    for (const QJsonValue& value : modelsArray) {
        ModelConfig config = ModelConfig::fromJson(value.toObject());
        if (resolveEnv) {
            config.apiKey = resolveApiKeyFromEnv(config.apiKey, config.provider);
        }
        if (config.isValid()) {
            models.append(config);
        }
    }
    
    return models;
}

bool ModelConfigLoader::saveToFile(const QString& filePath, 
                                   const QVector<ModelConfig>& models, 
                                   const QString& defaultModelId)
{
    QJsonObject root;
    
    // 构建模型数组
    QJsonArray modelsArray;
    for (const ModelConfig& config : models) {
        modelsArray.append(config.toJson());
    }
    root["models"] = modelsArray;
    root["default"] = defaultModelId;
    
    // 转换为 YAML
    QString yamlContent = convertJsonToYaml(root);
    
    // 写入文件
    QFile file(filePath);
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "无法写入模型配置文件:" << filePath;
        return false;
    }
    
    file.write(yamlContent.toUtf8());
    file.close();
    
    qInfo() << "已保存模型配置到:" << filePath;
    return true;
}

bool ModelConfigLoader::addOrUpdateModel(const QString& filePath, const ModelConfig& config)
{
    // 加载现有配置
    QVector<ModelConfig> models = loadFromFile(filePath);
    QString defaultModelId = getDefaultModelId(filePath);
    
    // 查找是否存在同名模型
    bool found = false;
    for (int i = 0; i < models.size(); ++i) {
        if (models[i].modelId == config.modelId) {
            models[i] = config;
            found = true;
            break;
        }
    }
    
    // 如果不存在则添加
    if (!found) {
        models.append(config);
    }
    
    // 保存配置
    return saveToFile(filePath, models, defaultModelId);
}

bool ModelConfigLoader::removeModel(const QString& filePath, const QString& modelId)
{
    // 加载现有配置
    QVector<ModelConfig> models = loadFromFile(filePath);
    QString defaultModelId = getDefaultModelId(filePath);
    
    // 移除指定模型
    for (int i = 0; i < models.size(); ++i) {
        if (models[i].modelId == modelId) {
            models.removeAt(i);
            break;
        }
    }
    
    // 如果删除的是默认模型，清空默认模型
    if (defaultModelId == modelId) {
        defaultModelId.clear();
    }
    
    // 保存配置
    return saveToFile(filePath, models, defaultModelId);
}

QString ModelConfigLoader::getDefaultModelId(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    
    QString yamlContent = QString::fromUtf8(file.readAll());
    file.close();
    
    QJsonObject root = parseYamlToJson(yamlContent);
    return root["default"].toString();
}

bool ModelConfigLoader::setDefaultModelId(const QString& filePath, const QString& modelId)
{
    QVector<ModelConfig> models = loadFromFile(filePath);
    return saveToFile(filePath, models, modelId);
}

ModelConfig ModelConfigLoader::getModelConfig(const QString& filePath, const QString& modelId, bool resolveEnv)
{
    QVector<ModelConfig> models = loadFromFile(filePath, resolveEnv);
    
    for (const ModelConfig& config : models) {
        if (config.modelId == modelId) {
            return config;
        }
    }
    
    return ModelConfig();  // 返回空配置
}

QJsonObject ModelConfigLoader::parseYamlToJson(const QString& yamlContent)
{
    // 简化的 YAML 解析器（仅支持本项目使用的 YAML 格式）
    // 注意：这是一个简化实现，仅支持特定格式的 YAML
    
    QJsonObject root;
    QJsonArray modelsArray;
    QJsonObject currentModel;
    QStringList currentCapabilities;
    QString currentKey;
    bool inSystemPrompt = false;
    QString systemPromptContent;
    
    QStringList lines = yamlContent.split('\n');
    
    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        
        // 跳过注释和空行
        if (trimmed.startsWith('#') || trimmed.isEmpty()) {
            continue;
        }
        
        // 处理 default 字段
        if (trimmed.startsWith("default:")) {
            QString value = trimmed.mid(8).trimmed();
            if (value.startsWith('"') && value.endsWith('"')) {
                value = value.mid(1, value.length() - 2);
            }
            root["default"] = value;
            continue;
        }
        
        // 处理 models 数组开始
        if (trimmed.startsWith("models:")) {
            continue;
        }
        
        // 处理新模型开始（- id:）
        if (trimmed.startsWith("- id:") || trimmed.startsWith("- modelId:")) {
            // 保存上一个模型
            if (!currentModel.isEmpty()) {
                if (!currentCapabilities.isEmpty()) {
                    QJsonArray caps;
                    for (const QString& cap : currentCapabilities) {
                        caps.append(cap);
                    }
                    currentModel["capabilities"] = caps;
                    currentCapabilities.clear();
                }
                if (inSystemPrompt) {
                    currentModel["systemPrompt"] = systemPromptContent.trimmed();
                    systemPromptContent.clear();
                    inSystemPrompt = false;
                }
                modelsArray.append(currentModel);
                currentModel = QJsonObject();
            }
            
            // 解析 id
            QString value = trimmed.contains("id:") ? 
                           trimmed.mid(trimmed.indexOf("id:") + 3).trimmed() :
                           trimmed.mid(trimmed.indexOf("modelId:") + 8).trimmed();
            if (value.startsWith('"') && value.endsWith('"')) {
                value = value.mid(1, value.length() - 2);
            }
            currentModel["modelId"] = value;
            continue;
        }
        
        // 处理系统提示词（多行）
        if (inSystemPrompt) {
            if (line.startsWith("  ") && !trimmed.contains(':')) {
                // 继续收集系统提示词内容
                systemPromptContent += line.mid(4) + "\n";  // 移除缩进
                continue;
            } else {
                // 系统提示词结束
                currentModel["systemPrompt"] = systemPromptContent.trimmed();
                systemPromptContent.clear();
                inSystemPrompt = false;
            }
        }
        
        // 处理 system_prompt 开始
        if (trimmed.startsWith("system_prompt:") || trimmed.startsWith("systemPrompt:")) {
            QString value = trimmed.contains("system_prompt:") ?
                           trimmed.mid(14).trimmed() :
                           trimmed.mid(13).trimmed();
            
            if (value == "|") {
                // 多行模式
                inSystemPrompt = true;
                systemPromptContent.clear();
            } else {
                // 单行模式
                if (value.startsWith('"') && value.endsWith('"')) {
                    value = value.mid(1, value.length() - 2);
                }
                currentModel["systemPrompt"] = value;
            }
            continue;
        }
        
        // 处理 capabilities 数组
        if (trimmed.startsWith("capabilities:")) {
            currentCapabilities.clear();
            continue;
        }
        
        if (trimmed.startsWith("- ") && !trimmed.contains(':')) {
            // capabilities 数组项
            QString cap = trimmed.mid(2).trimmed();
            if (cap.startsWith('"') && cap.endsWith('"')) {
                cap = cap.mid(1, cap.length() - 2);
            }
            currentCapabilities.append(cap);
            continue;
        }
        
        // 处理其他字段
        if (trimmed.contains(':')) {
            int colonPos = trimmed.indexOf(':');
            QString key = trimmed.left(colonPos).trimmed();
            QString value = trimmed.mid(colonPos + 1).trimmed();
            
            // 移除引号
            if (value.startsWith('"') && value.endsWith('"')) {
                value = value.mid(1, value.length() - 2);
            }
            
            // 转换字段名（下划线转驼峰）
            if (key == "display_name") key = "displayName";
            else if (key == "api_key") key = "apiKey";
            else if (key == "base_url") key = "baseUrl";
            else if (key == "auth_type") key = "authType";
            else if (key == "max_tokens") key = "maxTokens";
            else if (key == "timeout_ms") key = "timeoutMs";
            else if (key == "tool_calling") key = "toolCalling";
            else if (key == "context_length") key = "contextLength";
            
            // 类型转换
            if (key == "temperature") {
                currentModel[key] = value.toDouble();
            } else if (key == "maxTokens" || key == "timeoutMs" || key == "contextLength") {
                currentModel[key] = value.toInt();
            } else if (key == "toolCalling") {
                currentModel[key] = (value == "true" || value == "True");
            } else {
                currentModel[key] = value;
            }
        }
    }
    
    // 保存最后一个模型
    if (!currentModel.isEmpty()) {
        if (!currentCapabilities.isEmpty()) {
            QJsonArray caps;
            for (const QString& cap : currentCapabilities) {
                caps.append(cap);
            }
            currentModel["capabilities"] = caps;
        }
        if (inSystemPrompt) {
            currentModel["systemPrompt"] = systemPromptContent.trimmed();
        }
        modelsArray.append(currentModel);
    }
    
    root["models"] = modelsArray;
    return root;
}

QString ModelConfigLoader::convertJsonToYaml(const QJsonObject& json)
{
    QString yaml;
    
    // 添加文件头注释
    yaml += "# 模型配置文件\n";
    yaml += "# 使用「从厂商导入」按钮添加模型\n\n";
    
    // 写入 models 数组
    yaml += "models:\n";
    
    QJsonArray modelsArray = json["models"].toArray();
    if (modelsArray.isEmpty()) {
        yaml += "  []\n";
    } else {
        for (const QJsonValue& value : modelsArray) {
            QJsonObject model = value.toObject();
            
            yaml += "  - id: " + model["modelId"].toString() + "\n";
            yaml += "    display_name: " + model["displayName"].toString() + "\n";
            yaml += "    provider: " + model["provider"].toString() + "\n";
            yaml += "    api_key: " + model["apiKey"].toString() + "\n";
            yaml += "    base_url: " + model["baseUrl"].toString() + "\n";
            yaml += "    auth_type: " + model["authType"].toString() + "\n";
            yaml += "    temperature: " + QString::number(model["temperature"].toDouble()) + "\n";
            yaml += "    max_tokens: " + QString::number(model["maxTokens"].toInt()) + "\n";
            yaml += "    timeout_ms: " + QString::number(model["timeoutMs"].toInt()) + "\n";
            
            // capabilities 数组
            yaml += "    capabilities:\n";
            QJsonArray caps = model["capabilities"].toArray();
            for (const QJsonValue& cap : caps) {
                yaml += "      - " + cap.toString() + "\n";
            }
            
            yaml += "    tool_calling: " + QString(model["toolCalling"].toBool() ? "true" : "false") + "\n";
            
            // system_prompt（多行）
            QString systemPrompt = model["systemPrompt"].toString();
            if (!systemPrompt.isEmpty()) {
                yaml += "    system_prompt: |\n";
                QStringList promptLines = systemPrompt.split('\n');
                for (const QString& line : promptLines) {
                    yaml += "      " + line + "\n";
                }
            }
            
            yaml += "\n";
        }
    }
    
    // 写入 default 字段
    QString defaultModel = json["default"].toString();
    yaml += "default: " + (defaultModel.isEmpty() ? "\"\"" : defaultModel) + "\n";
    
    return yaml;
}
