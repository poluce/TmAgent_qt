#ifndef MINIMALBACKENDPLUGIN_H
#define MINIMALBACKENDPLUGIN_H

#include <tmagent/plugin/IBackendPlugin.h>
#include <tmagent/plugin/IDelegateBackend.h>
#include <QObject>

using namespace TmAgent;

class MinimalDelegateSession : public QObject, public IDelegateSession {
    Q_OBJECT
public:
    MinimalDelegateSession(const DelegateRequest& request,
                          const DelegateCallbacks& callbacks,
                          QObject* parent = nullptr);
    
    QString backendId() const override { return "minimal_backend"; }
    void start() override;
    void cancel() override;

private:
    DelegateRequest m_request;
    DelegateCallbacks m_callbacks;
    bool m_cancelled = false;
};

class MinimalDelegateBackend : public QObject, public IDelegateBackend {
    Q_OBJECT
public:
    explicit MinimalDelegateBackend(QObject* parent = nullptr);
    
    QString backendId() const override { return "minimal_backend"; }
    std::unique_ptr<IDelegateSession> createSession(
        const DelegateRequest& request,
        const DelegateCallbacks& callbacks,
        QString* error) override;
};

class MinimalBackendPlugin : public QObject, public IBackendPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_BACKEND_PLUGIN_IID FILE "minimal_backend.json")
    Q_INTERFACES(TmAgent::IBackendPlugin)

public:
    BackendDescriptor descriptor() const override;
    IDelegateBackend* createDelegateBackend(QObject* parent) override;
    ITeammateBackend* createTeammateBackend(QObject* parent) override;
};

#endif // MINIMALBACKENDPLUGIN_H
