#ifndef HEARTBEATSERVICE_H
#define HEARTBEATSERVICE_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTime>

class QTimer;
class ChatPersistenceService;
class HeartbeatWake;

struct ActiveHours {
    QTime start;
    QTime end;
    QString timezone;
};

struct HeartbeatConfig {
    bool enabled = true;
    int intervalMs = 30 * 60 * 1000;
    int coalesceMs = 250;
    int duplicateWindowMs = 24 * 60 * 60 * 1000;
    bool silentWhenNoChange = true;
    bool notifyOnChangeOnly = true;
    int notifyMinIntervalMs = 30 * 60 * 1000;
    ActiveHours activeHours;
    QString heartbeatPath;
};

class HeartbeatService : public QObject {
    Q_OBJECT
public:
    explicit HeartbeatService(QObject* parent = nullptr);
    ~HeartbeatService() override;

    void setPersistence(ChatPersistenceService* persistence);

    void startHeartbeat(const QString& agentId);
    void stopHeartbeat(const QString& agentId);
    void stopAll();

    void triggerHeartbeat(const QString& agentId, const QString& reason = QStringLiteral("requested"));

    void suppressHeartbeat(const QString& agentId, const QString& reason);
    void unsuppressHeartbeat(const QString& agentId);

    void updateConfig(const QString& agentId, const HeartbeatConfig& config);
    HeartbeatConfig configForAgent(const QString& agentId) const;
    QString heartbeatPathForAgent(const QString& agentId) const;

signals:
    void heartbeatTriggered(const QString& agentId, const QString& reason);
    void heartbeatCompleted(const QString& agentId, const QString& result);
    void heartbeatSkipped(const QString& agentId, const QString& reason);

private slots:
    void onTick();

private:
    struct AgentHeartbeat {
        HeartbeatConfig config;
        HeartbeatWake* wake = nullptr;
        QDateTime lastRunAtUtc;
        QDateTime nextRunAtUtc;
        bool suppressed = false;
        QString suppressReason;
    };

    AgentHeartbeat* ensureAgent(const QString& agentId);
    void removeAgent(const QString& agentId);
    HeartbeatConfig loadConfig(const QString& agentId) const;
    void saveConfig(const QString& agentId, const HeartbeatConfig& config) const;
    bool isWithinActiveHours(const HeartbeatConfig& config, const QDateTime& nowUtc) const;
    void scheduleNextRun(AgentHeartbeat* agent, const QDateTime& nowUtc);

    ChatPersistenceService* m_persistence = nullptr;
    QHash<QString, AgentHeartbeat*> m_agents;
    QTimer* m_tickTimer = nullptr;
};

#endif // HEARTBEATSERVICE_H
