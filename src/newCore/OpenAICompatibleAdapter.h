#ifndef OPENAICOMPATIBLEADAPTER_H
#define OPENAICOMPATIBLEADAPTER_H

#include "LLMTypes.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

/**
 * @brief OpenAI 兼容 API 的 HTTP/SSE 适配层（设计文档 6.4 ModelAdapter）
 *
 * 不实现 ILLMClient，仅作为 LLMProvider 内部实现细节。
 * 负责：将 LLMRequest 转为 HTTP 请求体、发送请求、解析 SSE、通过信号回传 delta/complete/error。
 */
class OpenAICompatibleAdapter : public QObject {
    Q_OBJECT
public:
    explicit OpenAICompatibleAdapter(QObject* parent = nullptr);
    ~OpenAICompatibleAdapter() override;

    /**
     * @brief 发起流式请求
     * @param request 已组装的 LLMRequest（含 modelId、messages、tools 等）
     * @param apiKey API 密钥
     * @param baseUrl 如 "https://api.deepseek.com"
     * @param authType "Bearer" / "X-API-Key" / "api-key"
     * @param endpoint 如 "/chat/completions"
     */
    void startStream(const LLMRequest& request,
                     const QString& apiKey,
                     const QString& baseUrl,
                     const QString& authType = QStringLiteral("Bearer"),
                     const QString& endpoint = QStringLiteral("/chat/completions"));

    void abort();

signals:
    void deltaReceived(const QString& delta);
    void toolCallsReceived(const QJsonArray& toolCalls);
    void streamComplete(const QString& fullContent, const LLMUsage& usage);
    void errorOccurred(const LLMError& err);

private:
    QJsonObject buildRequestBody(const LLMRequest& request) const;
    void handleReadyRead();
    void handleFinished();
    void parseStreamEventLine(const QByteArray& line);
    QJsonArray mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson);

    QNetworkAccessManager* m_manager = nullptr;
    QNetworkReply* m_currentReply = nullptr;
    QTimer* m_timeoutTimer = nullptr;
    QString m_baseUrl;
    int m_timeoutMs = 180000;

    QString m_fullContent;
    QString m_lastFinishReason;
    QJsonArray m_streamingToolCallsJson;
};

#endif // OPENAICOMPATIBLEADAPTER_H
