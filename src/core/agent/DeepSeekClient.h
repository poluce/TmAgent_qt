#ifndef DEEPSEEKCLIENT_H
#define DEEPSEEKCLIENT_H

#include "ILLMClient.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

/**
 * @brief DeepSeek API 客户端实现
 * 
 * 专门负责 OpenAI 兼容格式（DeepSeek 扩展版）的通信细节。
 */
class DeepSeekClient : public ILLMClient {
    Q_OBJECT
public:
    explicit DeepSeekClient(QObject *parent = nullptr);
    ~DeepSeekClient() override;

    void postRequest(const LLMConfig& config, 
                    const QJsonArray& messages, 
                    const QList<Tool>& tools) override;
    
    void abort() override;

private:
    void handleReadyRead();
    void handleFinished();
    void parseStreamEventLine(const QByteArray& line);
    QJsonArray mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson);
    QJsonObject buildRequestBody(const LLMConfig& config, 
                                const QJsonArray& messages, 
                                const QList<Tool>& tools);

    QNetworkAccessManager *m_manager;
    QNetworkReply *m_currentReply = nullptr;
    QTimer *m_timeoutTimer;
    
    // 临时状态寄存器
    QString m_fullContent;
    QString m_lastFinishReason;
    QJsonArray m_streamingToolCallsJson;
};

#endif // DEEPSEEKCLIENT_H
