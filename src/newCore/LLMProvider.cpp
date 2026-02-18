#include "LLMProvider.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>

LLMProvider::LLMProvider(const QString& modelId, QObject* parent)
    : QObject(parent)
    , m_modelId(modelId)
{
    m_manager = new QNetworkAccessManager(this);
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);

    m_descriptor.modelId = modelId;
    m_descriptor.capabilities << Capability::TextGeneration << Capability::ToolCalling;
    m_descriptor.toolCalling = true;

    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_currentReply) {
            m_currentReply->abort();
            emitTimeoutError();
        }
    });
}

LLMProvider::~LLMProvider()
{
    abort();
}

LLMResponse LLMProvider::generate(const LLMRequest& request)
{
    Q_UNUSED(request);
    LLMResponse out;
    out.error.errorCode = LLMErrorCode::Unknown;
    out.error.userMessage = tr("本 Provider 仅支持流式调用，请使用 generateStream。");
    return out;
}

bool LLMProvider::supports(const QString& capability) const
{
    return m_descriptor.supports(capability);
}

CapabilityDescriptor LLMProvider::descriptor() const
{
    return m_descriptor;
}

void LLMProvider::applyConfig(const ModelConfig& config)
{
    m_config = config;
    m_descriptor.capabilities = config.capabilities;
    m_descriptor.toolCalling = config.toolCalling;
    m_descriptor.contextLength = config.contextLength;
}

void LLMProvider::abort()
{
    if (m_currentReply) {
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    if (m_timeoutTimer)
        m_timeoutTimer->stop();
}

void LLMProvider::emitTimeoutError()
{
    LLMError err;
    err.errorCode = LLMErrorCode::Timeout;
    err.userMessage = tr("网络请求超时");
    emit errorOccurred(err);
}

LLMError LLMProvider::buildNetworkError(QNetworkReply* reply) const
{
    LLMError err;
    err.errorCode = LLMErrorCode::ProtocolError;
    err.userMessage = reply->errorString();

    QByteArray responseBody = reply->readAll();
    if (!responseBody.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(responseBody);
        if (!doc.isNull()) {
            QJsonObject rootObj = doc.object();
            QJsonObject errObj = rootObj[QStringLiteral("error")].toObject();
            if (!errObj.isEmpty()) {
                QString apiMessage = errObj[QStringLiteral("message")].toString();
                if (!apiMessage.isEmpty())
                    err.userMessage = apiMessage;
                err.diagnostics[QStringLiteral("api_error_type")] = errObj[QStringLiteral("type")].toString();
            }
            err.diagnostics[QStringLiteral("response_body")] = QString::fromUtf8(responseBody);
        } else {
            err.userMessage = reply->errorString()
                + QStringLiteral(" | Body: ") + QString::fromUtf8(responseBody.left(500));
        }
    }

    err.diagnostics[QStringLiteral("qt_network_error")] = static_cast<int>(reply->error());
    return err;
}
