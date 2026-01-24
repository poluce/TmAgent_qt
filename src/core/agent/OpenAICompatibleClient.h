#ifndef OPENAICOMPATIBLECLIENT_H
#define OPENAICOMPATIBLECLIENT_H

#include "ILLMClient.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

/**
 * @brief OpenAI 兼容接口客户端实现
 *
 * 通用客户端，支持 DeepSeek, OpenAI, Ollama, Moonshot, Aliyun 等
 * 所有兼容 OpenAI API 格式（Chat Completion）的服务商。
 */
class OpenAICompatibleClient : public ILLMClient {
    Q_OBJECT
public:
    explicit OpenAICompatibleClient(QObject* parent = nullptr);
    ~OpenAICompatibleClient() override;

    void postRequest(const LLMConfig& config, const QJsonArray& messages, const QList<Tool>& tools) override;

    void abort() override;

private:
    void handleReadyRead();
    void handleFinished();
    void parseStreamEventLine(const QByteArray& line);
    QJsonArray mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson);
    QJsonObject buildRequestBody(const LLMConfig& config, const QJsonArray& messages, const QList<Tool>& tools);

    QNetworkAccessManager* m_manager;
    QNetworkReply* m_currentReply = nullptr;
    QTimer* m_timeoutTimer;

    // 临时状态寄存器
    QString m_fullContent;
    QString m_lastFinishReason;
    QJsonArray m_streamingToolCallsJson;
};

#endif // DEEPSEEKCLIENT_H
