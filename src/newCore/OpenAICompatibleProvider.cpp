#include "OpenAICompatibleProvider.h"
#include "core/utils/AppSettings.h"
#include <QJsonDocument>
#include <QJsonValue>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QTimer>

namespace {
QString appendStreamContent(const QString& incoming, QString& buffer)
{
    if (incoming.isEmpty())
        return QString();
    if (buffer.isEmpty()) {
        buffer = incoming;
        return incoming;
    }
    if (incoming.startsWith(buffer)) {
        const QString inc = incoming.mid(buffer.size());
        buffer = incoming;
        return inc;
    }
    if (buffer.startsWith(incoming))
        return QString();
    const int max = qMin(buffer.size(), incoming.size());
    int lcp = 0;
    while (lcp < max && buffer.at(lcp) == incoming.at(lcp))
        ++lcp;
    if (lcp > 0) {
        const QString inc = incoming.mid(lcp);
        buffer = incoming;
        return inc;
    }
    if (incoming.size() <= 16) {
        buffer += incoming;
        return incoming;
    }
    if (buffer.endsWith(incoming))
        return QString();
    buffer += incoming;
    return incoming;
}
} // namespace

// -----------------------------------------------------------------------------
// OpenAICompatibleProvider
// -----------------------------------------------------------------------------
OpenAICompatibleProvider::OpenAICompatibleProvider(const QString& modelId, QObject* parent)
    : LLMProvider(parent)
    , m_modelId(modelId)
{
    m_descriptor.modelId = modelId;
    m_descriptor.capabilities << Capability::TextGeneration << Capability::ToolCalling;
    m_descriptor.toolCalling = true;

    // 配置超时定时器（基类已创建）
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_currentReply) {
            m_currentReply->abort();
            LLMError err;
            err.errorCode = LLMErrorCode::Timeout;
            err.userMessage = tr("网络请求超时");
            emit errorOccurred(err);
        }
    });
}

OpenAICompatibleProvider::~OpenAICompatibleProvider()
{
    abort();
}

LLMResponse OpenAICompatibleProvider::generate(const LLMRequest& request)
{
    Q_UNUSED(request);
    LLMResponse out;
    out.error.errorCode = LLMErrorCode::Unknown;
    out.error.userMessage = tr("本 Provider 仅支持流式调用，请使用 generateStream。");
    return out;
}

void OpenAICompatibleProvider::applyConfig(const ModelConfig& config)
{
    m_config = config;
    
    // 更新能力描述
    m_descriptor.capabilities = config.capabilities;
    m_descriptor.toolCalling = config.toolCalling;
    m_descriptor.contextLength = config.contextLength;
}

void OpenAICompatibleProvider::generateStream(const LLMRequest& request)
{
    // 优先使用注入的配置，回退到 AppSettings
    QString apiKey = m_config.apiKey;
    QString baseUrl = m_config.baseUrl;
    QString authType = m_config.authType;
    
    if (apiKey.isEmpty()) {
        apiKey = AppSettings::getApiKey();
    }
    if (baseUrl.isEmpty()) {
        baseUrl = AppSettings::getBaseUrl();
    }
    if (baseUrl.isEmpty()) {
        baseUrl = QStringLiteral("https://api.deepseek.com");
    }
    if (authType.isEmpty()) {
        authType = QStringLiteral("Bearer");
    }

    startStream(request, apiKey, baseUrl, authType, QStringLiteral("/chat/completions"));
}


bool OpenAICompatibleProvider::supports(const QString& capability) const
{
    return m_descriptor.supports(capability);
}

CapabilityDescriptor OpenAICompatibleProvider::descriptor() const
{
    return m_descriptor;
}

void OpenAICompatibleProvider::abort()
{
    if (m_currentReply) {
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    if (m_timeoutTimer)
        m_timeoutTimer->stop();
}

void OpenAICompatibleProvider::startStream(const LLMRequest& request,
                                          const QString& apiKey,
                                          const QString& baseUrl,
                                          const QString& authType,
                                          const QString& endpoint)
{
    abort();

    m_fullContent.clear();
    m_lastFinishReason.clear();
    m_streamingToolCallsJson = QJsonArray();
    m_timeoutMs = request.timeoutMs > 0 ? request.timeoutMs : 180000;

    QJsonObject root = buildRequestBody(request);

    QString urlStr = baseUrl;
    if (!urlStr.endsWith("/") && !endpoint.startsWith("/"))
        urlStr += "/";
    urlStr += endpoint;

    QUrl url(urlStr);
    QNetworkRequest netRequest(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    if (authType == QStringLiteral("X-API-Key"))
        netRequest.setRawHeader("X-API-Key", apiKey.toUtf8());
    else if (authType == QStringLiteral("api-key"))
        netRequest.setRawHeader("api-key", apiKey.toUtf8());
    else
        netRequest.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());

    m_currentReply = m_manager->post(netRequest, QJsonDocument(root).toJson());
    m_timeoutTimer->start(m_timeoutMs);

    connect(m_currentReply, &QNetworkReply::readyRead, this, &OpenAICompatibleProvider::handleReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &OpenAICompatibleProvider::handleFinished);
}

QJsonObject OpenAICompatibleProvider::buildRequestBody(const LLMRequest& request) const
{
    QJsonObject root;
    root["model"] = request.modelId;
    root["max_tokens"] = request.maxTokens;
    root["temperature"] = request.temperature;
    root["stream"] = true;
    root["messages"] = request.messages;
    if (!request.tools.isEmpty())
        root["tools"] = request.tools;
    return root;
}

void OpenAICompatibleProvider::handleReadyRead()
{
    if (!m_currentReply)
        return;
    while (m_currentReply->canReadLine()) {
        QByteArray line = m_currentReply->readLine().trimmed();
        if (!line.isEmpty())
            parseStreamEventLine(line);
    }
}

void OpenAICompatibleProvider::handleFinished()
{
    if (m_timeoutTimer)
        m_timeoutTimer->stop();
    if (!m_currentReply)
        return;

    if (m_currentReply->error() != QNetworkReply::NoError) {
        LLMError err;
        err.errorCode = LLMErrorCode::ProtocolError;
        err.userMessage = m_currentReply->errorString();
        err.diagnostics[QStringLiteral("qt_network_error")] = static_cast<int>(m_currentReply->error());
        emit errorOccurred(err);
    } else {
        if (m_lastFinishReason == QStringLiteral("tool_calls") && !m_streamingToolCallsJson.isEmpty()) {
            QJsonArray assembled = mergeStreamingToolCalls(m_streamingToolCallsJson);
            emit toolCallsReceived(assembled);
        } else {
            emit streamComplete(m_fullContent, LLMUsage());
        }
    }

    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void OpenAICompatibleProvider::parseStreamEventLine(const QByteArray& line)
{
    if (!line.startsWith("data: "))
        return;

    QString data = QString::fromUtf8(line.mid(6));
    if (data == QStringLiteral("[DONE]"))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (doc.isNull())
        return;

    QJsonObject obj = doc.object();
    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty())
        return;

    QJsonObject choice = choices[0].toObject();
    QJsonObject delta = choice["delta"].toObject();

    if (choice.contains("finish_reason") && !choice["finish_reason"].isNull())
        m_lastFinishReason = choice["finish_reason"].toString();

    if (delta.contains("content")) {
        QString content = delta["content"].toString();
        const QString inc = appendStreamContent(content, m_fullContent);
        if (!inc.isEmpty())
            emit deltaReceived(inc);
    }

    if (delta.contains("tool_calls")) {
        QJsonArray toolCallsArray = delta["tool_calls"].toArray();
        for (const QJsonValue& tc : toolCallsArray)
            m_streamingToolCallsJson.append(tc);
    }
}

QJsonArray OpenAICompatibleProvider::mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson)
{
    QMap<int, QJsonObject> toolCallsMap;
    for (const QJsonValue& tcVal : streamingToolCallsJson) {
        const QJsonObject toolObject = tcVal.toObject();
        const int index = toolObject["index"].toInt();
        QJsonObject& current = toolCallsMap[index];
        if (toolObject.contains("id"))
            current["id"] = toolObject["id"];
        if (toolObject.contains("type"))
            current["type"] = toolObject["type"];
        const QJsonObject funcObj = toolObject["function"].toObject();
        if (!funcObj.isEmpty()) {
            QJsonObject currentFunc = current["function"].toObject();
            if (funcObj.contains("name"))
                currentFunc["name"] = funcObj["name"];
            if (funcObj.contains("arguments"))
                currentFunc["arguments"] = currentFunc["arguments"].toString() + funcObj["arguments"].toString();
            current["function"] = currentFunc;
        }
    }
    QJsonArray result;
    for (const QJsonObject& tc : toolCallsMap.values())
        result.append(tc);
    return result;
}
