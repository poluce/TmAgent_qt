#ifndef ANTHROPICPROVIDER_H
#define ANTHROPICPROVIDER_H

#include "LLMProvider.h"
#include <QHash>

class AnthropicProvider : public LLMProvider {
    Q_OBJECT
public:
    explicit AnthropicProvider(const QString& modelId, QObject* parent = nullptr);

    void generateStream(const LLMRequest& request) override;

private:
    void startStream(const LLMRequest& request, const QString& apiKey, const QString& baseUrl);
    QJsonObject buildRequestBody(const LLMRequest& request) const;
    QJsonArray convertMessagesToAnthropic(const QJsonArray& openaiMessages) const;
    QJsonArray convertToolsToAnthropic(const QJsonArray& openaiTools) const;
    void handleReadyRead();
    void handleFinished();
    void parseStreamEventLine(const QByteArray& line);
    bool shouldRetryOnFailure(QNetworkReply* reply, QNetworkReply::NetworkError netErr, bool hasContent) const;

    QString m_stopReason;
    QByteArray m_buffer;
    QJsonArray m_toolUseBlocks;
    QHash<int, int> m_toolUseIndexToBlockPos;

    // 自动重试仅用于瞬时错误（5xx/网关抖动），避免对子代理链路造成硬中断。
    LLMRequest m_lastRequest;
    QString m_lastApiKey;
    QString m_lastBaseUrl;
    int m_retryAttempt = 0;
    int m_maxRetryAttempts = 2;
};

#endif // ANTHROPICPROVIDER_H
