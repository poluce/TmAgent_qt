#ifndef OPENAICOMPATIBLEPROVIDER_H
#define OPENAICOMPATIBLEPROVIDER_H

#include "LLMProvider.h"
#include "LLMTypes.h"
#include <QObject>
#include <QNetworkReply>

// -----------------------------------------------------------------------------
// OpenAICompatibleProvider：基于 OpenAI 兼容 API 的 LLMProvider 实现
// 直接实现 HTTP 通信、SSE 解析等协议细节，复用基类的网络管理器和超时控制。
// -----------------------------------------------------------------------------
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

    /**
     * @brief 应用模型配置
     * @param config 模型配置信息
     * 
     * 由 ModelFactory 在创建 Provider 后调用，注入配置信息
     */
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
    QJsonArray mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson);

    QString m_modelId;
    CapabilityDescriptor m_descriptor;
    ModelConfig m_config;  // 模型配置（由 applyConfig 注入）
    
    QNetworkReply* m_currentReply = nullptr;
    QString m_fullContent;
    QString m_lastFinishReason;
    QByteArray m_buffer;
    QJsonArray m_streamingToolCallsJson;
};

#endif // OPENAICOMPATIBLEPROVIDER_H
