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

signals:
    void deltaReceived(const QString& delta);
    void toolCallsReceived(const QJsonArray& toolCalls);
    void streamComplete(const QString& fullContent, const LLMUsage& usage);
    void errorOccurred(const LLMError& err);

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
