#ifndef OPENAICOMPATIBLEPROVIDER_H
#define OPENAICOMPATIBLEPROVIDER_H

#include "LLMProvider.h"
#include "LLMTypes.h"
#include <QObject>

class OpenAICompatibleAdapter;

/**
 * @brief 基于 OpenAI 兼容 API 的 LLMProvider 实现
 *
 * 内部使用 OpenAICompatibleAdapter 发 HTTP/SSE，不依赖 ILLMClient。
 * 配置从 AppSettings 读取（apiKey、baseUrl），request.modelId 用作本次模型名。
 */
class OpenAICompatibleProvider : public LLMProvider {
    Q_OBJECT
public:
    /**
     * @param modelId 本 Provider 对应的模型标识，与 Factory 注册时的 descriptor.modelId 一致
     */
    explicit OpenAICompatibleProvider(const QString& modelId, QObject* parent = nullptr);
    ~OpenAICompatibleProvider() override;

    LLMResponse generate(const LLMRequest& request) override;
    void generateStream(const LLMRequest& request) override;
    bool supports(const QString& capability) const override;
    CapabilityDescriptor descriptor() const override;
    void abort() override;

private:
    QString m_modelId;
    CapabilityDescriptor m_descriptor;
    OpenAICompatibleAdapter* m_adapter = nullptr;
};

#endif // OPENAICOMPATIBLEPROVIDER_H
