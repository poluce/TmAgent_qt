#ifndef ANTHROPICPROVIDER_H
#define ANTHROPICPROVIDER_H

#include "LLMProvider.h"

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

    QString m_stopReason;
    QByteArray m_buffer;
    QJsonArray m_toolUseBlocks;
};

#endif // ANTHROPICPROVIDER_H
