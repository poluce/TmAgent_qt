#ifndef HEARTBEATDISPATCHCOORDINATOR_H
#define HEARTBEATDISPATCHCOORDINATOR_H

#include "core/agent/DelegateTaskScheduler.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class AgentPulse;

class HeartbeatDispatchCoordinator {
public:
    struct Dependencies {
        std::function<int(const QString&)> queueDepthForSession;
        std::function<void(const QString&,
                           const QString&,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEventSimple;
        std::function<QString()> userIdentityId;
        std::function<QString(const QString&, const QString&)> buildHeartbeatPrompt;
        std::function<AgentPulse*(const QString&)> pulseForAgent;
        std::function<QString(AgentPulse*)> pulseStateText;
        std::function<QString(const QString&, const QString&, const QString&, const QString&)> enqueueUserMessageAs;
        std::function<QString(const QString&, const QString&)> buildHeartbeatClientMessageId;
        std::function<void(const QString&, const QString&)> markHeartbeatNotified;
        std::function<void(bool)> persistStateIfNeeded;
    };

    explicit HeartbeatDispatchCoordinator(const Dependencies& dependencies);

    void dispatch(const QString& agentId,
                  const QString& sessionId,
                  const QString& reasonLabel,
                  bool forceInteractive,
                  bool hasChange,
                  bool watchDelegate,
                  bool watchProvider,
                  bool watchPulse,
                  bool providerDown,
                  const QString& providerId,
                  const QList<DelegateTaskScheduler::JobInfo>& activeJobs,
                  const QJsonObject& triggeredExtra);

private:
    Dependencies m_dependencies;
};

#endif // HEARTBEATDISPATCHCOORDINATOR_H
