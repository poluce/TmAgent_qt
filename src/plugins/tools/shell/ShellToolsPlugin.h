#ifndef SHELLTOOLSPLUGIN_H
#define SHELLTOOLSPLUGIN_H

#include <tmagent/plugin/IToolPlugin.h>
#include <QObject>

class ShellToolsPlugin final : public QObject, public TmAgent::IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "shell_tools.json")
    Q_INTERFACES(TmAgent::IToolPlugin)
public:
    TmAgent::ToolPluginDescriptor descriptor() const override;
    TmAgent::IToolProvider* createProvider(TmAgent::IToolPluginHost* host, QObject* parent) override;
};

#endif // SHELLTOOLSPLUGIN_H
