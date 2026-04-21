#ifndef CODEINTELTOOLSPLUGIN_H
#define CODEINTELTOOLSPLUGIN_H

#include <tmagent/plugin/IToolPlugin.h>
#include <QObject>

class CodeIntelToolsPlugin final : public QObject, public TmAgent::IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "code_intel_tools.json")
    Q_INTERFACES(TmAgent::IToolPlugin)
public:
    TmAgent::ToolPluginDescriptor descriptor() const override;
    TmAgent::IToolProvider* createProvider(TmAgent::IToolPluginHost* host, QObject* parent) override;
};

#endif // CODEINTELTOOLSPLUGIN_H
