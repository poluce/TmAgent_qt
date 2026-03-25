#ifndef HEARTBEATEXECUTIONSERVICE_H
#define HEARTBEATEXECUTIONSERVICE_H

#include "HeartbeatTypes.h"
#include <functional>

struct TurnTask;

class HeartbeatExecutionService {
public:
    struct Dependencies {
        std::function<QString(const QString&, bool, bool, const QString&)> resolvePrimarySessionForAgent;
        std::function<void(const QString&, const QString&, const TurnTask&, bool, const QString&)> reflectMemory;
        std::function<void(const QString&, const QString&, const TurnTask*, const QString&, const QString&, const QJsonObject&)> rebuildMemoryIndex;
        std::function<QString(const QString&, const HeartbeatTicket&, const HeartbeatSnapshot&, const QStringList&)> buildEscalationPrompt;
    };

    explicit HeartbeatExecutionService(const Dependencies& dependencies)
        : m_dependencies(dependencies)
    {
    }

    QString ensureSessionForMaintenance(const QString& agentId) const;
    void runMaintenance(const QString& sessionId,
                        const QString& agentId,
                        const HeartbeatPolicy& policy,
                        const HeartbeatTicket& ticket) const;
    QString buildEscalationPrompt(const QString& agentId,
                                 const HeartbeatTicket& ticket,
                                 const HeartbeatSnapshot& snapshot,
                                 const QStringList& actionableSignals) const;
    QString buildFallbackSummary(const HeartbeatTicket& ticket,
                                 const HeartbeatSnapshot& snapshot,
                                 const QStringList& actionableSignals) const;

private:
    Dependencies m_dependencies;
};

#endif // HEARTBEATEXECUTIONSERVICE_H
