#ifndef CODEXDELEGATEBACKEND_H
#define CODEXDELEGATEBACKEND_H

#include <tmagent/plugin/IDelegateBackend.h>

class CodexDelegateBackend final : public TmAgent::IDelegateBackend {
public:
    QString backendId() const override;
    std::unique_ptr<TmAgent::IDelegateSession> createSession(
        const TmAgent::DelegateRequest& request,
        const TmAgent::DelegateCallbacks& callbacks,
        QString* error) override;
};

#endif // CODEXDELEGATEBACKEND_H
