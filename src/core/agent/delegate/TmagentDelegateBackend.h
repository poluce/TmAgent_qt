#ifndef TMAGENTPLUGIN_TMAGENTDELEGATEBACKEND_H
#define TMAGENTPLUGIN_TMAGENTDELEGATEBACKEND_H

#include "core/agent/delegate/IDelegateBackend.h"

namespace DelegateBackendInternal {

class TmagentDelegateBackend final : public IDelegateBackend {
public:
    QString backendId() const override;
    std::unique_ptr<IDelegateBackendSession> createSession(
        const DelegateBackendStartRequest& request,
        const DelegateBackendCallbacks& callbacks,
        QString* error) override;
};

} // namespace DelegateBackendInternal

#endif // TMAGENTPLUGIN_TMAGENTDELEGATEBACKEND_H
