#ifndef TMAGENTBACKENDPLUGIN_H
#define TMAGENTBACKENDPLUGIN_H

#include "core/backend/IBackendPlugin.h"
#include <QObject>
#include <memory>

namespace DelegateBackendInternal {
class IDelegateBackend;
}

class TmagentBackendPlugin final : public QObject, public IBackendPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_BACKEND_PLUGIN_IID FILE "tmagent_backend.json")
    Q_INTERFACES(IBackendPlugin)
public:
    BackendDescriptor descriptor() const override;
    DelegateBackendInternal::IDelegateBackend* createDelegateBackend(QObject* parent) override;
    ITeammateBackend* createTeammateBackend(QObject* parent) override;

private:
    std::unique_ptr<DelegateBackendInternal::IDelegateBackend> m_delegateBackend;
};

#endif // TMAGENTBACKENDPLUGIN_H
