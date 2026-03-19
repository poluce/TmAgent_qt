#ifndef BACKGROUNDTASKCOORDINATOR_H
#define BACKGROUNDTASKCOORDINATOR_H

#include "CoordinatorContext.h"
#include "SchedulerService.h"
#include "core/model/Message.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class Identity;

class BackgroundTaskCoordinator {
public:
    struct Dependencies {
        CoordinatorContext ctx;

        // ── Scheduler 专有 ──
        std::function<bool(const QString&, ScheduledJob*)> jobById;
        std::function<Identity*(const QString&)> findIdentity;
        std::function<QString()> userIdentityId;
        std::function<QString(const QString&, const QString&, const QString&, const QString&)> buildClientMessageId;
        std::function<QString(const QString&, const QString&, const QString&, const QString&)> buildPrompt;
        std::function<QString(const QString&, const QString&, const QString&, const QString&)> enqueueUserMessageAs;

        // ── DelegateSettlement 专有 ──
        std::function<void(const QString&, const QString&)> triggerHeartbeat;
        std::function<void(const QString&, const QString&, const void*, const QJsonObject&)> updateTaskStateForSession;

        // ── 共享 ──
        std::function<QString(const QString&, bool, bool, const QString&)> resolvePrimarySessionForAgent;
        std::function<void(const QString&, const QString&, const QString&, const QString&, const QJsonObject&, bool)> emitPipelineEventSimple;
    };

    explicit BackgroundTaskCoordinator(const Dependencies& dependencies);

    void onScheduledJobTriggered(const QString& jobId, const QString& jobName);
    void onDelegateJobSettled(const QString& jobId, const QString& ownerAgentId, bool success, const QString& result);

private:
    Dependencies m_deps;
};

#endif // BACKGROUNDTASKCOORDINATOR_H
