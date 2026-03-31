#ifndef IBACKENDPLUGIN_H
#define IBACKENDPLUGIN_H

#include "core/agent/delegate/IDelegateBackend.h"
#include "core/service/include/ITeammateBackend.h"
#include <QtPlugin>

struct BackendDescriptor {
    QString backendId;
    QString displayName;
    QString version;
    bool supportsDelegate = false;
    bool supportsTeammate = false;

    bool isValid() const
    {
        return !backendId.trimmed().isEmpty()
            && (supportsDelegate || supportsTeammate);
    }
};

class IBackendPlugin {
public:
    virtual ~IBackendPlugin() = default;

    virtual BackendDescriptor descriptor() const = 0;
    virtual DelegateBackendInternal::IDelegateBackend* createDelegateBackend(QObject* parent) = 0;
    virtual ITeammateBackend* createTeammateBackend(QObject* parent) = 0;
};

#define TMAGENT_BACKEND_PLUGIN_IID "org.tmagent.BackendPlugin/1.0"
Q_DECLARE_INTERFACE(IBackendPlugin, TMAGENT_BACKEND_PLUGIN_IID)

#endif // IBACKENDPLUGIN_H
