#ifndef TMAGENTDELEGATEBACKEND_H
#define TMAGENTDELEGATEBACKEND_H

#include "IDelegateBackend.h"

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

#endif // TMAGENTDELEGATEBACKEND_H
