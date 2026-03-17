#include "ModelConfigDialog.h"

#include "core/utils/DefaultPrompts.h"
#include "core/utils/KeychainHelper.h"
#include "core/utils/ModelConfigLoader.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include "modelconfig/model_config_manager_page.h"
#include <QDialog>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcessEnvironment>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>

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

bool isEnvVarReference(const QString& value)
{
    QString dummy;
    return extractEnvVarName(value, &dummy);
}

QString canonicalProviderId(const QString& providerId)
{
    const QString id = providerId.trimmed().toLower();
    if (id == QStringLiteral("claude") || id == QStringLiteral("claudeai")
        || id == QStringLiteral("anthropic")) {
        return QStringLiteral("anthropic");
    }
    if (id == QStringLiteral("google"))
        return QStringLiteral("gemini");
    return id;
}

bool isAnthropicProviderId(const QString& providerId)
{
    return canonicalProviderId(providerId) == QStringLiteral("anthropic");
}

QString normalizeBaseUrl(const QString& baseUrl)
{
    QString url = baseUrl.trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

QString resolveApiKeyInputForTest(const QString& apiKeyInput)
{
    QString varName;
    if (extractEnvVarName(apiKeyInput, &varName))
        return QProcessEnvironment::systemEnvironment().value(varName);
    return apiKeyInput.trimmed();
}

QString buildModelsEndpoint(const QString& providerId, const QString& baseUrl)
{
    const QString root = normalizeBaseUrl(baseUrl);
    if (root.isEmpty())
        return QString();

    if (isAnthropicProviderId(providerId)) {
        if (root.endsWith(QStringLiteral("/v1/models"), Qt::CaseInsensitive))
            return root;
        if (root.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive))
            return root + QStringLiteral("/models");
        return root + QStringLiteral("/v1/models");
    }

    if (root.endsWith(QStringLiteral("/models"), Qt::CaseInsensitive))
        return root;
    return root + QStringLiteral("/models");
}

bool isHttpReachable(QNetworkReply* reply)
{
    if (!reply)
        return false;
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    return status.isValid() || reply->error() == QNetworkReply::NoError;
}

QString extractErrorMessage(const QByteArray& body, const QString& fallback)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fallback.trimmed();

    const QJsonObject root = doc.object();
    const QJsonObject errObj = root.value(QStringLiteral("error")).toObject();
    const QString errMsg = errObj.value(QStringLiteral("message")).toString().trimmed();
    if (!errMsg.isEmpty())
        return errMsg;

    const QString msg = root.value(QStringLiteral("message")).toString().trimmed();
    if (!msg.isEmpty())
        return msg;

    return fallback.trimmed();
}

QStringList parseModelIdsFromResponse(const QByteArray& body)
{
    QStringList modelIds;
    QSet<QString> seen;
    auto append = [&modelIds, &seen](const QString& candidate) {
        const QString id = candidate.trimmed();
        if (id.isEmpty() || seen.contains(id))
            return;
        seen.insert(id);
        modelIds.append(id);
    };

    auto parseArray = [&append](const QJsonArray& arr) {
        for (const QJsonValue& item : arr) {
            if (item.isString()) {
                append(item.toString());
                continue;
            }
            if (!item.isObject())
                continue;
            const QJsonObject obj = item.toObject();
            append(obj.value(QStringLiteral("id")).toString());
            append(obj.value(QStringLiteral("model")).toString());
            append(obj.value(QStringLiteral("name")).toString());
        }
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return modelIds;

    if (doc.isArray()) {
        parseArray(doc.array());
        return modelIds;
    }

    if (!doc.isObject())
        return modelIds;

    const QJsonObject root = doc.object();
    parseArray(root.value(QStringLiteral("data")).toArray());
    parseArray(root.value(QStringLiteral("models")).toArray());
    parseArray(root.value(QStringLiteral("result")).toArray());
    return modelIds;
}

QList<ModelConfigProvider> defaultModelConfigProviders()
{
    QList<ModelConfigProvider> list;
    ModelConfigProvider deepseek { "deepseek", "DeepSeek", "中国高性能 AI 模型" };
    deepseek.fields << ModelConfigField { "apiKey", "API 密钥", "sk-...", "", true, true };
    deepseek.fields << ModelConfigField { "modelId", "模型名称", "deepseek-chat", "deepseek-chat" };
    deepseek.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.deepseek.com", "https://api.deepseek.com" };
    list << deepseek;

    ModelConfigProvider openai { "openai", "OpenAI", "全球领先的 AI 语言模型" };
    openai.fields << ModelConfigField { "apiKey", "API 密钥", "sk-...", "", true, true };
    openai.fields << ModelConfigField { "modelId", "模型名称", "gpt-4o", "gpt-4o" };
    openai.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.openai.com/v1", "https://api.openai.com/v1" };
    list << openai;

    ModelConfigProvider anthropic { "anthropic", "Anthropic / Claude", "Anthropic 强大的 AI 模型" };
    anthropic.fields << ModelConfigField { "apiKey", "API 密钥", "sk-ant-...", "", true, true };
    anthropic.fields << ModelConfigField { "modelId", "模型名称", "claude-sonnet-4-5-20250929", "claude-sonnet-4-5-20250929" };
    anthropic.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.anthropic.com", "https://api.anthropic.com" };
    list << anthropic;

    ModelConfigProvider ollama { "ollama", "Ollama", "本地运行的各类型开源模型" };
    ollama.fields << ModelConfigField { "modelId", "模型名称", "llama3", "llama3" };
    ollama.fields << ModelConfigField { "baseUrl", "接口地址", "http://localhost:11434", "http://localhost:11434" };
    list << ollama;

    ModelConfigProvider gemini { "gemini", "Gemini", "Google 强大的 AI 服务" };
    gemini.fields << ModelConfigField { "apiKey", "API 密钥", "在此输入密钥", "", true, true };
    gemini.fields << ModelConfigField { "modelId", "模型名称", "gemini-1.5-pro", "gemini-1.5-pro" };
    gemini.fields << ModelConfigField { "baseUrl", "接口地址", "https://generativelanguage.googleapis.com", "" };
    list << gemini;

    return list;
}

} // namespace

namespace ModelConfigDialog {

void show(QWidget* parent, const ModelConfigDialogCapabilities& capabilities)
{
    if (!capabilities.governanceCommands || !capabilities.governanceQueries || !capabilities.modelCatalog)
        return;

    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle(QObject::tr("模型配置管理"));
    dlg->resize(800, 520);

    auto* page = new ModelConfigManagerPage(dlg);
    page->setProviders(defaultModelConfigProviders());
    page->setYamlPath(capabilities.governanceQueries->modelConfigPath());

    page->setConfigListLoader([](const QString& path) -> QList<ModelConfigEntry> {
        QList<ModelConfigEntry> result;
        const auto instances = ModelConfigLoader::loadProviderInstances(path, false);
        for (const auto& inst : instances) {
            ModelConfigEntry entry;
            entry.configId = inst.instanceId;
            entry.providerId = inst.providerType;
            entry.displayName = inst.displayName.isEmpty() ? inst.instanceId : inst.displayName;
            entry.baseUrl = inst.baseUrl;
            entry.apiKey = inst.apiKey;
            entry.modelId = QString();
            entry.enabled = inst.enabled;
            result.append(entry);
        }
        return result;
    });
    page->setSingleConfigLoader([](const QString& path, const QString& configId) -> ModelConfigEntry {
        ModelConfigEntry entry;
        const ProviderInstanceConfig inst = ModelConfigLoader::getProviderInstance(path, configId, false);
        if (inst.isValid()) {
            entry.configId = inst.instanceId;
            entry.providerId = inst.providerType;
            entry.displayName = inst.displayName.isEmpty() ? inst.instanceId : inst.displayName;
            entry.baseUrl = inst.baseUrl;
            entry.apiKey = inst.apiKey;
            entry.modelId = QString();
            entry.enabled = inst.enabled;
        }
        return entry;
    });
    page->setDefaultConfigIdLoader([](const QString& path) -> QString {
        return ModelConfigLoader::getDefaultProvider(path);
    });

    page->refreshConfigList();
    page->applyStyleSheet();

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(page);

    const QString yamlPath = capabilities.governanceQueries->modelConfigPath();

    QObject::connect(page, &ModelConfigManagerPage::configSaved, dlg, [=](const QVariantMap& config) {
        ModelConfig modelConfig;
        modelConfig.modelId = config.value("modelId").toString().trimmed();
        modelConfig.configId = config.value("configId").toString().trimmed();
        modelConfig.enabled = config.value("enabled", true).toBool();
        modelConfig.displayName = config.value("displayName").toString().trimmed();
        if (modelConfig.displayName.isEmpty())
            modelConfig.displayName = config.value("providerName").toString().trimmed();
        if (modelConfig.displayName.isEmpty())
            modelConfig.displayName = modelConfig.configId;
        modelConfig.provider = canonicalProviderId(config.value("providerId").toString());
        if (modelConfig.provider.isEmpty())
            modelConfig.provider = config.value("providerId").toString().trimmed();
        modelConfig.baseUrl = config.value("baseUrl").toString().trimmed();

        if (modelConfig.configId.isEmpty())
            modelConfig.configId = modelConfig.modelId;

        const bool isEdit = config.value("editMode").toBool();
        if (!isEdit) {
            const ProviderInstanceConfig existing = ModelConfigLoader::getProviderInstance(yamlPath, modelConfig.configId, false);
            if (existing.isValid() && !existing.providerType.isEmpty()
                && canonicalProviderId(existing.providerType) != modelConfig.provider) {
                QMessageBox::warning(
                    dlg,
                    QObject::tr("名称冲突"),
                    QObject::tr("名称「%1」已被 Provider「%2」使用。\n请修改名称后重试，或先删除旧配置。")
                        .arg(modelConfig.displayName.isEmpty() ? modelConfig.configId : modelConfig.displayName,
                             existing.providerType));
                return;
            }
        }

        QString apiKeyStored;
        QString apiKeyRuntime;
        const QString apiKeyInput = config.value("apiKey").toString().trimmed();
        if (!apiKeyInput.isEmpty()) {
            QString keychainId;
            if (KeychainHelper::parseKeyRef(apiKeyInput, &keychainId)) {
                apiKeyStored = KeychainHelper::makeKeyRef(keychainId);
                bool ok = false;
                QString error;
                apiKeyRuntime = KeychainHelper::readPasswordSync(keychainId, &ok, &error);
                if (!ok || apiKeyRuntime.isEmpty()) {
                    QMessageBox::warning(dlg, QObject::tr("读取失败"), QObject::tr("无法从系统密钥库读取：%1").arg(error.isEmpty() ? QObject::tr("未知错误") : error));
                    return;
                }
            } else if (isEnvVarReference(apiKeyInput)) {
                apiKeyStored = apiKeyInput;
                QString varName;
                if (extractEnvVarName(apiKeyInput, &varName))
                    apiKeyRuntime = QProcessEnvironment::systemEnvironment().value(varName);
                if (apiKeyRuntime.isEmpty()) {
                    QMessageBox::warning(dlg, QObject::tr("环境变量未设置"), QObject::tr("未读取到 %1，请先设置环境变量后再导入。").arg(apiKeyInput));
                    return;
                }
            } else {
                keychainId = KeychainHelper::entryIdForModel(modelConfig.provider, modelConfig.modelId);
                QString error;
                if (!KeychainHelper::writePasswordSync(keychainId, apiKeyInput, &error)) {
                    QMessageBox::warning(dlg, QObject::tr("保存失败"), QObject::tr("无法写入系统密钥库：%1").arg(error.isEmpty() ? QObject::tr("未知错误") : error));
                    return;
                }
                apiKeyStored = KeychainHelper::makeKeyRef(keychainId);
                apiKeyRuntime = apiKeyInput;
            }
        }
        modelConfig.apiKey = apiKeyRuntime;
        modelConfig.authType = isAnthropicProviderId(modelConfig.provider)
            ? QStringLiteral("X-API-Key")
            : QStringLiteral("Bearer");
        modelConfig.temperature = 0.7;
        modelConfig.maxTokens = 4096;
        modelConfig.timeoutMs = 180000;
        modelConfig.capabilities << Capability::TextGeneration << Capability::ToolCalling;
        modelConfig.toolCalling = true;
        modelConfig.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt();

        ProviderInstanceConfig inst;
        inst.instanceId = modelConfig.configId;
        inst.enabled = modelConfig.enabled;
        inst.displayName = modelConfig.displayName;
        inst.providerType = modelConfig.provider;
        inst.baseUrl = modelConfig.baseUrl;
        inst.apiKey = apiKeyStored;
        inst.authType = modelConfig.authType;
        inst.defaultTemperature = modelConfig.temperature;
        inst.defaultMaxTokens = modelConfig.maxTokens;
        inst.defaultTimeoutMs = modelConfig.timeoutMs;
        inst.capabilities = modelConfig.capabilities;
        inst.toolCalling = modelConfig.toolCalling;
        inst.contextLength = modelConfig.contextLength;
        ModelConfigLoader::addOrUpdateProviderInstance(yamlPath, inst);
        const QString currentDefault = ModelConfigLoader::getDefaultProvider(yamlPath);
        if (currentDefault.trimmed().isEmpty())
            ModelConfigLoader::setDefaultProvider(yamlPath, modelConfig.configId);

        capabilities.governanceCommands->registerModelConfig(modelConfig);

        LLMConfig agentConfig;
        agentConfig.configId = modelConfig.configId;
        agentConfig.systemPrompt = modelConfig.systemPrompt;
        agentConfig.userName = QObject::tr("TM Agent");
        capabilities.governanceCommands->setDefaultAgentConfig(agentConfig);
        capabilities.governanceCommands->applyConfigToAllRuntimes();

        page->refreshConfigList();
        QMessageBox::information(dlg, QObject::tr("已保存"), QObject::tr("配置「%1」已保存到 %2").arg(modelConfig.configId, QDir::toNativeSeparators(yamlPath)));
    });

    QObject::connect(page, &ModelConfigManagerPage::configDeleted, dlg, [=](const QString& configId) {
        ModelConfigLoader::removeProviderInstance(yamlPath, configId);
        page->refreshConfigList();
    });

    QObject::connect(page, &ModelConfigManagerPage::defaultChanged, dlg, [=](const QString& configId) {
        ModelConfigLoader::setDefaultProvider(yamlPath, configId);
        page->refreshConfigList();
    });

    QObject::connect(page, &ModelConfigManagerPage::enabledToggled, dlg, [=](const QString& configId, bool enabled) {
        ModelConfigLoader::setProviderEnabled(yamlPath, configId, enabled);
    });

    QObject::connect(page, &ModelConfigManagerPage::testConnectionRequested, dlg, [=](const QVariantMap& config) {
        const QString providerId = canonicalProviderId(config.value(QStringLiteral("providerId")).toString());
        const QString baseUrl = config.value(QStringLiteral("baseUrl")).toString().trimmed();
        const QString apiKey = resolveApiKeyInputForTest(config.value(QStringLiteral("apiKey")).toString());

        page->clearFieldErrors();
        page->setTestStatus(ModelConfigManagerPage::TestStatus::Testing, QObject::tr("正在验证地址连通性…"));

        if (baseUrl.isEmpty()) {
            page->setFieldError(providerId, QStringLiteral("baseUrl"), QObject::tr("接口地址不能为空"));
            page->setTestStatus(ModelConfigManagerPage::TestStatus::Failed, QObject::tr("接口地址不能为空"));
            return;
        }

        const QUrl parsedBase(baseUrl);
        if (!parsedBase.isValid()
            || (parsedBase.scheme() != QStringLiteral("http")
                && parsedBase.scheme() != QStringLiteral("https"))) {
            page->setFieldError(providerId, QStringLiteral("baseUrl"), QObject::tr("请输入合法的 http/https 地址"));
            page->setTestStatus(ModelConfigManagerPage::TestStatus::Failed, QObject::tr("地址格式不合法"));
            return;
        }

        const QString modelsEndpoint = buildModelsEndpoint(providerId, baseUrl);
        if (modelsEndpoint.isEmpty()) {
            page->setFieldError(providerId, QStringLiteral("baseUrl"), QObject::tr("无法生成模型列表地址"));
            page->setTestStatus(ModelConfigManagerPage::TestStatus::Failed, QObject::tr("模型列表地址无效"));
            return;
        }

        QPointer<ModelConfigManagerPage> safePage(page);
        auto* nam = new QNetworkAccessManager(page);
        QNetworkReply* pingReply = nam->get(QNetworkRequest(parsedBase));
        QObject::connect(pingReply, &QNetworkReply::finished, page, [=]() {
            const bool reachable = isHttpReachable(pingReply);
            const QString pingError = pingReply->errorString();
            pingReply->deleteLater();

            if (!safePage) {
                nam->deleteLater();
                return;
            }

            if (!reachable) {
                safePage->setFieldError(providerId, QStringLiteral("baseUrl"), QObject::tr("无法连通：%1").arg(pingError));
                safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Failed, QObject::tr("接口地址不可达"));
                nam->deleteLater();
                return;
            }

            safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Testing, QObject::tr("地址可达，正在拉取模型列表…"));

            QNetworkRequest modelsReq { QUrl(modelsEndpoint) };
            modelsReq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

            if (isAnthropicProviderId(providerId)) {
                if (!apiKey.isEmpty())
                    modelsReq.setRawHeader("x-api-key", apiKey.toUtf8());
                modelsReq.setRawHeader("anthropic-version", "2023-06-01");
            } else if (!apiKey.isEmpty()) {
                modelsReq.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
            }

            QNetworkReply* modelsReply = nam->get(modelsReq);
            QObject::connect(modelsReply, &QNetworkReply::finished, safePage.data(), [=]() {
                const int httpStatus = modelsReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray body = modelsReply->readAll();
                const QString fallbackMsg = modelsReply->errorString();
                modelsReply->deleteLater();

                if (!safePage) {
                    nam->deleteLater();
                    return;
                }

                const bool ok = (httpStatus >= 200 && httpStatus < 300);
                if (!ok) {
                    const QString errorMsg = extractErrorMessage(body, fallbackMsg);
                    if (httpStatus == 401 || httpStatus == 403)
                        safePage->setFieldError(providerId, QStringLiteral("apiKey"), QObject::tr("鉴权失败，请检查 API Key"));
                    safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Failed, QObject::tr("地址可达，但拉取模型失败（HTTP %1）：%2").arg(httpStatus).arg(errorMsg));
                    nam->deleteLater();
                    return;
                }

                const QStringList modelIds = parseModelIdsFromResponse(body);
                if (!modelIds.isEmpty()) {
                    safePage->setFieldOptions(providerId, QStringLiteral("modelId"), modelIds, true);
                    safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Success, QObject::tr("连接成功，发现 %1 个可用模型").arg(modelIds.size()));
                } else {
                    safePage->setTestStatus(ModelConfigManagerPage::TestStatus::Success, QObject::tr("连接成功，但未返回模型列表，可手动输入模型名称"));
                }

                nam->deleteLater();
            });
        });
    });

    dlg->exec();
    dlg->deleteLater();
}

} // namespace ModelConfigDialog
