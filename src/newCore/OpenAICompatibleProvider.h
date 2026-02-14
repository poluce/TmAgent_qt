#ifndef OPENAICOMPATIBLEPROVIDER_H
#define OPENAICOMPATIBLEPROVIDER_H

#include "LLMProvider.h"
#include <QNetworkReply>

class OpenAICompatibleProvider : public LLMProvider {
    Q_OBJECT
public:
    explicit OpenAICompatibleProvider(const QString& modelId, QObject* parent = nullptr);
    ~OpenAICompatibleProvider() override;

    LLMResponse generate(const LLMRequest& request) override;
    void generateStream(const LLMRequest& request) override;
    bool supports(const QString& capability) const override;
    CapabilityDescriptor descriptor() const override;
    void abort() override;

    void applyConfig(const ModelConfig& config);

private:
    void startStream(const LLMRequest& request,
                     const QString& apiKey,
                     const QString& baseUrl,
                     const QString& authType = QStringLiteral("Bearer"),
                     const QString& endpoint = QStringLiteral("/chat/completions"));
    QJsonObject buildRequestBody(const LLMRequest& request) const;
    void handleReadyRead();
    void handleFinished();
    void parseStreamEventLine(const QByteArray& line);
    static QJsonArray mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson);

    QString m_modelId;
    CapabilityDescriptor m_descriptor;
    ModelConfig m_config;

    QNetworkReply* m_currentReply = nullptr;
    QString m_fullContent;
    QString m_lastFinishReason;
    QByteArray m_buffer;
    QJsonArray m_streamingToolCallsJson;
};

#endif // OPENAICOMPATIBLEPROVIDER_H
