#include "ModelConfigLoader.h"
#include "KeychainHelper.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStringList>
#include <yaml-cpp/yaml.h>

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
        QStringList providerTags;
        providerTags << providerTag;
        if (providerTag == QStringLiteral("CLAUDE")
            || providerTag == QStringLiteral("CLAUDEAI")) {
            providerTags << QStringLiteral("ANTHROPIC");
        } else if (providerTag == QStringLiteral("ANTHROPIC")) {
            providerTags << QStringLiteral("CLAUDE");
        }

        for (const QString& tag : providerTags) {
            const QString providerKey = env.value(QStringLiteral("TMAGENT_%1_API_KEY").arg(tag));
            if (!providerKey.isEmpty())
                return providerKey;
        }
    }
    return env.value(QStringLiteral("TMAGENT_API_KEY"));
}

QString nodeToString(const YAML::Node& node)
{
    if (!node || node.IsNull())
        return QString();
    try {
        return QString::fromStdString(node.as<std::string>());
    } catch (const YAML::Exception&) {
        return QString();
    }
}

bool nodeToBool(const YAML::Node& node, bool fallback)
{
    if (!node || node.IsNull())
        return fallback;
    try {
        return node.as<bool>();
    } catch (const YAML::Exception&) {
        return fallback;
    }
}

int nodeToInt(const YAML::Node& node, int fallback)
{
    if (!node || node.IsNull())
        return fallback;
    try {
        return node.as<int>();
    } catch (const YAML::Exception&) {
        return fallback;
    }
}

double nodeToDouble(const YAML::Node& node, double fallback)
{
    if (!node || node.IsNull())
        return fallback;
    try {
        return node.as<double>();
    } catch (const YAML::Exception&) {
        return fallback;
    }
}

YAML::Node findNode(const YAML::Node& node, const char* primary, const char* fallback = nullptr)
{
    if (!node || !node.IsMap())
        return YAML::Node();
    YAML::Node val = node[primary];
    if (val)
        return val;
    if (fallback)
        return node[fallback];
    return YAML::Node();
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

    YAML::Node root;
    try {
        root = YAML::Load(yamlContent.toStdString());
    } catch (const YAML::Exception& e) {
        qWarning() << "模型配置解析失败:" << filePath << e.what();
        return models;
    }

    YAML::Node modelsNode = root["models"];
    if (!modelsNode || !modelsNode.IsSequence())
        return models;

    for (const YAML::Node& node : modelsNode) {
        if (!node || !node.IsMap())
            continue;
        ModelConfig config;
        QString modelId = nodeToString(findNode(node, "id"));
        config.modelId = modelId;

        QString configId = nodeToString(findNode(node, "config_id"));
        config.configId = configId;

        config.enabled = nodeToBool(findNode(node, "enabled"), true);

        config.displayName = nodeToString(findNode(node, "display_name"));
        config.provider = nodeToString(findNode(node, "provider"));
        config.apiKey = nodeToString(findNode(node, "api_key"));
        config.baseUrl = nodeToString(findNode(node, "base_url"));
        QString authType = nodeToString(findNode(node, "auth_type"));
        config.authType = authType.isEmpty() ? QStringLiteral("Bearer") : authType;
        config.temperature = nodeToDouble(findNode(node, "temperature"), 0.7);
        config.maxTokens = nodeToInt(findNode(node, "max_tokens"), 4096);
        config.timeoutMs = nodeToInt(findNode(node, "timeout_ms"), 180000);
        config.contextLength = nodeToInt(findNode(node, "context_length"), 0);
        config.toolCalling = nodeToBool(findNode(node, "tool_calling"), false);
        config.systemPrompt = nodeToString(findNode(node, "system_prompt"));

        YAML::Node capsNode = findNode(node, "capabilities");
        if (capsNode && capsNode.IsSequence()) {
            for (const YAML::Node& cap : capsNode) {
                QString capStr = nodeToString(cap);
                if (!capStr.isEmpty())
                    config.capabilities.append(capStr);
            }
        }

        if (resolveEnv) {
            config.apiKey = resolveApiKeyFromEnv(config.apiKey, config.provider);
        }
        if (config.isValid() && !config.configId.trimmed().isEmpty()) {
            models.append(config);
        } else {
            qWarning() << "模型配置缺失必填字段(config_id/id/provider/display_name)，已跳过一条。";
        }
    }

    return models;
}

bool ModelConfigLoader::saveToFile(const QString& filePath, const QVector<ModelConfig>& models, const QString& defaultConfigId)
{
    YAML::Emitter out;
    out.SetIndent(2);
    out.SetMapFormat(YAML::Block);
    out.SetSeqFormat(YAML::Block);
    out << YAML::BeginMap;
    out << YAML::Key << "models" << YAML::Value;
    out << YAML::BeginSeq;
    for (const ModelConfig& config : models) {
        out << YAML::BeginMap;
        out << YAML::Key << "config_id" << YAML::Value << config.configId.toStdString();
        out << YAML::Key << "id" << YAML::Value << config.modelId.toStdString();
        out << YAML::Key << "display_name" << YAML::Value << config.displayName.toStdString();
        out << YAML::Key << "provider" << YAML::Value << config.provider.toStdString();
        out << YAML::Key << "api_key" << YAML::Value << config.apiKey.toStdString();
        out << YAML::Key << "base_url" << YAML::Value << config.baseUrl.toStdString();
        out << YAML::Key << "auth_type" << YAML::Value << config.authType.toStdString();
        out << YAML::Key << "enabled" << YAML::Value << config.enabled;
        out << YAML::Key << "temperature" << YAML::Value << config.temperature;
        out << YAML::Key << "max_tokens" << YAML::Value << config.maxTokens;
        out << YAML::Key << "timeout_ms" << YAML::Value << config.timeoutMs;
        out << YAML::Key << "capabilities" << YAML::Value;
        out << YAML::BeginSeq;
        for (const QString& cap : config.capabilities) {
            out << cap.toStdString();
        }
        out << YAML::EndSeq;
        out << YAML::Key << "tool_calling" << YAML::Value << config.toolCalling;
        out << YAML::Key << "context_length" << YAML::Value << config.contextLength;
        out << YAML::Key << "system_prompt" << YAML::Value;
        if (config.systemPrompt.isEmpty()) {
            out << "";
        } else {
            out << YAML::Literal << config.systemPrompt.toStdString();
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::Key << "default" << YAML::Value
        << (defaultConfigId.isEmpty() ? std::string() : defaultConfigId.toStdString());
    out << YAML::EndMap;

    QString yamlContent = QStringLiteral("# 模型配置文件\n# 使用「从厂商导入」按钮添加模型\n\n");
    yamlContent += QString::fromStdString(out.c_str());
    yamlContent += "\n";

    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "无法写入模型配置文件:" << filePath;
        return false;
    }
    file.write(yamlContent.toUtf8());

    qInfo() << "已保存模型配置到:" << filePath;
    return true;
}

bool ModelConfigLoader::addOrUpdateModel(const QString& filePath, const ModelConfig& config)
{
    QVector<ModelConfig> models = loadFromFile(filePath);
    QString defaultId = getDefaultConfigId(filePath);

    bool found = false;
    for (int i = 0; i < models.size(); ++i) {
        if (models[i].configId == config.configId) {
            models[i] = config;
            found = true;
            break;
        }
    }
    if (!found)
        models.append(config);

    return saveToFile(filePath, models, defaultId);
}

bool ModelConfigLoader::removeModel(const QString& filePath, const QString& configId)
{
    QVector<ModelConfig> models = loadFromFile(filePath);
    QString defaultId = getDefaultConfigId(filePath);

    for (int i = 0; i < models.size(); ++i) {
        if (models[i].configId == configId) {
            models.removeAt(i);
            break;
        }
    }
    if (defaultId == configId)
        defaultId.clear();

    return saveToFile(filePath, models, defaultId);
}

QString ModelConfigLoader::getDefaultConfigId(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QString yamlContent = QString::fromUtf8(file.readAll());

    YAML::Node root;
    try {
        root = YAML::Load(yamlContent.toStdString());
    } catch (const YAML::Exception&) {
        return QString();
    }

    QString defaultVal = nodeToString(root["default"]);
    if (defaultVal.isEmpty())
        return defaultVal;

    return defaultVal;
}

bool ModelConfigLoader::setDefaultConfigId(const QString& filePath, const QString& configId)
{
    QVector<ModelConfig> models = loadFromFile(filePath);
    return saveToFile(filePath, models, configId);
}

bool ModelConfigLoader::setModelEnabled(const QString& filePath, const QString& configId, bool enabled)
{
    QVector<ModelConfig> models = loadFromFile(filePath);
    QString defaultId = getDefaultConfigId(filePath);

    for (int i = 0; i < models.size(); ++i) {
        if (models[i].configId == configId) {
            models[i].enabled = enabled;
            break;
        }
    }

    return saveToFile(filePath, models, defaultId);
}

ModelConfig ModelConfigLoader::getModelConfig(const QString& filePath, const QString& configId, bool resolveEnv)
{
    QVector<ModelConfig> models = loadFromFile(filePath, resolveEnv);

    // 优先按 configId 查找
    for (const ModelConfig& config : models) {
        if (config.configId == configId) {
            return config;
        }
    }

    return ModelConfig(); // 返回空配置
}
