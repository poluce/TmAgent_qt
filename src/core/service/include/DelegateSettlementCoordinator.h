#ifndef DELEGATESETTLEMENTCOORDINATOR_H
#define DELEGATESETTLEMENTCOORDINATOR_H

#include "core/model/Message.h"
#include <QJsonObject>
#include <QString>
#include <functional>

class DelegateSettlementCoordinator {
public:
    struct Dependencies {
        std::function<QString(const QString&, bool, bool, const QString&)> resolvePrimarySessionForAgent;
        std::function<void(const QString&, const Message&)> postMessage;
        std::function<void(const QString&, const QString&, const void*, const QJsonObject&)> updateTaskStateForSession;
        std::function<QString(const QString&, int)> taskStateTextPreview;
        std::function<void(const QString&, const QString&)> triggerHeartbeat;
        std::function<void(const QString&, const QString&, const QString&, const QString&, const QJsonObject&, bool)> emitPipelineEventSimple;
    };

    explicit DelegateSettlementCoordinator(const Dependencies& dependencies);

    void onDelegateJobSettled(const QString& jobId, const QString& ownerAgentId, bool success, const QString& result);

private:
    Dependencies m_dependencies;
};

#endif // DELEGATESETTLEMENTCOORDINATOR_H
