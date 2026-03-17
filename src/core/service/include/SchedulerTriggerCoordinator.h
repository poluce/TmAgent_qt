#ifndef SCHEDULERTRIGGERCOORDINATOR_H
#define SCHEDULERTRIGGERCOORDINATOR_H

#include "SchedulerService.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class Identity;

class SchedulerTriggerCoordinator {
public:
    struct Dependencies {
        std::function<bool(const QString&, ScheduledJob*)> jobById;
        std::function<Identity*(const QString&)> findIdentity;
        std::function<QString(const QString&, bool, bool, const QString&)> resolvePrimarySessionForAgent;
        std::function<QString()> userIdentityId;
        std::function<QString(const QString&, const QString&, const QString&, const QString&)> buildClientMessageId;
        std::function<QString(const QString&, const QString&, const QString&, const QString&)> buildPrompt;
        std::function<QString(const QString&, const QString&, const QString&, const QString&)> enqueueUserMessageAs;
        std::function<void(const QString&,
                           const QString&,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEventSimple;
    };

    explicit SchedulerTriggerCoordinator(const Dependencies& dependencies);

    void onScheduledJobTriggered(const QString& jobId, const QString& jobName);

private:
    Dependencies m_dependencies;
};

#endif // SCHEDULERTRIGGERCOORDINATOR_H

