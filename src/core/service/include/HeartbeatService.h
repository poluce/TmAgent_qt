#ifndef HEARTBEATSERVICE_H
#define HEARTBEATSERVICE_H

#include "HeartbeatRuntimeState.h"
#include "HeartbeatTypes.h"
#include <QHash>
#include <QObject>

class ApplicationServices;
class Identity;
class QTimer;

class HeartbeatDecisionEngine;
class HeartbeatExecutionService;
class HeartbeatSnapshotService;

class HeartbeatService : public QObject {
    Q_OBJECT
public:
    explicit HeartbeatService(ApplicationServices& app, QObject* parent = nullptr);
    ~HeartbeatService() override;

    void startAgentHeartbeat(const QString& agentId);
    void stopAgentHeartbeat(const QString& agentId);
    void stopAll();

    void requestManualHeartbeat(const QString& agentId,
                                const QString& reason = QStringLiteral("manual"));
    void requestEventDrivenHeartbeat(const QString& agentId,
                                     const QString& reason,
                                     HeartbeatTicketPriority priority = HeartbeatTicketPriority::High,
                                     const QJsonObject& payload = QJsonObject());

    void suppressAgentHeartbeat(const QString& agentId, const QString& reason);
    void unsuppressAgentHeartbeat(const QString& agentId);

    void updatePolicy(const QString& agentId, const HeartbeatPolicy& policy);
    HeartbeatPolicy policyForAgent(const QString& agentId) const;
    QString instructionPathForAgent(const QString& agentId) const;

private slots:
    void onTick();
    void onProviderDown(const QString& providerId, const QString& reason);
    void onProviderRecovered(const QString& providerId);

private:
    struct AgentState {
        HeartbeatPolicy policy;
        HeartbeatRuntimeState runtimeState;
        bool suppressed = false;
        QString suppressReason;
        bool cycleRunning = false;
        QDateTime startupReadyAtUtc;
    };

    AgentState* ensureAgent(const QString& agentId);
    void removeAgent(const QString& agentId);

    HeartbeatPolicy defaultPolicyForAgent(const QString& agentId) const;
    HeartbeatPolicy loadPolicy(const QString& agentId) const;
    void savePolicy(const QString& agentId, const HeartbeatPolicy& policy) const;

    bool isWithinActiveHours(const HeartbeatPolicy& policy, const QDateTime& nowUtc) const;
    QDateTime nextActiveTime(const HeartbeatPolicy& policy, const QDateTime& nowUtc) const;
    void scheduleNextDue(AgentState* agent, const QDateTime& referenceUtc);
    void restoreRuntimeState(const QString& agentId, AgentState* agent, const QDateTime& nowUtc);
    void requestTicket(const QString& agentId, const HeartbeatTicket& ticket);
    void deferTicket(const QString& agentId,
                     AgentState* agent,
                     const HeartbeatTicket& ticket,
                     const QString& reason,
                     const QDateTime& nowUtc);
    void beginCycle(const QString& agentId, AgentState* agent, const HeartbeatTicket& ticket);
    void handleCycleCompleted(const QString& agentId,
                              AgentState* agent,
                              const HeartbeatTicket& ticket,
                              const HeartbeatCycleResult& cycleResult,
                              const HeartbeatSnapshot& snapshot,
                              const QDateTime& startedAtUtc,
                              const QString& summaryDigest);
    QString providerStateForAgent(Identity* agent) const;
    QString providerIdForAgent(Identity* agent) const;
    void emitHeartbeatEvent(const QString& type,
                            const QString& agentId,
                            const QJsonObject& extra = QJsonObject(),
                            const QString& sessionId = QString(),
                            const QString& error = QString(),
                            bool persistToDisk = true);

private:
    ApplicationServices& m_app;
    HeartbeatSnapshotService* m_snapshotService = nullptr;
    HeartbeatDecisionEngine* m_decisionEngine = nullptr;
    HeartbeatExecutionService* m_executionService = nullptr;
    QHash<QString, AgentState*> m_agents;
    QTimer* m_tickTimer = nullptr;
    bool m_healthMonitorConnected = false;
};

#endif // HEARTBEATSERVICE_H
