#ifndef MEMORYTOOLWRITESERVICE_H
#define MEMORYTOOLWRITESERVICE_H

#include "TurnManager.h"
#include "core/agent/ToolTypes.h"

#include <QJsonObject>
#include <QString>
#include <functional>

class MemoryToolWriteService {
public:
    struct Dependencies {
        std::function<QString(const QString&)> activeSessionForAgent;
        std::function<QString(const QString&)> resolveSessionForAgent;
        std::function<TurnTask*(const QString&)> activeTurnForSession;
        std::function<bool(const QString&,
                           const QString&,
                           const QString&,
                           const QString&,
                           const QString&,
                           const QString&,
                           QString*,
                           QString*,
                           QJsonObject*,
                           QString*)> rememberToolRequested;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&)> refreshMemoryIndexAndEmit;
    };

    explicit MemoryToolWriteService(const Dependencies& dependencies);

    ToolResult execute(const QJsonObject& args) const;

private:
    Dependencies m_dependencies;
};

#endif // MEMORYTOOLWRITESERVICE_H
