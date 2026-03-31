#ifndef MINIMALTOOLPLUGIN_H
#define MINIMALTOOLPLUGIN_H

#include <tmagent/plugin/IToolPlugin.h>
#include <tmagent/plugin/IToolProvider.h>
#include <QObject>

using namespace TmAgent;

class MinimalToolProvider : public QObject, public IToolProvider {
    Q_OBJECT
public:
    explicit MinimalToolProvider(IToolPluginHost* host, QObject* parent = nullptr);
    
    QList<Tool> listTools() const override;
    ToolResult execute(const ToolCall& call) override;

private:
    IToolPluginHost* m_host;
};

class MinimalToolPlugin : public QObject, public IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "minimal_tool.json")
    Q_INTERFACES(TmAgent::IToolPlugin)

public:
    ToolPluginDescriptor descriptor() const override;
    IToolProvider* createProvider(IToolPluginHost* host, QObject* parent) override;
};

#endif // MINIMALTOOLPLUGIN_H
