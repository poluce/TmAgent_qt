#ifndef SHELLTOOLSPLUGIN_H
#define SHELLTOOLSPLUGIN_H

#include "core/agent/IToolPlugin.h"
#include <QObject>

class ShellToolsPlugin final : public QObject, public IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "shell_tools.json")
    Q_INTERFACES(IToolPlugin)
public:
    ToolPluginDescriptor descriptor() const override;
    IToolProvider* createProvider(IToolPluginHost* host, QObject* parent) override;
};

#endif // SHELLTOOLSPLUGIN_H
