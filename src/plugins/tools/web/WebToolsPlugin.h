#ifndef WEBTOOLSPLUGIN_H
#define WEBTOOLSPLUGIN_H

#include <tmagent/plugin/IToolPlugin.h>
#include <QObject>

class WebToolsPlugin final : public QObject, public TmAgent::IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "web_tools.json")
    Q_INTERFACES(TmAgent::IToolPlugin)
public:
    TmAgent::ToolPluginDescriptor descriptor() const override;
    TmAgent::IToolProvider* createProvider(TmAgent::IToolPluginHost* host, QObject* parent) override;
};

#endif // WEBTOOLSPLUGIN_H
