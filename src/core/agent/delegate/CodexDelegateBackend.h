#ifndef CODEXDELEGATEBACKEND_H
#define CODEXDELEGATEBACKEND_H

#include "IDelegateBackend.h"

namespace DelegateBackendInternal {

class CodexDelegateBackend final : public IDelegateBackend {
public:
    QString backendId() const override;
    std::unique_ptr<IDelegateBackendSession> createSession(
        const DelegateBackendStartRequest& request,
        const DelegateBackendCallbacks& callbacks,
        QString* error) override;
};

} // namespace DelegateBackendInternal

#endif // CODEXDELEGATEBACKEND_H
