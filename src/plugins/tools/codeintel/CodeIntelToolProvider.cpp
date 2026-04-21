#include "CodeIntelToolProvider.h"

#include "CodeParserTool.h"
#include "LspInstallTool.h"
#include "LspTool.h"
#include <tmagent/support/ToolSchemaSupport.h>

#include <QRegularExpression>

namespace {

QList<TmAgent::Tool> buildCodeIntelTools()
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

bool isOkResult(const QString& raw)
{
    const QString text = raw.trimmed();
    if (text.startsWith(QStringLiteral("错误"))
        || text.startsWith(QStringLiteral("抓取失败"))
        || text.startsWith(QStringLiteral("搜索失败"))) {
        return false;
    }

    static const QRegularExpression kExitCodeRe(
        QStringLiteral("(?:^|\\n)\\s*退出码\\s*:\\s*(-?\\d+)"));
    const QRegularExpressionMatch match = kExitCodeRe.match(text);
    if (match.hasMatch())
        return match.captured(1).toInt() == 0;

    return true;
}

TmAgent::ToolResult wrapResult(const QString& raw, const QString& okSummary, const QString& failSummary)
{
    const bool ok = isOkResult(raw);
    return TmAgent::ToolResult(raw, ok ? okSummary : failSummary, ok);
}

TmAgent::ToolResult wrapSimpleResult(const QString& raw, const QString& okSummary, const QString& failSummary)
{
    return wrapResult(raw, okSummary, failSummary);
}

} // namespace

CodeIntelToolProvider::CodeIntelToolProvider(TmAgent::IToolPluginHost* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_tools(buildCodeIntelTools())
{
}

QList<TmAgent::Tool> CodeIntelToolProvider::toolSchemas()
{
    return buildCodeIntelTools();
}

QList<TmAgent::Tool> CodeIntelToolProvider::listTools() const
{
    return m_tools;
}

TmAgent::ToolResult CodeIntelToolProvider::execute(const TmAgent::ToolCall& call)
{
    QJsonObject input = call.input;
    input.insert(QStringLiteral("_tool_call_id"), call.id);

    if (call.name == QStringLiteral("view_file_outline")) {
        return wrapSimpleResult(
            CodeParserTool::executeViewFileOutline(input, m_host),
            QStringLiteral("[OK] 已生成大纲"),
            QStringLiteral("[FAIL] 生成大纲失败"));
    }
    if (call.name == QStringLiteral("view_code_item")) {
        return wrapSimpleResult(
            CodeParserTool::executeViewCodeItem(input, m_host),
            QStringLiteral("[OK] 已获取代码项"),
            QStringLiteral("[FAIL] 获取代码项失败"));
    }
    if (call.name == QStringLiteral("lsp")) {
        return wrapSimpleResult(
            LspTool::execute(input),
            QStringLiteral("[OK] LSP 请求完成"),
            QStringLiteral("[FAIL] LSP 请求失败"));
    }
    if (call.name == QStringLiteral("lsp_install")) {
        return wrapSimpleResult(
            LspInstallTool::execute(input),
            QStringLiteral("[OK] LSP 安装已触发"),
            QStringLiteral("[FAIL] LSP 安装失败"));
    }

    return TmAgent::ToolResult(
        QStringLiteral("错误: 未知的工具 %1").arg(call.name),
        QStringLiteral("执行失败"),
        false);
}
