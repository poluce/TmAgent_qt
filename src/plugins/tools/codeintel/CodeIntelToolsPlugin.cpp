#include "CodeIntelToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"

namespace {

QList<HostedToolSpec> codeIntelToolSpecs()
{
    return {
        { QStringLiteral("view_file_outline"), QStringLiteral("查看代码文件大纲") },
        { QStringLiteral("view_code_item"), QStringLiteral("查看指定代码项") },
        { QStringLiteral("lsp"), QStringLiteral("LSP 智能分析工具") },
        { QStringLiteral("lsp_install"), QStringLiteral("安装 LSP 语言服务") }
    };
}

} // namespace

ToolPluginDescriptor CodeIntelToolsPlugin::descriptor() const
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = QStringLiteral("code_intel_tools");
    descriptor.displayName = QStringLiteral("代码智能工具");
    descriptor.version = QStringLiteral("1.0.0");
    descriptor.category = QStringLiteral("code_intel");
    descriptor.description = QStringLiteral("代码大纲、代码项查看和 LSP 相关工具。");
    const QList<HostedToolSpec> specs = codeIntelToolSpecs();
    for (const HostedToolSpec& spec : specs)
        descriptor.toolNames.append(spec.name);
    return descriptor;
}

IToolProvider* CodeIntelToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, codeIntelToolSpecs(), parent);
}
