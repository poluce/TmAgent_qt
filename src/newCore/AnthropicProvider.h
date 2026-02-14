#ifndef ANTHROPICPROVIDER_H
#define ANTHROPICPROVIDER_H

#include "LLMProvider.h"
#include <QNetworkReply>

class AnthropicProvider : public LLMProvider {
    Q_OBJECT
public:
    explicit AnthropicProvider(const QString& modelId, QObject* parent = nullptr);
    ~AnthropicProvider() override;

    LLMResponse generate(const LLMRequest& request) override;
    void generateStream(const LLMRequest& request) override;
    bool supports(const QString& capability) const override;
    CapabilityDescriptor descriptor() const override;
    void abort() override;

    void applyConfig(const ModelConfig& config);

private:
    void startStream(const LLMRequest& request,
                     const QString& apiKey,
                     const QString& baseUrl);
    QJsonObject buildRequestBody(const LLMRequest& request) const;
    QJsonArray convertMessagesToAnthropic(const QJsonArray& openaiMessages) const;
    QJsonArray convertToolsToAnthropic(const QJsonArray& openaiTools) const;
    void handleReadyRead();
    void handleFinished();
    void parseStreamEventLine(const QByteArray& line);

    QString m_modelId;
    CapabilityDescriptor m_descriptor;
    ModelConfig m_config;

    QNetworkReply* m_currentReply = nullptr;
    QString m_fullContent;
    QString m_stopReason;
    QByteArray m_buffer;
    QJsonArray m_toolUseBlocks;
};

#endif // ANTHROPICPROVIDER_H
