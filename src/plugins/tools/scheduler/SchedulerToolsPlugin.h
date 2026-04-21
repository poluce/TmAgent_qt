#ifndef SCHEDULERTOOLSPLUGIN_H
#define SCHEDULERTOOLSPLUGIN_H

#include <tmagent/plugin/IToolPlugin.h>
#include <QObject>

using namespace TmAgent;

class SchedulerToolsPlugin final : public QObject, public IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "scheduler_tools.json")
    Q_INTERFACES(TmAgent::IToolPlugin)
public:
    ToolPluginDescriptor descriptor() const override;
    IToolProvider* createProvider(IToolPluginHost* host, QObject* parent) override;
};

#endif // SCHEDULERTOOLSPLUGIN_H
