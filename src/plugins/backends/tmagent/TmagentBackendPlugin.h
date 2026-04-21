#ifndef TMAGENTBACKENDPLUGIN_H
#define TMAGENTBACKENDPLUGIN_H

#include <tmagent/plugin/IBackendPlugin.h>
#include <QObject>

class TmagentDelegateBackendAdapter;
class TmagentTeammateBackendAdapter;

class TmagentBackendPlugin final : public QObject, public TmAgent::IBackendPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_BACKEND_PLUGIN_IID FILE "tmagent_backend.json")
    Q_INTERFACES(TmAgent::IBackendPlugin)
    
public:
    TmAgent::BackendDescriptor descriptor() const override;
    TmAgent::IDelegateBackend* createDelegateBackend(QObject* parent = nullptr) override;
    TmAgent::ITeammateBackend* createTeammateBackend(QObject* parent = nullptr) override;

private:
    TmagentDelegateBackendAdapter* m_delegateBackend = nullptr;
    TmagentTeammateBackendAdapter* m_teammateBackend = nullptr;
};

#endif // TMAGENTBACKENDPLUGIN_H
