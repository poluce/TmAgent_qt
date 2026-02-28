#ifndef LLMPROVIDER_H
#define LLMPROVIDER_H

#include "LLMTypes.h"
#include <QNetworkReply>
#include <QObject>

class QNetworkAccessManager;
class QTimer;

/**
 * @brief 统一模型调用抽象
 *
 * 上层唯一依赖的模型调用接口。
 * 接收已组装好的请求，提供非流式与流式两套入口，返回标准化结果与错误。
 * 基类提供通用的超时、abort、配置应用等逻辑，子类只需实现协议差异部分。
 */
class LLMProvider : public QObject {
    Q_OBJECT
public:
    explicit LLMProvider(const QString& modelId, QObject* parent = nullptr);
    ~LLMProvider() override;

    LLMResponse generate(const LLMRequest& request);
    virtual void generateStream(const LLMRequest& request) = 0;
    bool supports(const QString& capability) const;
    CapabilityDescriptor descriptor() const;
    void abort();

    void applyConfig(const ModelConfig& config);

    /**
     * @brief 异步拉取该接入点可用的模型列表（GET /v1/models）
     *
     * 默认实现适用于 OpenAI 兼容接口。子类可覆盖。
     * 结果通过 modelListReceived 信号返回。
     */
    virtual void fetchModelList();

signals:
    void deltaReceived(const QString& delta);
    void toolCallsReceived(const QJsonArray& toolCalls);
    void streamComplete(const QString& fullContent, const LLMUsage& usage);
    void errorOccurred(const LLMError& err);
    void modelListReceived(const QList<AvailableModel>& models);

    // UI 指示器相关：深度思考状态通知
    void reasoningStarted();
    void reasoningStopped();

    /**
     * @brief 提取模型返回的额外思维链/推理过程（如 DeepSeek R1 的 reasoning_content）
     *
     * 适用于所有支持深度思考/思维链过程并要求将此过程回传给 API 的模型。
     * 必须在 streamComplete / toolCallsReceived 之前发射本信号，以便上层（LLMAgent）
     * 收到此信号后可以同步暂存该内容，并在生成 assistant 历史记录时将其拼接进去，
     * 确保下一轮请求能携带正确的上下文。
     */
    void reasoningContentReady(const QString& reasoningContent);

protected:
    void emitTimeoutError();
    void abortAndCleanReply();
    LLMError buildNetworkError(QNetworkReply* reply) const;

    QNetworkAccessManager* m_manager = nullptr;
    QTimer* m_timeoutTimer = nullptr;
    int m_timeoutMs = 180000;

    QString m_modelId;
    CapabilityDescriptor m_descriptor;
    ModelConfig m_config;
    QNetworkReply* m_currentReply = nullptr;
    QString m_fullContent;
};

#endif // LLMPROVIDER_H
