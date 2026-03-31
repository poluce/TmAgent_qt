#include "CodexBackendPlugin.h"

#include "CodexTeammateBackend.h"
#include "CodexDelegateBackend.h"

BackendDescriptor CodexBackendPlugin::descriptor() const
{
    BackendDescriptor descriptor;
    descriptor.backendId = QStringLiteral("codex");
    descriptor.displayName = QStringLiteral("Codex");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.supportsDelegate = true;
    descriptor.supportsTeammate = true;
    return descriptor;
}

DelegateBackendInternal::IDelegateBackend* CodexBackendPlugin::createDelegateBackend(QObject*)
{
    if (!m_delegateBackend)
        m_delegateBackend = std::make_unique<DelegateBackendInternal::CodexDelegateBackend>();
    return m_delegateBackend.get();
}

ITeammateBackend* CodexBackendPlugin::createTeammateBackend(QObject*)
{
    if (!m_teammateBackend)
        m_teammateBackend = new CodexTeammateBackend(this);
    return m_teammateBackend;
}
