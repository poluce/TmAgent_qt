#include "AnthropicProvider.h"
#include <QJsonDocument>
#include <QJsonValue>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QDebug>

// -----------------------------------------------------------------------------
// AnthropicProvider - Anthropic Messages API 实现
// API 文档: https://docs.anthropic.com/en/api/messages
// -----------------------------------------------------------------------------

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
    
    if (baseUrl.isEmpty()) {
        baseUrl = QStringLiteral("https://api.anthropic.com");
    }

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
    
    // Anthropic 认证方式
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
    
    // 模型 ID
    root["model"] = request.modelId.isEmpty() ? m_modelId : request.modelId;
    
    // max_tokens 是必需的
    root["max_tokens"] = request.maxTokens > 0 ? request.maxTokens : 4096;
    
    // 温度（可选）
    if (request.temperature > 0)
        root["temperature"] = request.temperature;
    
    // 流式输出
    root["stream"] = true;
    
    // 转换消息格式（OpenAI → Anthropic）
    QJsonArray anthropicMessages = convertMessagesToAnthropic(request.messages);
    root["messages"] = anthropicMessages;
    
    // 如果有 system prompt，从消息中提取
    for (const QJsonValue& msg : request.messages) {
        QJsonObject msgObj = msg.toObject();
        if (msgObj["role"].toString() == "system") {
            root["system"] = msgObj["content"].toString();
            break;
        }
    }
    
    // 工具（如果有）
    if (!request.tools.isEmpty()) {
        QJsonArray anthropicTools = convertToolsToAnthropic(request.tools);
        root["tools"] = anthropicTools;
    }
    
    return root;
}

QJsonArray AnthropicProvider::convertMessagesToAnthropic(const QJsonArray& openaiMessages) const
{
    // Anthropic 消息格式：
    // - 不支持 "system" role 在 messages 数组里，需要单独放到 "system" 字段
    // - role 只能是 "user" 或 "assistant"
    // - content 可以是字符串或 content blocks 数组
    
    QJsonArray result;
    
    for (const QJsonValue& msgVal : openaiMessages) {
        QJsonObject msg = msgVal.toObject();
        QString role = msg["role"].toString();
        
        // 跳过 system 消息（已在 buildRequestBody 中处理）
        if (role == "system")
            continue;
        
        QJsonObject anthropicMsg;
        anthropicMsg["role"] = role;
        
        // 处理 content
        QJsonValue content = msg["content"];
        if (content.isString()) {
            anthropicMsg["content"] = content.toString();
        } else if (content.isArray()) {
            // 多模态内容，需要转换格式
            QJsonArray contentBlocks;
            for (const QJsonValue& block : content.toArray()) {
                QJsonObject blockObj = block.toObject();
                QString type = blockObj["type"].toString();
                
                if (type == "text") {
                    QJsonObject textBlock;
                    textBlock["type"] = "text";
                    textBlock["text"] = blockObj["text"].toString();
                    contentBlocks.append(textBlock);
                } else if (type == "image_url") {
                    // Anthropic 图片格式不同，这里简化处理
                    QJsonObject imageBlock;
                    imageBlock["type"] = "image";
                    QJsonObject source;
                    source["type"] = "url";
                    source["url"] = blockObj["image_url"].toObject()["url"].toString();
                    imageBlock["source"] = source;
                    contentBlocks.append(imageBlock);
                }
            }
            anthropicMsg["content"] = contentBlocks;
        }
        
        // 处理 tool_calls（assistant 消息中的工具调用）
        if (msg.contains("tool_calls")) {
            QJsonArray toolCalls = msg["tool_calls"].toArray();
            QJsonArray contentBlocks;
            
            // 先添加文本内容（如果有）
            if (!content.toString().isEmpty()) {
                QJsonObject textBlock;
                textBlock["type"] = "text";
                textBlock["text"] = content.toString();
                contentBlocks.append(textBlock);
            }
            
            // 添加 tool_use blocks
            for (const QJsonValue& tc : toolCalls) {
                QJsonObject tcObj = tc.toObject();
                QJsonObject toolUseBlock;
                toolUseBlock["type"] = "tool_use";
                toolUseBlock["id"] = tcObj["id"].toString();
                toolUseBlock["name"] = tcObj["function"].toObject()["name"].toString();
                
                // 解析 arguments JSON 字符串
                QString argsStr = tcObj["function"].toObject()["arguments"].toString();
                QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
                toolUseBlock["input"] = argsDoc.object();
                
                contentBlocks.append(toolUseBlock);
            }
            
            anthropicMsg["content"] = contentBlocks;
        }
        
        // 处理 tool 消息（工具结果）
        if (role == "tool") {
            anthropicMsg["role"] = "user";
            QJsonArray contentBlocks;
            QJsonObject toolResultBlock;
            toolResultBlock["type"] = "tool_result";
            toolResultBlock["tool_use_id"] = msg["tool_call_id"].toString();
            toolResultBlock["content"] = msg["content"].toString();
            contentBlocks.append(toolResultBlock);
            anthropicMsg["content"] = contentBlocks;
        }
        
        result.append(anthropicMsg);
    }
    
    return result;
}

QJsonArray AnthropicProvider::convertToolsToAnthropic(const QJsonArray& openaiTools) const
{
    // OpenAI tools 格式:
    // { "type": "function", "function": { "name": "...", "description": "...", "parameters": {...} } }
    //
    // Anthropic tools 格式:
    // { "name": "...", "description": "...", "input_schema": {...} }
    
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
    
    // SSE 格式解析：按行处理
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
        // 检查是否有工具调用
        if (m_stopReason == QStringLiteral("tool_use") && !m_toolUseBlocks.isEmpty()) {
            // 转换 Anthropic tool_use 到 OpenAI tool_calls 格式
            QJsonArray toolCalls;
            for (const QJsonValue& block : m_toolUseBlocks) {
                QJsonObject blockObj = block.toObject();
                QJsonObject toolCall;
                toolCall["id"] = blockObj["id"].toString();
                toolCall["type"] = "function";
                QJsonObject func;
                func["name"] = blockObj["name"].toString();
                func["arguments"] = QString::fromUtf8(QJsonDocument(blockObj["input"].toObject()).toJson(QJsonDocument::Compact));
                toolCall["function"] = func;
                toolCalls.append(toolCall);
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
    // Anthropic SSE 格式:
    // event: message_start
    // data: {"type":"message_start","message":{...}}
    //
    // event: content_block_delta
    // data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}
    //
    // event: message_stop
    // data: {"type":"message_stop"}
    
    if (line.startsWith("event:")) {
        // 事件类型行，暂时忽略
        return;
    }
    
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
        // 消息开始，可以获取 message.id 等信息
        QJsonObject message = obj["message"].toObject();
        qDebug() << "[AnthropicProvider] message_start, id:" << message["id"].toString();
    }
    else if (type == "content_block_start") {
        // 内容块开始
        QJsonObject contentBlock = obj["content_block"].toObject();
        QString blockType = contentBlock["type"].toString();
        
        if (blockType == "tool_use") {
            // 工具调用开始
            m_toolUseBlocks.append(contentBlock);
        }
    }
    else if (type == "content_block_delta") {
        // 内容增量
        QJsonObject delta = obj["delta"].toObject();
        QString deltaType = delta["type"].toString();
        
        if (deltaType == "text_delta") {
            QString text = delta["text"].toString();
            m_fullContent += text;
            emit deltaReceived(text);
        }
        else if (deltaType == "input_json_delta") {
            // 工具调用参数增量
            int index = obj["index"].toInt();
            if (index < m_toolUseBlocks.size()) {
                QJsonObject block = m_toolUseBlocks[index].toObject();
                QString partialJson = block["partial_input"].toString();
                partialJson += delta["partial_json"].toString();
                block["partial_input"] = partialJson;
                m_toolUseBlocks[index] = block;
            }
        }
    }
    else if (type == "content_block_stop") {
        // 内容块结束
        int index = obj["index"].toInt();
        if (index < m_toolUseBlocks.size()) {
            QJsonObject block = m_toolUseBlocks[index].toObject();
            // 解析完整的 input JSON
            QString partialInput = block["partial_input"].toString();
            if (!partialInput.isEmpty()) {
                QJsonDocument inputDoc = QJsonDocument::fromJson(partialInput.toUtf8());
                block["input"] = inputDoc.object();
                block.remove("partial_input");
                m_toolUseBlocks[index] = block;
            }
        }
    }
    else if (type == "message_delta") {
        // 消息级别的增量（包含 stop_reason）
        QJsonObject delta = obj["delta"].toObject();
        if (delta.contains("stop_reason")) {
            m_stopReason = delta["stop_reason"].toString();
        }
    }
    else if (type == "message_stop") {
        // 消息结束
        qDebug() << "[AnthropicProvider] message_stop";
    }
    else if (type == "error") {
        // 错误
        QJsonObject error = obj["error"].toObject();
        qWarning() << "[AnthropicProvider] Error:" << error["message"].toString();
    }
}
