#include "LLMProvider.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
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

    const QVariant statusVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (statusVar.isValid())
        err.diagnostics[QStringLiteral("http_status")] = statusVar.toInt();

    err.diagnostics[QStringLiteral("qt_network_error")] = static_cast<int>(reply->error());
    return err;
}

void LLMProvider::fetchModelList()
{
    QString baseUrl = m_config.baseUrl.trimmed();
    if (baseUrl.isEmpty())
        baseUrl = QStringLiteral("https://api.openai.com");
    if (baseUrl.endsWith(QLatin1Char('/')))
        baseUrl.chop(1);

    const QUrl url(baseUrl + QStringLiteral("/v1/models"));
    QNetworkRequest req(url);
    req.setRawHeader("Content-Type", "application/json");

    const QString apiKey = m_config.apiKey.trimmed();
    if (!apiKey.isEmpty()) {
        const QString authType = m_config.authType.trimmed().toLower();
        if (authType == QStringLiteral("x-api-key"))
            req.setRawHeader("x-api-key", apiKey.toUtf8());
        else
            req.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
    }

    QNetworkReply* reply = m_manager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QList<AvailableModel> models;

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "LLMProvider::fetchModelList failed:" << reply->errorString();
            emit modelListReceived(models);
            return;
        }

        const QByteArray body = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QJsonArray data = doc.object().value(QStringLiteral("data")).toArray();

        for (const QJsonValue& val : data) {
            const QJsonObject obj = val.toObject();
            AvailableModel m;
            m.modelId = obj.value(QStringLiteral("id")).toString().trimmed();
            if (m.modelId.isEmpty())
                continue;
            // 部分 API 返回 display_name 或 name
            m.displayName = obj.value(QStringLiteral("display_name")).toString().trimmed();
            if (m.displayName.isEmpty())
                m.displayName = obj.value(QStringLiteral("name")).toString().trimmed();
            models.append(m);
        }

        emit modelListReceived(models);
    });
}
