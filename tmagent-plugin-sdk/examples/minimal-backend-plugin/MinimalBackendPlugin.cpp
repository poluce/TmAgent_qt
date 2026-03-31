#include "MinimalBackendPlugin.h"
#include <tmagent/version.h>
#include <QTimer>

// MinimalDelegateSession implementation
MinimalDelegateSession::MinimalDelegateSession(const DelegateRequest& request,
                                             const DelegateCallbacks& callbacks,
                                             QObject* parent)
    : QObject(parent), m_request(request), m_callbacks(callbacks)
{
}

void MinimalDelegateSession::start()
{
    if (m_cancelled) return;
    
    // Simulate activity
    if (m_callbacks.onActivity) {
        m_callbacks.onActivity();
    }
    
    // Simulate processing with a simple response
    QTimer::singleShot(100, this, [this]() {
        if (m_cancelled) return;
        
        QString response = QString("Task completed: %1").arg(m_request.task);
        
        if (m_callbacks.onSummary) {
            m_callbacks.onSummary(response);
        }
        
        if (m_callbacks.onSuccess) {
            m_callbacks.onSuccess(response);
        }
    });
}

void MinimalDelegateSession::cancel()
{
    m_cancelled = true;
    
    if (m_callbacks.onFailure) {
        m_callbacks.onFailure("Task cancelled by user");
    }
}

// MinimalDelegateBackend implementation
MinimalDelegateBackend::MinimalDelegateBackend(QObject* parent)
    : QObject(parent)
{
}

std::unique_ptr<IDelegateSession> MinimalDelegateBackend::createSession(
    const DelegateRequest& request,
    const DelegateCallbacks& callbacks,
    QString* error)
{
    if (request.task.isEmpty()) {
        if (error) {
            *error = "Task description is empty";
        }
        return nullptr;
    }
    
    return std::make_unique<MinimalDelegateSession>(request, callbacks, this);
}

// MinimalBackendPlugin implementation
BackendDescriptor MinimalBackendPlugin::descriptor() const
{
    BackendDescriptor desc;
    desc.backendId = "minimal_backend";
    desc.displayName = "Minimal Backend";
    desc.version = "1.0.0";
    desc.supportsDelegate = true;
    desc.supportsTeammate = false;
    desc.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    desc.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    
    return desc;
}

IDelegateBackend* MinimalBackendPlugin::createDelegateBackend(QObject* parent)
{
    return new MinimalDelegateBackend(parent);
}

ITeammateBackend* MinimalBackendPlugin::createTeammateBackend(QObject* parent)
{
    // Not supported in this minimal example
    return nullptr;
}
