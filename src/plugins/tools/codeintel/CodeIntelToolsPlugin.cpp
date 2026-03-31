#include "CodeIntelToolsPlugin.h"

#include "CodeIntelToolProvider.h"

ToolPluginDescriptor CodeIntelToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("code_intel_tools");
    descriptor.displayName = QStringLiteral("代码智能工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("code_intel");
    descriptor.description = QStringLiteral("代码大纲、代码项查看和 LSP 相关工具。");
    const QList<Tool> tools = CodeIntelToolProvider::toolSchemas();
    for (const Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* CodeIntelToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    Q_UNUSED(host);
    return new CodeIntelToolProvider(parent);
}
