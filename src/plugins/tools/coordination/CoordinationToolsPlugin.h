#ifndef COORDINATIONTOOLSPLUGIN_H
#define COORDINATIONTOOLSPLUGIN_H

#include "core/agent/IToolPlugin.h"
#include <QObject>

class CoordinationToolsPlugin final : public QObject, public IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "coordination_tools.json")
    Q_INTERFACES(IToolPlugin)
public:
    ToolPluginDescriptor descriptor() const override;
    IToolProvider* createProvider(IToolPluginHost* host, QObject* parent) override;
};

#endif // COORDINATIONTOOLSPLUGIN_H
