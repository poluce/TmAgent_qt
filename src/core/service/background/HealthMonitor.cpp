#include "HealthMonitor.h"

#include "AgentRuntime.h"
#include "RuntimeManager.h"
#include "llm/ModelFactory.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrl>

namespace {
QString normalizeProviderId(const QString& providerId)
{
    return providerId.trimmed();
}

} // namespace

HealthMonitor::HealthMonitor(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_network(new QNetworkAccessManager(this))
{
    m_timer->setInterval(30000);
    connect(m_timer, &QTimer::timeout, this, &HealthMonitor::onCheckTick);
}

HealthMonitor::~HealthMonitor() = default;

void HealthMonitor::setRuntimeManager(RuntimeManager* runtimeManager)
{
    m_runtimeManager = runtimeManager;
}

void HealthMonitor::setModelFactory(ModelFactory* modelFactory)
{
    m_modelFactory = modelFactory;
}

void HealthMonitor::setIntervalMs(int intervalMs)
{
    m_timer->setInterval(qMax(1000, intervalMs));
}

int HealthMonitor::intervalMs() const
{
    return m_timer->interval();
}

void HealthMonitor::start()
{
    if (!m_timer->isActive())
        m_timer->start();
    onCheckTick();
}

void HealthMonitor::stop()
{
    if (m_timer->isActive())
        m_timer->stop();
}

HealthMonitor::State HealthMonitor::providerState(const QString& configId) const
{
    return m_providerStates.value(normalizeProviderId(configId), Unknown);
}

HealthMonitor::State HealthMonitor::mcpState(const QString& serverId) const
{
    return m_mcpStates.value(serverId.trimmed(), Unknown);
}

HealthMonitor::State HealthMonitor::lspState() const
{
    return m_lspState;
}

bool HealthMonitor::isProviderDown(const QString& configId) const
{
    return providerState(configId) == Down;
}

void HealthMonitor::onCheckTick()
{
    if (!m_runtimeManager || !m_modelFactory)
        return;

    QSet<QString> providerIds;
    const QHash<QString, AgentRuntime*>& runtimes = m_runtimeManager->runtimes();
    for (auto it = runtimes.constBegin(); it != runtimes.constEnd(); ++it) {
        const AgentRuntime* runtime = it.value();
        if (!runtime)
            continue;
        const QString providerId = ModelFactory::resolveInstanceId(runtime->config());
        if (!providerId.trimmed().isEmpty())
            providerIds.insert(providerId.trimmed());
    }

    if (providerIds.isEmpty())
        return;

    for (const QString& providerId : providerIds)
        probeProvider(providerId);
}

void HealthMonitor::probeProvider(const QString& providerId)
{
    const QString key = normalizeProviderId(providerId);
    if (key.isEmpty() || !m_modelFactory)
        return;
    if (m_providerProbeRunning.value(key, false))
        return;

    const ProviderInstanceConfig cfg = m_modelFactory->getProviderInstance(key);
    if (!cfg.isValid() || cfg.baseUrl.trimmed().isEmpty()) {
        updateProviderState(key, Unknown, QStringLiteral("missing_provider_config"));
        return;
    }

    QUrl url(cfg.baseUrl.trimmed());
    if (!url.isValid()) {
        updateProviderState(key, Down, QStringLiteral("invalid_base_url"));
        return;
    }

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("TmAgent-HealthMonitor/1.0"));
    if (!cfg.apiKey.trimmed().isEmpty()) {
        const QString authType = cfg.authType.trimmed().isEmpty() ? QStringLiteral("Bearer") : cfg.authType.trimmed();
        if (authType.compare(QStringLiteral("Bearer"), Qt::CaseInsensitive) == 0) {
            req.setRawHeader("Authorization", QByteArray("Bearer ") + cfg.apiKey.trimmed().toUtf8());
        } else {
            req.setRawHeader(authType.toUtf8(), cfg.apiKey.trimmed().toUtf8());
        }
    }

    m_providerProbeRunning.insert(key, true);
    QNetworkReply* reply = m_network->get(req);

    QTimer* timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    timeout->setInterval(8000);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    timeout->start();

    connect(reply, &QNetworkReply::finished, this, [this, key, reply]() {
        m_providerProbeRunning.insert(key, false);
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QString errStr = reply->errorString().trimmed();

        if (netError == QNetworkReply::NoError) {
            updateProviderState(key, Healthy, QString());
        } else if (httpStatus == 401 || httpStatus == 403 || httpStatus == 404 || httpStatus == 405) {
            updateProviderState(key, Healthy, QStringLiteral("http_%1").arg(httpStatus));
        } else if (httpStatus >= 500) {
            updateProviderState(key, Degraded, QStringLiteral("http_%1").arg(httpStatus));
        } else {
            updateProviderState(key, Down, errStr.isEmpty() ? QStringLiteral("network_error") : errStr);
        }

        reply->deleteLater();
    });
}

void HealthMonitor::updateProviderState(const QString& providerId, State state, const QString& reason)
{
    const QString key = normalizeProviderId(providerId);
    if (key.isEmpty())
        return;

    const State previous = m_providerStates.value(key, Unknown);
    m_providerStates.insert(key, state);
    emit providerStateChanged(key, state, reason);

    if (state == Down && previous != Down)
        emit providerDown(key, reason.isEmpty() ? QStringLiteral("down") : reason);
    if (previous == Down && state != Down)
        emit providerRecovered(key);
}
