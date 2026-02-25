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

// ========== Schema 版本检测 ==========

int ModelConfigLoader::detectSchemaVersion(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 1;

    YAML::Node root;
    try {
        root = YAML::Load(QString::fromUtf8(file.readAll()).toStdString());
    } catch (const YAML::Exception&) {
        return 1;
    }

    return nodeToInt(root["schema_version"], 1);
}

// ========== 新格式（schema_version 2）：Provider 接入点 ==========

QVector<ProviderInstanceConfig> ModelConfigLoader::loadProviderInstances(const QString& filePath, bool resolveEnv)
{
    QVector<ProviderInstanceConfig> instances;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "无法打开配置文件:" << filePath;
        return instances;
    }

    YAML::Node root;
    try {
        root = YAML::Load(QString::fromUtf8(file.readAll()).toStdString());
    } catch (const YAML::Exception& e) {
        qWarning() << "配置解析失败:" << filePath << e.what();
        return instances;
    }

    YAML::Node providersNode = root["providers"];
    if (!providersNode || !providersNode.IsSequence())
        return instances;

    for (const YAML::Node& node : providersNode) {
        if (!node || !node.IsMap())
            continue;

        ProviderInstanceConfig inst;
        inst.instanceId = nodeToString(findNode(node, "instance_id"));
        inst.displayName = nodeToString(findNode(node, "display_name"));
        inst.providerType = nodeToString(findNode(node, "provider_type"));
        inst.baseUrl = nodeToString(findNode(node, "base_url"));
        inst.apiKey = nodeToString(findNode(node, "api_key"));
        QString authType = nodeToString(findNode(node, "auth_type"));
        inst.authType = authType.isEmpty() ? QStringLiteral("Bearer") : authType;
        inst.enabled = nodeToBool(findNode(node, "enabled"), true);
        inst.defaultTemperature = nodeToDouble(findNode(node, "default_temperature"), 0.7);
        inst.defaultMaxTokens = nodeToInt(findNode(node, "default_max_tokens"), 4096);
        inst.defaultTimeoutMs = nodeToInt(findNode(node, "default_timeout_ms"), 180000);
        inst.toolCalling = nodeToBool(findNode(node, "tool_calling"), false);
        inst.contextLength = nodeToInt(findNode(node, "context_length"), 0);

        YAML::Node capsNode = findNode(node, "capabilities");
        if (capsNode && capsNode.IsSequence()) {
            for (const YAML::Node& cap : capsNode) {
                QString capStr = nodeToString(cap);
                if (!capStr.isEmpty())
                    inst.capabilities.append(capStr);
            }
        }

        if (resolveEnv) {
            inst.apiKey = resolveApiKeyFromEnv(inst.apiKey, inst.providerType);
        }

        if (inst.isValid() && !inst.instanceId.trimmed().isEmpty()) {
            instances.append(inst);
        } else {
            qWarning() << "接入点配置缺失必填字段，已跳过一条。";
        }
    }

    return instances;
}

bool ModelConfigLoader::saveProviderInstances(const QString& filePath,
                                              const QVector<ProviderInstanceConfig>& instances,
                                              const QString& defaultProvider,
                                              const QString& defaultModel)
{
    YAML::Emitter out;
    out.SetIndent(2);
    out.SetMapFormat(YAML::Block);
    out.SetSeqFormat(YAML::Block);
    out << YAML::BeginMap;
    out << YAML::Key << "schema_version" << YAML::Value << 2;
    out << YAML::Key << "providers" << YAML::Value;
    out << YAML::BeginSeq;
    for (const ProviderInstanceConfig& inst : instances) {
        out << YAML::BeginMap;
        out << YAML::Key << "instance_id" << YAML::Value << inst.instanceId.toStdString();
        out << YAML::Key << "display_name" << YAML::Value << inst.displayName.toStdString();
        out << YAML::Key << "provider_type" << YAML::Value << inst.providerType.toStdString();
        out << YAML::Key << "base_url" << YAML::Value << inst.baseUrl.toStdString();
        out << YAML::Key << "api_key" << YAML::Value << inst.apiKey.toStdString();
        out << YAML::Key << "auth_type" << YAML::Value << inst.authType.toStdString();
        out << YAML::Key << "enabled" << YAML::Value << inst.enabled;
        out << YAML::Key << "default_temperature" << YAML::Value << inst.defaultTemperature;
        out << YAML::Key << "default_max_tokens" << YAML::Value << inst.defaultMaxTokens;
        out << YAML::Key << "default_timeout_ms" << YAML::Value << inst.defaultTimeoutMs;
        out << YAML::Key << "tool_calling" << YAML::Value << inst.toolCalling;
        out << YAML::Key << "context_length" << YAML::Value << inst.contextLength;
        if (!inst.capabilities.isEmpty()) {
            out << YAML::Key << "capabilities" << YAML::Value;
            out << YAML::BeginSeq;
            for (const QString& cap : inst.capabilities)
                out << cap.toStdString();
            out << YAML::EndSeq;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::Key << "default_provider" << YAML::Value
        << (defaultProvider.isEmpty() ? std::string() : defaultProvider.toStdString());
    out << YAML::Key << "default_model" << YAML::Value
        << (defaultModel.isEmpty() ? std::string() : defaultModel.toStdString());
    out << YAML::EndMap;

    QString yamlContent = QStringLiteral("# 接入点配置文件 (schema v2)\n# 使用「从厂商导入」按钮添加接入点\n\n");
    yamlContent += QString::fromStdString(out.c_str());
    yamlContent += "\n";

    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "无法写入配置文件:" << filePath;
        return false;
    }
    file.write(yamlContent.toUtf8());

    qInfo() << "已保存接入点配置到:" << filePath;
    return true;
}

bool ModelConfigLoader::addOrUpdateProviderInstance(const QString& filePath, const ProviderInstanceConfig& instance)
{
    QVector<ProviderInstanceConfig> instances = loadProviderInstances(filePath);
    QString defaultProvider = getDefaultProvider(filePath);
    QString defaultModel = getDefaultModel(filePath);

    bool found = false;
    for (int i = 0; i < instances.size(); ++i) {
        if (instances[i].instanceId == instance.instanceId) {
            instances[i] = instance;
            found = true;
            break;
        }
    }
    if (!found)
        instances.append(instance);

    return saveProviderInstances(filePath, instances, defaultProvider, defaultModel);
}

bool ModelConfigLoader::removeProviderInstance(const QString& filePath, const QString& instanceId)
{
    QVector<ProviderInstanceConfig> instances = loadProviderInstances(filePath);
    QString defaultProvider = getDefaultProvider(filePath);
    QString defaultModel = getDefaultModel(filePath);

    for (int i = 0; i < instances.size(); ++i) {
        if (instances[i].instanceId == instanceId) {
            instances.removeAt(i);
            break;
        }
    }
    if (defaultProvider == instanceId) {
        defaultProvider.clear();
        defaultModel.clear();
    }

    return saveProviderInstances(filePath, instances, defaultProvider, defaultModel);
}

ProviderInstanceConfig ModelConfigLoader::getProviderInstance(const QString& filePath, const QString& instanceId, bool resolveEnv)
{
    QVector<ProviderInstanceConfig> instances = loadProviderInstances(filePath, resolveEnv);
    for (const ProviderInstanceConfig& inst : instances) {
        if (inst.instanceId == instanceId)
            return inst;
    }
    return ProviderInstanceConfig();
}

QString ModelConfigLoader::getDefaultProvider(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    YAML::Node root;
    try {
        root = YAML::Load(QString::fromUtf8(file.readAll()).toStdString());
    } catch (const YAML::Exception&) {
        return QString();
    }

    // V2 格式
    QString val = nodeToString(root["default_provider"]);
    if (!val.isEmpty())
        return val;
    // V1 兼容
    return nodeToString(root["default"]);
}

QString ModelConfigLoader::getDefaultModel(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    YAML::Node root;
    try {
        root = YAML::Load(QString::fromUtf8(file.readAll()).toStdString());
    } catch (const YAML::Exception&) {
        return QString();
    }

    return nodeToString(root["default_model"]);
}

bool ModelConfigLoader::setDefaultProvider(const QString& filePath, const QString& instanceId, const QString& modelId)
{
    QVector<ProviderInstanceConfig> instances = loadProviderInstances(filePath);
    QString defaultModel = modelId.isEmpty() ? getDefaultModel(filePath) : modelId;
    return saveProviderInstances(filePath, instances, instanceId, defaultModel);
}

bool ModelConfigLoader::setProviderEnabled(const QString& filePath, const QString& instanceId, bool enabled)
{
    QVector<ProviderInstanceConfig> instances = loadProviderInstances(filePath);
    QString defaultProvider = getDefaultProvider(filePath);
    QString defaultModel = getDefaultModel(filePath);

    for (int i = 0; i < instances.size(); ++i) {
        if (instances[i].instanceId == instanceId) {
            instances[i].enabled = enabled;
            break;
        }
    }

    return saveProviderInstances(filePath, instances, defaultProvider, defaultModel);
}

// ========== 迁移 ==========

QVector<ProviderInstanceConfig> ModelConfigLoader::migrateFromV1(const QVector<ModelConfig>& oldModels)
{
    QVector<ProviderInstanceConfig> instances;
    for (const ModelConfig& mc : oldModels) {
        ProviderInstanceConfig inst;
        inst.instanceId = mc.configId;
        inst.enabled = mc.enabled;
        inst.displayName = mc.displayName;
        inst.providerType = mc.provider;
        inst.baseUrl = mc.baseUrl;
        inst.apiKey = mc.apiKey;
        inst.authType = mc.authType;
        inst.defaultTemperature = mc.temperature;
        inst.defaultMaxTokens = mc.maxTokens;
        inst.defaultTimeoutMs = mc.timeoutMs;
        inst.capabilities = mc.capabilities;
        inst.toolCalling = mc.toolCalling;
        inst.contextLength = mc.contextLength;
        inst.extraConfig = mc.extraConfig;

        if (inst.isValid())
            instances.append(inst);
    }
    return instances;
}
