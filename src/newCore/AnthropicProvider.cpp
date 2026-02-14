#include "AnthropicProvider.h"
#include <QJsonDocument>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QDebug>

AnthropicProvider::AnthropicProvider(const QString& modelId, QObject* parent)
    : LLMProvider(parent)
    , m_modelId(modelId)
{
    m_descriptor.modelId = modelId;
    m_descriptor.capabilities << Capability::TextGeneration << Capability::ToolCalling;
    m_descriptor.toolCalling = true;

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

AnthropicProvider::~AnthropicProvider()
{
    abort();
}

LLMResponse AnthropicProvider::generate(const LLMRequest& request)
{
    Q_UNUSED(request);
    LLMResponse out;
    out.error.errorCode = LLMErrorCode::Unknown;
    out.error.userMessage = tr("本 Provider 仅支持流式调用，请使用 generateStream。");
    return out;
}

void AnthropicProvider::applyConfig(const ModelConfig& config)
{
    m_config = config;
    m_descriptor.capabilities = config.capabilities;
    m_descriptor.toolCalling = config.toolCalling;
    m_descriptor.contextLength = config.contextLength;
}

void AnthropicProvider::generateStream(const LLMRequest& request)
{
    QString apiKey = m_config.apiKey;
    QString baseUrl = m_config.baseUrl;
    if (baseUrl.isEmpty())
        baseUrl = QStringLiteral("https://api.anthropic.com");

    startStream(request, apiKey, baseUrl);
}

bool AnthropicProvider::supports(const QString& capability) const
{
    return m_descriptor.supports(capability);
}

CapabilityDescriptor AnthropicProvider::descriptor() const
{
    return m_descriptor;
}

void AnthropicProvider::abort()
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

void AnthropicProvider::startStream(const LLMRequest& request,
                                    const QString& apiKey,
                                    const QString& baseUrl)
{
    abort();

    m_fullContent.clear();
    m_stopReason.clear();
    m_toolUseBlocks = QJsonArray();
    m_buffer.clear();
    m_timeoutMs = request.timeoutMs > 0 ? request.timeoutMs : 180000;

    QJsonObject root = buildRequestBody(request);

    QString urlStr = baseUrl;
    if (!urlStr.endsWith("/"))
        urlStr += "/";
    urlStr += "v1/messages";

    QUrl url(urlStr);
    QNetworkRequest netRequest(url);
    netRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    netRequest.setRawHeader("x-api-key", apiKey.toUtf8());
    netRequest.setRawHeader("anthropic-version", "2023-06-01");

    qDebug() << "[AnthropicProvider] POST" << urlStr;

    m_currentReply = m_manager->post(netRequest, QJsonDocument(root).toJson());
    m_timeoutTimer->start(m_timeoutMs);

    connect(m_currentReply, &QNetworkReply::readyRead, this, &AnthropicProvider::handleReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &AnthropicProvider::handleFinished);
}

QJsonObject AnthropicProvider::buildRequestBody(const LLMRequest& request) const
{
    QJsonObject root;
    root["model"] = request.modelId.isEmpty() ? m_modelId : request.modelId;
    root["max_tokens"] = request.maxTokens > 0 ? request.maxTokens : 4096;
    if (request.temperature > 0)
        root["temperature"] = request.temperature;
    root["stream"] = true;

    QJsonArray anthropicMessages = convertMessagesToAnthropic(request.messages);
    root["messages"] = anthropicMessages;

    for (const QJsonValue& msg : request.messages) {
        QJsonObject msgObj = msg.toObject();
        if (msgObj["role"].toString() == "system") {
            root["system"] = msgObj["content"].toString();
            break;
        }
    }

    if (!request.tools.isEmpty()) {
        QJsonArray anthropicTools = convertToolsToAnthropic(request.tools);
        root["tools"] = anthropicTools;
    }

    return root;
}

QJsonArray AnthropicProvider::convertMessagesToAnthropic(const QJsonArray& openaiMessages) const
{
    QJsonArray result;

    for (const QJsonValue& msgVal : openaiMessages) {
        QJsonObject msg = msgVal.toObject();
        QString role = msg["role"].toString();

        if (role == "system")
            continue;

        QJsonObject anthropicMsg;
        anthropicMsg["role"] = role;

        QJsonValue content = msg["content"];
        if (content.isString()) {
            anthropicMsg["content"] = content.toString();
        } else if (content.isArray()) {
            QJsonArray contentBlocks;
            for (const QJsonValue& block : content.toArray()) {
                QJsonObject blockObj = block.toObject();
                QString type = blockObj["type"].toString();

                if (type == "text") {
                    contentBlocks.append(QJsonObject{{"type", "text"}, {"text", blockObj["text"].toString()}});
                } else if (type == "image_url") {
                    QJsonObject source;
                    source["type"] = "url";
                    source["url"] = blockObj["image_url"].toObject()["url"].toString();
                    contentBlocks.append(QJsonObject{{"type", "image"}, {"source", source}});
                }
            }
            anthropicMsg["content"] = contentBlocks;
        }

        if (msg.contains("tool_calls")) {
            QJsonArray contentBlocks;
            if (!content.toString().isEmpty())
                contentBlocks.append(QJsonObject{{"type", "text"}, {"text", content.toString()}});

            for (const QJsonValue& tc : msg["tool_calls"].toArray()) {
                QJsonObject tcObj = tc.toObject();
                QJsonObject toolUseBlock;
                toolUseBlock["type"] = "tool_use";
                toolUseBlock["id"] = tcObj["id"].toString();
                toolUseBlock["name"] = tcObj["function"].toObject()["name"].toString();
                QString argsStr = tcObj["function"].toObject()["arguments"].toString();
                toolUseBlock["input"] = QJsonDocument::fromJson(argsStr.toUtf8()).object();
                contentBlocks.append(toolUseBlock);
            }
            anthropicMsg["content"] = contentBlocks;
        }

        if (role == "tool") {
            anthropicMsg["role"] = "user";
            QJsonObject toolResultBlock;
            toolResultBlock["type"] = "tool_result";
            toolResultBlock["tool_use_id"] = msg["tool_call_id"].toString();
            toolResultBlock["content"] = msg["content"].toString();
            anthropicMsg["content"] = QJsonArray{toolResultBlock};
        }

        result.append(anthropicMsg);
    }

    return result;
}

QJsonArray AnthropicProvider::convertToolsToAnthropic(const QJsonArray& openaiTools) const
{
    QJsonArray result;
    for (const QJsonValue& toolVal : openaiTools) {
        QJsonObject tool = toolVal.toObject();
        if (tool["type"].toString() != "function")
            continue;
        QJsonObject func = tool["function"].toObject();
        QJsonObject anthropicTool;
        anthropicTool["name"] = func["name"].toString();
        anthropicTool["description"] = func["description"].toString();
        anthropicTool["input_schema"] = func["parameters"].toObject();
        result.append(anthropicTool);
    }
    return result;
}

void AnthropicProvider::handleReadyRead()
{
    if (!m_currentReply)
        return;

    m_buffer += m_currentReply->readAll();

    while (true) {
        int idx = m_buffer.indexOf('\n');
        if (idx < 0)
            break;
        QByteArray line = m_buffer.left(idx).trimmed();
        m_buffer = m_buffer.mid(idx + 1);
        if (!line.isEmpty())
            parseStreamEventLine(line);
    }
}

void AnthropicProvider::handleFinished()
{
    if (m_timeoutTimer)
        m_timeoutTimer->stop();
    if (!m_currentReply)
        return;

    // 处理剩余 buffer
    if (!m_buffer.isEmpty()) {
        parseStreamEventLine(m_buffer.trimmed());
        m_buffer.clear();
    }

    const QNetworkReply::NetworkError netErr = m_currentReply->error();
    const bool hasToolBlocks = !m_toolUseBlocks.isEmpty();
    const bool hasText = !m_fullContent.isEmpty();
    const bool hasStopReason = !m_stopReason.isEmpty();
    const bool isBenignRemoteClose =
        (netErr == QNetworkReply::RemoteHostClosedError)
        && (hasToolBlocks || hasText || hasStopReason);

    if (netErr != QNetworkReply::NoError && !isBenignRemoteClose) {
        LLMError err;
        err.errorCode = LLMErrorCode::ProtocolError;
        err.userMessage = m_currentReply->errorString();
        
        // 尝试解析错误响应
        QByteArray responseData = m_currentReply->readAll();
        if (!responseData.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(responseData);
            if (!doc.isNull()) {
                QJsonObject errObj = doc.object()["error"].toObject();
                if (!errObj.isEmpty()) {
                    err.userMessage = errObj["message"].toString();
                    err.diagnostics["type"] = errObj["type"].toString();
                }
            }
        }
        
        err.diagnostics[QStringLiteral("qt_network_error")] = static_cast<int>(netErr);
        emit errorOccurred(err);
    } else {
        if (m_stopReason == QStringLiteral("tool_use") && !m_toolUseBlocks.isEmpty()) {
            QJsonArray toolCalls;
            for (const QJsonValue& block : m_toolUseBlocks) {
                QJsonObject blockObj = block.toObject();
                QJsonObject func;
                func["name"] = blockObj["name"].toString();
                func["arguments"] = QString::fromUtf8(
                    QJsonDocument(blockObj["input"].toObject()).toJson(QJsonDocument::Compact));
                toolCalls.append(QJsonObject{
                    {"id", blockObj["id"].toString()},
                    {"type", "function"},
                    {"function", func}
                });
            }
            emit toolCallsReceived(toolCalls);
        } else {
            emit streamComplete(m_fullContent, LLMUsage());
        }
    }

    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void AnthropicProvider::parseStreamEventLine(const QByteArray& line)
{
    if (line.startsWith("event:"))
        return;

    if (!line.startsWith("data:"))
        return;

    QString data = QString::fromUtf8(line.mid(5).trimmed());
    if (data.isEmpty())
        return;

    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (doc.isNull())
        return;

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    
    if (type == "message_start") {
        QJsonObject message = obj["message"].toObject();
        qDebug() << "[AnthropicProvider] message_start, id:" << message["id"].toString();
    }
    else if (type == "content_block_start") {
        QJsonObject contentBlock = obj["content_block"].toObject();
        if (contentBlock["type"].toString() == "tool_use")
            m_toolUseBlocks.append(contentBlock);
    }
    else if (type == "content_block_delta") {
        QJsonObject delta = obj["delta"].toObject();
        QString deltaType = delta["type"].toString();

        if (deltaType == "text_delta") {
            QString text = delta["text"].toString();
            m_fullContent += text;
            emit deltaReceived(text);
        }
        else if (deltaType == "input_json_delta") {
            int index = obj["index"].toInt();
            if (index < m_toolUseBlocks.size()) {
                QJsonObject block = m_toolUseBlocks[index].toObject();
                block["partial_input"] = block["partial_input"].toString() + delta["partial_json"].toString();
                m_toolUseBlocks[index] = block;
            }
        }
    }
    else if (type == "content_block_stop") {
        int index = obj["index"].toInt();
        if (index < m_toolUseBlocks.size()) {
            QJsonObject block = m_toolUseBlocks[index].toObject();
            QString partialInput = block["partial_input"].toString();
            if (!partialInput.isEmpty()) {
                block["input"] = QJsonDocument::fromJson(partialInput.toUtf8()).object();
                block.remove("partial_input");
                m_toolUseBlocks[index] = block;
            }
        }
    }
    else if (type == "message_delta") {
        QJsonObject delta = obj["delta"].toObject();
        if (delta.contains("stop_reason"))
            m_stopReason = delta["stop_reason"].toString();
    }
    else if (type == "message_stop") {
        qDebug() << "[AnthropicProvider] message_stop";
    }
    else if (type == "error") {
        QJsonObject error = obj["error"].toObject();
        qWarning() << "[AnthropicProvider] Error:" << error["message"].toString();
    }
}
