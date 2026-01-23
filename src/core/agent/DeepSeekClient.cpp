#include "DeepSeekClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QDebug>

DeepSeekClient::DeepSeekClient(QObject *parent) : ILLMClient(parent) {
    m_manager = new QNetworkAccessManager(this);
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_currentReply) {
            m_currentReply->abort();
            emit errorOccurred("网络请求超时");
        }
    });
}

DeepSeekClient::~DeepSeekClient() {
    abort();
}

void DeepSeekClient::postRequest(const LLMConfig& config, 
                               const QJsonArray& messages, 
                               const QList<Tool>& tools) {
    abort(); // 清理旧请求

    m_fullContent.clear();
    m_lastFinishReason.clear();
    m_streamingToolCallsJson = QJsonArray();

    QJsonObject root = buildRequestBody(config, messages, tools);
    
    QUrl url(config.baseUrl + config.endpoint);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // 认证处理
    QString authValue;
    if (config.authType == "X-API-Key") {
        request.setRawHeader("X-API-Key", config.apiKey.toUtf8());
    } else if (config.authType == "api-key") {
        request.setRawHeader("api-key", config.apiKey.toUtf8());
    } else {
        request.setRawHeader("Authorization", QString("Bearer %1").arg(config.apiKey).toUtf8());
    }

    m_currentReply = m_manager->post(request, QJsonDocument(root).toJson());
    
    m_timeoutTimer->start(config.timeoutMs);

    connect(m_currentReply, &QNetworkReply::readyRead, this, &DeepSeekClient::handleReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &DeepSeekClient::handleFinished);
}

void DeepSeekClient::abort() {
    if (m_currentReply) {
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_timeoutTimer->stop();
}

void DeepSeekClient::handleReadyRead() {
    if (!m_currentReply) return;
    while (m_currentReply->canReadLine()) {
        QByteArray line = m_currentReply->readLine().trimmed();
        if (!line.isEmpty()) {
            parseStreamEventLine(line);
        }
    }
}

void DeepSeekClient::handleFinished() {
    m_timeoutTimer->stop();
    if (!m_currentReply) return;

    if (m_currentReply->error() != QNetworkReply::NoError) {
        emit errorOccurred(m_currentReply->errorString());
    } else {
        // 如果是工具调用结束
        if (m_lastFinishReason == "tool_calls" && !m_streamingToolCallsJson.isEmpty()) {
            QJsonArray assembledToolCalls = mergeStreamingToolCalls(m_streamingToolCallsJson);
            emit toolCallsReceived(assembledToolCalls);
        }
        emit finished(m_fullContent);
    }

    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void DeepSeekClient::parseStreamEventLine(const QByteArray& line) {
    if (!line.startsWith("data: ")) return;
    
    QString data = QString::fromUtf8(line.mid(6));
    if (data == "[DONE]") return;
    
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (doc.isNull()) return;
    
    QJsonObject obj = doc.object();
    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) return;
    
    QJsonObject choice = choices[0].toObject();
    QJsonObject delta = choice["delta"].toObject();
    
    if (choice.contains("finish_reason") && !choice["finish_reason"].isNull()) {
        m_lastFinishReason = choice["finish_reason"].toString();
    }
    
    if (delta.contains("content")) {
        QString content = delta["content"].toString();
        m_fullContent += content;
        if (!content.isEmpty()) emit deltaReceived(content);
    }
    
    if (delta.contains("tool_calls")) {
        QJsonArray toolCallsArray = delta["tool_calls"].toArray();
        for (const QJsonValue& tc : toolCallsArray) {
            m_streamingToolCallsJson.append(tc);
        }
    }
}

QJsonArray DeepSeekClient::mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson) {
    QMap<int, QJsonObject> toolCallsMap;
    for (const QJsonValue& tcVal : streamingToolCallsJson) {
        const QJsonObject toolObject = tcVal.toObject();
        const int index = toolObject["index"].toInt();
        QJsonObject& current = toolCallsMap[index];
        if (toolObject.contains("id")) current["id"] = toolObject["id"];
        if (toolObject.contains("type")) current["type"] = toolObject["type"];
        
        const QJsonObject funcObj = toolObject["function"].toObject();
        if (!funcObj.isEmpty()) {
            QJsonObject currentFunc = current["function"].toObject();
            if (funcObj.contains("name")) currentFunc["name"] = funcObj["name"];
            if (funcObj.contains("arguments")) {
                currentFunc["arguments"] = currentFunc["arguments"].toString() + funcObj["arguments"].toString();
            }
            current["function"] = currentFunc;
        }
    }
    
    QJsonArray result;
    for (const QJsonObject& tc : toolCallsMap.values()) {
        result.append(tc);
    }
    return result;
}

QJsonObject DeepSeekClient::buildRequestBody(const LLMConfig& config, 
                                           const QJsonArray& messages, 
                                           const QList<Tool>& tools) {
    QJsonObject root;
    root["model"] = config.model;
    root["max_tokens"] = config.maxTokens;
    root["temperature"] = config.temperature;
    root["stream"] = true;
    root["messages"] = messages;

    if (!tools.isEmpty()) {
        QJsonArray toolsArray;
        for (const Tool& tool : tools) {
            toolsArray.append(tool.toJson());
        }
        root["tools"] = toolsArray;
    }
    return root;
}
