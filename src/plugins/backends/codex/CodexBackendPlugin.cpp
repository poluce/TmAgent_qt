#include "CodexBackendPlugin.h"

#include "CodexTeammateBackend.h"
#include "CodexDelegateBackend.h"
#include <tmagent/version.h>

TmAgent::BackendDescriptor CodexBackendPlugin::descriptor() const
{
    TmAgent::BackendDescriptor descriptor;
    descriptor.backendId = QStringLiteral("codex");
    descriptor.displayName = QStringLiteral("Codex");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.supportsDelegate = true;
    descriptor.supportsTeammate = true;
    descriptor.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    descriptor.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    return descriptor;
}

TmAgent::IDelegateBackend* CodexBackendPlugin::createDelegateBackend(QObject*)
{
    if (!m_delegateBackend)
        m_delegateBackend = std::make_unique<CodexDelegateBackend>();
    return m_delegateBackend.get();
}

TmAgent::ITeammateBackend* CodexBackendPlugin::createTeammateBackend(QObject*)
{
    if (!m_teammateBackend)
        m_teammateBackend = new CodexTeammateBackend(this);
    return m_teammateBackend;
}
