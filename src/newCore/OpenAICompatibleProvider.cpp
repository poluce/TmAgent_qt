#include "OpenAICompatibleProvider.h"
#include "OpenAICompatibleAdapter.h"
#include "core/utils/AppSettings.h"
#include <QCoreApplication>

OpenAICompatibleProvider::OpenAICompatibleProvider(const QString& modelId, QObject* parent)
    : LLMProvider(parent)
    , m_modelId(modelId)
{
    m_adapter = new OpenAICompatibleAdapter(this);
    m_descriptor.modelId = modelId;
    m_descriptor.capabilities << Capability::TextGeneration << Capability::ToolCalling;
    m_descriptor.toolCalling = true;

    connect(m_adapter, &OpenAICompatibleAdapter::deltaReceived,
            this, &OpenAICompatibleProvider::deltaReceived);
    connect(m_adapter, &OpenAICompatibleAdapter::toolCallsReceived,
            this, &OpenAICompatibleProvider::toolCallsReceived);
    connect(m_adapter, &OpenAICompatibleAdapter::streamComplete,
            this, &OpenAICompatibleProvider::streamComplete);
    connect(m_adapter, &OpenAICompatibleAdapter::errorOccurred,
            this, &OpenAICompatibleProvider::errorOccurred);
}

OpenAICompatibleProvider::~OpenAICompatibleProvider() = default;

LLMResponse OpenAICompatibleProvider::generate(const LLMRequest& request)
{
    Q_UNUSED(request);
    LLMResponse out;
    out.error.errorCode = LLMErrorCode::Unknown;
    out.error.userMessage = tr("本 Provider 仅支持流式调用，请使用 generateStream。");
    return out;
}

void OpenAICompatibleProvider::generateStream(const LLMRequest& request)
{
    QString apiKey = AppSettings::getApiKey();
    QString baseUrl = AppSettings::getBaseUrl();
    if (baseUrl.isEmpty())
        baseUrl = QStringLiteral("https://api.deepseek.com");

    m_adapter->startStream(request, apiKey, baseUrl,
                           QStringLiteral("Bearer"),
                           QStringLiteral("/chat/completions"));
}

bool OpenAICompatibleProvider::supports(const QString& capability) const
{
    return m_descriptor.supports(capability);
}

CapabilityDescriptor OpenAICompatibleProvider::descriptor() const
{
    return m_descriptor;
}

void OpenAICompatibleProvider::abort()
{
    m_adapter->abort();
}
