#ifndef LLMPROVIDER_H
#define LLMPROVIDER_H

#include "LLMTypes.h"
#include <QObject>

class QNetworkAccessManager;
class QTimer;

/**
 * @brief 统一模型调用抽象
 *
 * 上层唯一依赖的模型调用接口。
 * 接收已组装好的请求，提供非流式与流式两套入口，返回标准化结果与错误。
 */
class LLMProvider : public QObject {
    Q_OBJECT
public:
    explicit LLMProvider(QObject* parent = nullptr);
    ~LLMProvider() override;

    virtual LLMResponse generate(const LLMRequest& request) = 0;
    virtual void generateStream(const LLMRequest& request) = 0;
    virtual bool supports(const QString& capability) const = 0;
    virtual CapabilityDescriptor descriptor() const = 0;
    virtual void abort() = 0;

signals:
    void deltaReceived(const QString& delta);
    void toolCallsReceived(const QJsonArray& toolCalls);
    void streamComplete(const QString& fullContent, const LLMUsage& usage);
    void errorOccurred(const LLMError& err);

protected:
    QNetworkAccessManager* m_manager = nullptr;
    QTimer* m_timeoutTimer = nullptr;
    int m_timeoutMs = 180000;
};

#endif // LLMPROVIDER_H
