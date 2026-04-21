#ifndef COORDINATIONTOOLSPLUGIN_H
#define COORDINATIONTOOLSPLUGIN_H

#include <tmagent/plugin/IToolPlugin.h>
#include <QObject>

class CoordinationToolsPlugin final : public QObject, public TmAgent::IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "coordination_tools.json")
    Q_INTERFACES(TmAgent::IToolPlugin)
public:
    TmAgent::ToolPluginDescriptor descriptor() const override;
    TmAgent::IToolProvider* createProvider(TmAgent::IToolPluginHost* host, QObject* parent) override;
};

#endif // COORDINATIONTOOLSPLUGIN_H
