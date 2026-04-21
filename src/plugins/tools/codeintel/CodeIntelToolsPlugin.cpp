#include "CodeIntelToolsPlugin.h"

#include "CodeIntelToolProvider.h"

TmAgent::ToolPluginDescriptor CodeIntelToolsPlugin::descriptor() const
{
    TmAgent::ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("code_intel_tools");
    descriptor.displayName = QStringLiteral("代码智能工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("code_intel");
    descriptor.description = QStringLiteral("代码大纲、代码项查看和 LSP 相关工具。");
    descriptor.sdkVersionMajor = TMAGENT_SDK_VERSION_MAJOR;
    descriptor.sdkVersionMinor = TMAGENT_SDK_VERSION_MINOR;
    const QList<TmAgent::Tool> tools = CodeIntelToolProvider::toolSchemas();
    for (const TmAgent::Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

TmAgent::IToolProvider* CodeIntelToolsPlugin::createProvider(TmAgent::IToolPluginHost* host, QObject* parent)
{
    return new CodeIntelToolProvider(host, parent);
}
