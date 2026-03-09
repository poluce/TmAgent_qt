#include "DeepSeekProvider.h"
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QUrl>

DeepSeekProvider::DeepSeekProvider(const QString& modelId, QObject* parent)
    : LLMProvider(modelId, parent)
{
}

void DeepSeekProvider::generateStream(const LLMRequest& request)
{
    QString apiKey = m_config.apiKey;
    QString baseUrl = m_config.baseUrl;
    QString authType = m_config.authType;

    if (baseUrl.isEmpty())
        baseUrl = QStringLiteral("https://api.deepseek.com");
    if (authType.isEmpty())
        authType = QStringLiteral("Bearer");

    startStream(request, apiKey, baseUrl, authType);
}

void DeepSeekProvider::startStream(const LLMRequest& request, const QString& apiKey, const QString& baseUrl, const QString& authType)
{
    abort();

    m_fullContent.clear();
    m_reasoningContent.clear();
    m_lastFinishReason.clear();
    m_streamingToolCallsJson = QJsonArray();
    m_timeoutMs = request.timeoutMs > 0 ? request.timeoutMs : 180000;

    QJsonObject root = buildRequestBody(request);
    if (root.contains("tools")) {
        QJsonArray toolsArr = root["tools"].toArray();
        qDebug() << "[DeepSeekProvider] Sending" << toolsArr.size() << "tools";
        qDebug().noquote() << "[DeepSeekProvider] Request body size:"
                           << QJsonDocument(root).toJson(QJsonDocument::Compact).size() << "bytes";
    }

    QString urlStr = baseUrl;
    if (!urlStr.endsWith("/"))
        urlStr += "/";
    urlStr += "chat/completions";

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

    connect(m_currentReply, &QNetworkReply::readyRead, this, &DeepSeekProvider::handleReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &DeepSeekProvider::handleFinished);
}

QJsonObject DeepSeekProvider::buildRequestBody(const LLMRequest& request) const
{
    QJsonObject root;
    root["model"] = request.modelId;
    root["max_tokens"] = request.maxTokens;
    root["temperature"] = request.temperature;
    root["stream"] = true;

    // 找出最后一轮用户提问的索引
    int lastUserIdx = -1;
    for (int i = request.messages.size() - 1; i >= 0; --i) {
        if (request.messages.at(i).toObject().value("role").toString() == QStringLiteral("user")) {
            lastUserIdx = i;
            break;
        }
    }

    // 组装 messages：对 role=assistant 的消息处理 reasoning_content
    QJsonArray messages;
    for (int i = 0; i < request.messages.size(); ++i) {
        QJsonObject msg = request.messages.at(i).toObject();
        // DeepSeek 官方指南：新一轮用户提问开始时，之前的 reasoning_content 会被 API 忽略。
        // 故主动移除最后一个 user 提问之前的 reasoning_content，以节省带宽和 Token。
        if (i < lastUserIdx && msg.value("role").toString() == QStringLiteral("assistant")) {
            msg.remove("reasoning_content");
        }
        messages.append(msg);
    }
    root["messages"] = messages;

    if (!request.tools.isEmpty())
        root["tools"] = request.tools;

    return root;
}

void DeepSeekProvider::handleReadyRead()
{
    if (!m_currentReply)
        return;
    while (m_currentReply->canReadLine()) {
        QByteArray line = m_currentReply->readLine().trimmed();
        if (!line.isEmpty())
            parseStreamEventLine(line);
    }
}

void DeepSeekProvider::handleFinished()
{
    m_timeoutTimer->stop();
    if (!m_currentReply)
        return;

    const QNetworkReply::NetworkError netErr = m_currentReply->error();
    const bool hasContent = !m_streamingToolCallsJson.isEmpty()
        || !m_fullContent.isEmpty()
        || !m_lastFinishReason.isEmpty();
    const bool isBenignRemoteClose = (netErr == QNetworkReply::RemoteHostClosedError) && hasContent;

    if (netErr != QNetworkReply::NoError && !isBenignRemoteClose) {
        LLMError err = buildNetworkError(m_currentReply);
        qWarning() << "[DeepSeekProvider] API error:" << err.userMessage;
        emit errorOccurred(err);
    } else {
        // 结束思考状态（如果曾经启动过）
        if (!m_reasoningContent.isEmpty()) {
            emit reasoningStopped();
            // 在 streamComplete / toolCallsReceived 之前先发射 reasoning_content
            emit reasoningContentReady(m_reasoningContent);
        }

        if (m_lastFinishReason == QStringLiteral("tool_calls") && !m_streamingToolCallsJson.isEmpty()) {
            emit toolCallsReceived(mergeStreamingToolCalls(m_streamingToolCallsJson));
        } else {
            emit streamComplete(m_fullContent, LLMUsage());
        }
    }
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void DeepSeekProvider::parseStreamEventLine(const QByteArray& line)
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

    // DeepSeek R1 专有：思维链内容（reasoning_content）
    if (delta.contains("reasoning_content")) {
        const QString rc = delta["reasoning_content"].toString();
        if (!rc.isEmpty()) {
            // 第一个 reasoning_content 分片到达时，发射点亮 UI 的信号
            if (m_reasoningContent.isEmpty()) {
                emit reasoningStarted();
            }
            m_reasoningContent += rc;
        }
    }

    // 正文 content
    if (delta.contains("content")) {
        const QString content = delta["content"].toString();
        if (!content.isEmpty()) {
            m_fullContent += content;
            emit deltaReceived(content);
        }
    }

    // 工具调用
    if (delta.contains("tool_calls")) {
        QJsonArray toolCallsArray = delta["tool_calls"].toArray();
        for (const QJsonValue& tc : toolCallsArray)
            m_streamingToolCallsJson.append(tc);
    }
}

QJsonArray DeepSeekProvider::mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson)
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
