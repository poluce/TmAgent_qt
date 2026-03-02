#ifndef AGENTPULSE_H
#define AGENTPULSE_H

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QTimer;

class AgentPulse : public QObject {
    Q_OBJECT
public:
    enum State {
        Healthy,
        SoftTimeout,
        Stalled,
        HardTimeout,
        Dead
    };
    Q_ENUM(State)

    explicit AgentPulse(const QString& agentId, QObject* parent = nullptr);

    void start(int intervalMs = 1000);
    void stop();

    void reportProgress(const QString& summary = QString());

    State currentState() const;
    qint64 idleMs() const;

    void setThresholds(int softTimeoutMs, int hardTimeoutMs, int stallMs);

signals:
    void stateChanged(const QString& agentId, AgentPulse::State newState);
    void hardTimeoutReached(const QString& agentId);

private slots:
    void onTick();

private:
    QString m_agentId;
    QTimer* m_timer = nullptr;
    QElapsedTimer m_lastProgressTimer;
    State m_state = Healthy;
    int m_softTimeoutMs = 180000;
    int m_hardTimeoutMs = 600000;
    int m_stallMs = 300000;
};

#endif // AGENTPULSE_H
