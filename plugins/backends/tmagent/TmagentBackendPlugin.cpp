#include "TmagentBackendPlugin.h"

#include "core/agent/delegate/TmagentDelegateBackend.h"

BackendDescriptor TmagentBackendPlugin::descriptor() const
{
    BackendDescriptor descriptor;
    descriptor.backendId = QStringLiteral("tmagent");
    descriptor.displayName = QStringLiteral("TmAgent");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.supportsDelegate = true;
    descriptor.supportsTeammate = false;
    return descriptor;
}

DelegateBackendInternal::IDelegateBackend* TmagentBackendPlugin::createDelegateBackend(QObject*)
{
    if (!m_delegateBackend)
        m_delegateBackend = std::make_unique<DelegateBackendInternal::TmagentDelegateBackend>();
    return m_delegateBackend.get();
}

ITeammateBackend* TmagentBackendPlugin::createTeammateBackend(QObject*)
{
    return nullptr;
}
