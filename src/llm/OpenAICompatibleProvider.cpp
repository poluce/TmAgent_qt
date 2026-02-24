#include "OpenAICompatibleProvider.h"
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QUrl>

OpenAICompatibleProvider::OpenAICompatibleProvider(const QString& modelId, QObject* parent)
    : LLMProvider(modelId, parent)
{
}

void OpenAICompatibleProvider::generateStream(const LLMRequest& request)
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

void OpenAICompatibleProvider::startStream(const LLMRequest& request, const QString& apiKey, const QString& baseUrl, const QString& authType)
{
    abort();

    m_fullContent.clear();
    m_lastFinishReason.clear();
    m_streamingToolCallsJson = QJsonArray();
    m_timeoutMs = request.timeoutMs > 0 ? request.timeoutMs : 180000;

    QJsonObject root = buildRequestBody(request);

    if (root.contains("tools")) {
        QJsonArray toolsArr = root["tools"].toArray();
        qDebug() << "[OpenAICompatibleProvider] Sending" << toolsArr.size() << "tools";
        qDebug().noquote() << "[OpenAICompatibleProvider] Request body size:"
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
        qWarning() << "[OpenAICompatibleProvider] API error:" << err.userMessage;
        emit errorOccurred(err);
    } else {
        if (m_lastFinishReason == QStringLiteral("tool_calls") && !m_streamingToolCallsJson.isEmpty()) {
            emit toolCallsReceived(mergeStreamingToolCalls(m_streamingToolCallsJson));
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
        if (!content.isEmpty()) {
            m_fullContent += content;
            emit deltaReceived(content);
        }
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
