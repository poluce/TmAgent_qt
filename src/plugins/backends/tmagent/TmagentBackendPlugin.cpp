#include "TmagentBackendPlugin.h"
#include "TmagentDelegateBackendAdapter.h"
#include "TmagentTeammateBackendAdapter.h"
#include <tmagent/version.h>

TmAgent::BackendDescriptor TmagentBackendPlugin::descriptor() const
{
    TmAgent::BackendDescriptor descriptor;
    descriptor.backendId = QStringLiteral("tmagent");
    descriptor.displayName = QStringLiteral("TmAgent");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.supportsDelegate = true;
    descriptor.supportsTeammate = true;
    descriptor.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    descriptor.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    return descriptor;
}

TmAgent::IDelegateBackend* TmagentBackendPlugin::createDelegateBackend(QObject* parent)
{
    if (!m_delegateBackend)
        m_delegateBackend = new TmagentDelegateBackendAdapter(parent ? parent : this);
    return m_delegateBackend;
}

TmAgent::ITeammateBackend* TmagentBackendPlugin::createTeammateBackend(QObject* parent)
{
    if (!m_teammateBackend)
        m_teammateBackend = new TmagentTeammateBackendAdapter(parent ? parent : this);
    return m_teammateBackend;
}
