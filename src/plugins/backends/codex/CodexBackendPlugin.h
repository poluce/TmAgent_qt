#ifndef CODEXBACKENDPLUGIN_H
#define CODEXBACKENDPLUGIN_H

#include <tmagent/plugin/IBackendPlugin.h>
#include <tmagent/plugin/IDelegateBackend.h>
#include <QObject>
#include <memory>

class CodexTeammateBackend;

class CodexBackendPlugin final : public QObject, public TmAgent::IBackendPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_BACKEND_PLUGIN_IID FILE "codex_backend.json")
    Q_INTERFACES(TmAgent::IBackendPlugin)
public:
    TmAgent::BackendDescriptor descriptor() const override;
    TmAgent::IDelegateBackend* createDelegateBackend(QObject* parent) override;
    TmAgent::ITeammateBackend* createTeammateBackend(QObject* parent) override;

private:
    std::unique_ptr<TmAgent::IDelegateBackend> m_delegateBackend;
    CodexTeammateBackend* m_teammateBackend = nullptr;
};

#endif // CODEXBACKENDPLUGIN_H
