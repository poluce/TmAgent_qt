#ifndef WORKSPACETOOLSPLUGIN_H
#define WORKSPACETOOLSPLUGIN_H

#include <tmagent/plugin/IToolPlugin.h>
#include <QObject>

class WorkspaceToolsPlugin final : public QObject, public TmAgent::IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "workspace_tools.json")
    Q_INTERFACES(TmAgent::IToolPlugin)
public:
    TmAgent::ToolPluginDescriptor descriptor() const override;
    TmAgent::IToolProvider* createProvider(TmAgent::IToolPluginHost* host, QObject* parent) override;
};

#endif // WORKSPACETOOLSPLUGIN_H
