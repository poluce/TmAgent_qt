#include "AnthropicProvider.h"
#include <QDebug>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
QString buildAnthropicEndpoint(const QString& baseUrl)
{
    QString url = baseUrl.trimmed();
    if (url.isEmpty())
        url = QStringLiteral("https://api.anthropic.com");

    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);

    if (url.endsWith(QStringLiteral("/v1/messages"), Qt::CaseInsensitive))
        return url;
    if (url.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive))
        return url + QStringLiteral("/messages");
    return url + QStringLiteral("/v1/messages");
}

bool isRetriableHttpStatus(int status)
{
    switch (status) {
    case 408:
    case 429:
    case 500:
    case 502:
    case 503:
    case 504:
        return true;
    default:
        return false;
    }
}
} // namespace

AnthropicProvider::AnthropicProvider(const QString& modelId, QObject* parent)
    : LLMProvider(modelId, parent)
{
}

void AnthropicProvider::generateStream(const LLMRequest& request)
{
    QString apiKey = m_config.apiKey;
    QString baseUrl = m_config.baseUrl;
    if (baseUrl.isEmpty())
        baseUrl = QStringLiteral("https://api.anthropic.com");

    m_lastRequest = request;
    m_lastApiKey = apiKey;
    m_lastBaseUrl = baseUrl;
    m_retryAttempt = 0;

    startStream(request, apiKey, baseUrl);
}

void AnthropicProvider::startStream(const LLMRequest& request, const QString& apiKey, const QString& baseUrl)
{
    abort();

    m_fullContent.clear();
    m_stopReason.clear();
    m_toolUseBlocks = QJsonArray();
    m_toolUseIndexToBlockPos.clear();
    m_buffer.clear();
    m_timeoutMs = request.timeoutMs > 0 ? request.timeoutMs : 180000;

    QJsonObject root = buildRequestBody(request);

    const QString urlStr = buildAnthropicEndpoint(baseUrl);

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

    root["messages"] = convertMessagesToAnthropic(request.messages);

    for (const QJsonValue& msg : request.messages) {
        QJsonObject msgObj = msg.toObject();
        if (msgObj["role"].toString() == "system") {
            root["system"] = msgObj["content"].toString();
            break;
        }
    }

    if (!request.tools.isEmpty())
        root["tools"] = convertToolsToAnthropic(request.tools);

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
                    contentBlocks.append(QJsonObject { { "type", "text" }, { "text", blockObj["text"].toString() } });
                } else if (type == "image_url") {
                    QJsonObject source;
                    source["type"] = "url";
                    source["url"] = blockObj["image_url"].toObject()["url"].toString();
                    contentBlocks.append(QJsonObject { { "type", "image" }, { "source", source } });
                }
            }
            anthropicMsg["content"] = contentBlocks;
        }

        if (msg.contains("tool_calls")) {
            QJsonArray contentBlocks;
            if (!content.toString().isEmpty())
                contentBlocks.append(QJsonObject { { "type", "text" }, { "text", content.toString() } });

            for (const QJsonValue& tc : msg["tool_calls"].toArray()) {
                QJsonObject tcObj = tc.toObject();
                QJsonObject toolUseBlock;
                toolUseBlock["type"] = "tool_use";
                toolUseBlock["id"] = tcObj["id"].toString();
                toolUseBlock["name"] = tcObj["function"].toObject()["name"].toString();
                const QJsonValue argsValue = tcObj["function"].toObject()["arguments"];
                if (argsValue.isObject()) {
                    toolUseBlock["input"] = argsValue.toObject();
                } else {
                    const QString argsStr = argsValue.toString();
                    toolUseBlock["input"] = QJsonDocument::fromJson(argsStr.toUtf8()).object();
                }
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
            anthropicMsg["content"] = QJsonArray { toolResultBlock };
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
    m_timeoutTimer->stop();
    if (!m_currentReply)
        return;

    if (!m_buffer.isEmpty()) {
        parseStreamEventLine(m_buffer.trimmed());
        m_buffer.clear();
    }

    const QNetworkReply::NetworkError netErr = m_currentReply->error();
    const bool hasContent = !m_toolUseBlocks.isEmpty()
        || !m_fullContent.isEmpty()
        || !m_stopReason.isEmpty();
    const bool isBenignRemoteClose = (netErr == QNetworkReply::RemoteHostClosedError) && hasContent;

    if (netErr != QNetworkReply::NoError && !isBenignRemoteClose) {
        if (shouldRetryOnFailure(m_currentReply, netErr, hasContent) && m_retryAttempt < m_maxRetryAttempts) {
            const QVariant statusVar = m_currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const int httpStatus = statusVar.isValid() ? statusVar.toInt() : -1;
            ++m_retryAttempt;
            qWarning() << "[AnthropicProvider] transient failure, retry"
                       << m_retryAttempt << "/" << m_maxRetryAttempts
                       << "status=" << httpStatus
                       << "error=" << m_currentReply->errorString();

            m_currentReply->deleteLater();
            m_currentReply = nullptr;

            startStream(m_lastRequest, m_lastApiKey, m_lastBaseUrl);
            return;
        }

        LLMError err = buildNetworkError(m_currentReply);
        emit errorOccurred(err);
    } else {
        m_retryAttempt = 0;
        if (m_stopReason == QStringLiteral("tool_use") && !m_toolUseBlocks.isEmpty()) {
            QJsonArray toolCalls;
            for (int i = 0; i < m_toolUseBlocks.size(); ++i) {
                const QJsonObject blockObj = m_toolUseBlocks.at(i).toObject();
                QJsonObject func;
                func[QStringLiteral("name")] = blockObj.value(QStringLiteral("name")).toString();
                func[QStringLiteral("arguments")] = QString::fromUtf8(
                    QJsonDocument(blockObj.value(QStringLiteral("input")).toObject())
                        .toJson(QJsonDocument::Compact));
                toolCalls.append(QJsonObject { { QStringLiteral("id"), blockObj.value(QStringLiteral("id")).toString() }, { QStringLiteral("type"), QStringLiteral("function") }, { QStringLiteral("function"), func } });
            }
            emit toolCallsReceived(toolCalls);
        } else {
            emit streamComplete(m_fullContent, LLMUsage());
        }
    }

    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

bool AnthropicProvider::shouldRetryOnFailure(QNetworkReply* reply, QNetworkReply::NetworkError netErr, bool hasContent) const
{
    if (!reply)
        return false;

    const QVariant statusVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int httpStatus = statusVar.isValid() ? statusVar.toInt() : -1;
    if (isRetriableHttpStatus(httpStatus))
        return true;

    switch (netErr) {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::ProxyTimeoutError:
    case QNetworkReply::ServiceUnavailableError:
    case QNetworkReply::UnknownServerError:
    case QNetworkReply::InternalServerError:
        return true;
    case QNetworkReply::RemoteHostClosedError:
        return !hasContent;
    default:
        break;
    }

    const QString errText = reply->errorString();
    if (errText.contains(QStringLiteral("Bad Gateway"), Qt::CaseInsensitive)
        || errText.contains(QStringLiteral("Internal Server Error"), Qt::CaseInsensitive)
        || errText.contains(QStringLiteral("Service Unavailable"), Qt::CaseInsensitive)) {
        return true;
    }

    return false;
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

    if (type == "content_block_start") {
        QJsonObject contentBlock = obj["content_block"].toObject();
        if (contentBlock["type"].toString() == "tool_use") {
            const int contentIndex = obj.value(QStringLiteral("index")).toInt(-1);
            const int blockPos = m_toolUseBlocks.size();
            m_toolUseBlocks.append(contentBlock);
            if (contentIndex >= 0)
                m_toolUseIndexToBlockPos.insert(contentIndex, blockPos);
        }
    } else if (type == "content_block_delta") {
        QJsonObject delta = obj["delta"].toObject();
        QString deltaType = delta["type"].toString();

        if (deltaType == "text_delta") {
            QString text = delta["text"].toString();
            m_fullContent += text;
            emit deltaReceived(text);
        } else if (deltaType == "input_json_delta") {
            const int contentIndex = obj.value(QStringLiteral("index")).toInt(-1);
            const int blockPos = m_toolUseIndexToBlockPos.value(contentIndex, -1);
            if (blockPos >= 0 && blockPos < m_toolUseBlocks.size()) {
                QJsonObject block = m_toolUseBlocks[blockPos].toObject();
                block["partial_input"] = block["partial_input"].toString() + delta["partial_json"].toString();
                m_toolUseBlocks[blockPos] = block;
            }
        }
    } else if (type == "content_block_stop") {
        const int contentIndex = obj.value(QStringLiteral("index")).toInt(-1);
        const int blockPos = m_toolUseIndexToBlockPos.value(contentIndex, -1);
        if (blockPos >= 0 && blockPos < m_toolUseBlocks.size()) {
            QJsonObject block = m_toolUseBlocks[blockPos].toObject();
            QString partialInput = block["partial_input"].toString();
            if (!partialInput.isEmpty()) {
                block["input"] = QJsonDocument::fromJson(partialInput.toUtf8()).object();
                block.remove("partial_input");
                m_toolUseBlocks[blockPos] = block;
            }
        }
    } else if (type == "message_delta") {
        QJsonObject delta = obj["delta"].toObject();
        if (delta.contains("stop_reason"))
            m_stopReason = delta["stop_reason"].toString();
    } else if (type == "error") {
        QJsonObject error = obj["error"].toObject();
        qWarning() << "[AnthropicProvider] Error:" << error["message"].toString();
    }
}
