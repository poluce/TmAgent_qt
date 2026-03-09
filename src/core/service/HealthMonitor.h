#ifndef HEALTHMONITOR_H
#define HEALTHMONITOR_H

#include <QHash>
#include <QObject>
#include <QString>

class QTimer;
class QNetworkAccessManager;
class RuntimeManager;
class ModelFactory;

class HealthMonitor : public QObject {
    Q_OBJECT
public:
    enum State {
        Healthy,
        Degraded,
        Down,
        Unknown
    };
    Q_ENUM(State)

    explicit HealthMonitor(QObject* parent = nullptr);
    ~HealthMonitor() override;

    void setRuntimeManager(RuntimeManager* runtimeManager);
    void setModelFactory(ModelFactory* modelFactory);

    void setIntervalMs(int intervalMs);
    int intervalMs() const;

    void start();
    void stop();

    State providerState(const QString& configId) const;
    State mcpState(const QString& serverId) const;
    State lspState() const;

    bool isProviderDown(const QString& configId) const;

signals:
    void providerDown(const QString& configId, const QString& reason);
    void providerRecovered(const QString& configId);
    void mcpDown(const QString& serverId, const QString& reason);
    void mcpRecovered(const QString& serverId);
    void lspDown(const QString& reason);
    void lspRecovered();
    void providerStateChanged(const QString& configId, HealthMonitor::State state, const QString& reason);

private slots:
    void onCheckTick();

private:
    void probeProvider(const QString& providerId);
    void updateProviderState(const QString& providerId, State state, const QString& reason);

    QTimer* m_timer = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    RuntimeManager* m_runtimeManager = nullptr;
    ModelFactory* m_modelFactory = nullptr;

    QHash<QString, State> m_providerStates;
    QHash<QString, State> m_mcpStates;
    State m_lspState = Unknown;
    QHash<QString, bool> m_providerProbeRunning;
};

#endif // HEALTHMONITOR_H
