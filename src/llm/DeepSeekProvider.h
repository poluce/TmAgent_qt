#ifndef DEEPSEEEPROVIDER_H
#define DEEPSEEEPROVIDER_H

#include "LLMProvider.h"

/**
 * @brief DeepSeek 专用 Provider
 *
 * 完全独立实现，不依赖 OpenAICompatibleProvider。
 * 主要差异：
 *   1. 流式解析时提取 reasoning_content（思维链内容）
 *   2. buildRequestBody 回填历史消息中的 reasoning_content 字段
 *      （DeepSeek R1 要求：如响应中含此字段，下次请求历史中的 assistant 消息必须携带该字段）
 *   3. 流式完成时通过 reasoningContentReady 信号将思维链内容传递给上层
 */
class DeepSeekProvider : public LLMProvider {
    Q_OBJECT
public:
    explicit DeepSeekProvider(const QString& modelId, QObject* parent = nullptr);

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

    // DeepSeek reasoning_content 累积缓冲
    QString m_reasoningContent;
};

#endif // DEEPSEEEPROVIDER_H
