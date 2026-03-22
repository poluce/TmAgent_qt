#include "CodeIntelToolsPlugin.h"

#include "core/agent/HostedToolProvider.h"
#include "core/tools/ToolSchemaSupport.h"

namespace {

QList<Tool> codeIntelTools()
{
    return {
        makeToolSchema(
            QStringLiteral("view_file_outline"),
            QStringLiteral("解析代码文件，提取所有函数、类、结构体的大纲信息。"),
            QJsonObject {
                { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要解析的代码文件绝对路径（目前仅支持 C++）")) }
            },
            QStringList { QStringLiteral("file_path") }),
        makeToolSchema(
            QStringLiteral("view_code_item"),
            QStringLiteral("查看指定函数或类的完整代码。"),
            QJsonObject {
                { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("代码文件绝对路径")) },
                { QStringLiteral("item_name"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要查看的代码项名称")) }
            },
            QStringList { QStringLiteral("file_path"), QStringLiteral("item_name") }),
        makeToolSchema(
            QStringLiteral("lsp"),
            QStringLiteral("执行 LSP 代码智能分析。"),
            QJsonObject {
                { QStringLiteral("operation"), makePropertySchema(QStringLiteral("string"), QStringLiteral("操作类型：status、goToDefinition、findReferences、hover、documentSymbol、workspaceSymbol、goToImplementation、incomingCalls、outgoingCalls")) },
                { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("操作涉及的文件路径")) },
                { QStringLiteral("line"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("行号（0-based）")) },
                { QStringLiteral("character"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("字符列偏移（0-based）")) },
                { QStringLiteral("query"), makePropertySchema(QStringLiteral("string"), QStringLiteral("workspaceSymbol 搜索关键词")) }
            },
            QStringList { QStringLiteral("operation") }),
        makeToolSchema(
            QStringLiteral("lsp_install"),
            QStringLiteral("安装/下载 LSP 语言服务（目前仅支持 clangd）。"),
            QJsonObject {
                { QStringLiteral("language"), makePropertySchema(QStringLiteral("string"), QStringLiteral("语言标识（默认 cpp）")) }
            })
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
    const QList<Tool> tools = codeIntelTools();
    for (const Tool& tool : tools)
        descriptor.toolNames.append(tool.name);
    return descriptor;
}

IToolProvider* CodeIntelToolsPlugin::createProvider(IToolPluginHost* host, QObject* parent)
{
    return new HostedToolProvider(host, codeIntelTools(), parent);
}
