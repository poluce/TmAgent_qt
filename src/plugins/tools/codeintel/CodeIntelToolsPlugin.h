#ifndef CODEINTELTOOLSPLUGIN_H
#define CODEINTELTOOLSPLUGIN_H

#include "core/agent/IToolPlugin.h"
#include <QObject>

class CodeIntelToolsPlugin final : public QObject, public IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID TMAGENT_TOOL_PLUGIN_IID FILE "code_intel_tools.json")
    Q_INTERFACES(IToolPlugin)
public:
    ToolPluginDescriptor descriptor() const override;
    IToolProvider* createProvider(IToolPluginHost* host, QObject* parent) override;
};

#endif // CODEINTELTOOLSPLUGIN_H
