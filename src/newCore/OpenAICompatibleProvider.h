#ifndef OPENAICOMPATIBLEPROVIDER_H
#define OPENAICOMPATIBLEPROVIDER_H

#include "LLMProvider.h"

class OpenAICompatibleProvider : public LLMProvider {
    Q_OBJECT
public:
    explicit OpenAICompatibleProvider(const QString& modelId, QObject* parent = nullptr);

    void generateStream(const LLMRequest& request) override;

private:
    void startStream(const LLMRequest& request, const QString& apiKey, const QString& baseUrl, const QString& authType);
    QJsonObject buildRequestBody(const LLMRequest& request) const;
    void handleReadyRead();
    void handleFinished();
    void parseStreamEventLine(const QByteArray& line);
    static QJsonArray mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson);

    QString m_lastFinishReason;
    QByteArray m_buffer;
    QJsonArray m_streamingToolCallsJson;
};

#endif // OPENAICOMPATIBLEPROVIDER_H
