#ifndef CODEXBACKENDPLUGIN_H
#define CODEXBACKENDPLUGIN_H

#include "core/backend/IBackendPlugin.h"
#include <QObject>
#include <memory>

class CodexTeammateBackend;

namespace DelegateBackendInternal {
class IDelegateBackend;
}

class CodexBackendPlugin final : public QObject, public IBackendPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_BACKEND_PLUGIN_IID FILE "codex_backend.json")
    Q_INTERFACES(IBackendPlugin)
public:
    BackendDescriptor descriptor() const override;
    DelegateBackendInternal::IDelegateBackend* createDelegateBackend(QObject* parent) override;
    ITeammateBackend* createTeammateBackend(QObject* parent) override;

private:
    std::unique_ptr<DelegateBackendInternal::IDelegateBackend> m_delegateBackend;
    CodexTeammateBackend* m_teammateBackend = nullptr;
};

#endif // CODEXBACKENDPLUGIN_H
