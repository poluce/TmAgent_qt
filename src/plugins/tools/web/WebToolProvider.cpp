#include "WebToolProvider.h"

#include "ExternalSearchTool.h"
#include <tmagent/support/ToolSchemaSupport.h>
#include "WebTool.h"

#include <QRegularExpression>

namespace {

QList<TmAgent::Tool> buildWebTools()
{
    return {
        makeToolSchema(
            QStringLiteral("web_fetch"),
            QStringLiteral("获取指定 URL 的网页内容并转换为 Markdown 格式。"),
            QJsonObject {
                { QStringLiteral("url"), makePropertySchema(QStringLiteral("string"), QStringLiteral("网页 URL")) },
                { QStringLiteral("format"), makePropertySchema(QStringLiteral("string"), QStringLiteral("返回格式（text/markdown，默认 markdown）")) }
            },
            QStringList { QStringLiteral("url") }),
        makeToolSchema(
            QStringLiteral("websearch"),
            QStringLiteral("搜索互联网获取实时信息。"),
            QJsonObject {
                { QStringLiteral("query"), makePropertySchema(QStringLiteral("string"), QStringLiteral("搜索关键词")) }
            },
            QStringList { QStringLiteral("query") })
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

WebToolProvider::WebToolProvider(QObject* parent)
    : QObject(parent)
    , m_tools(buildWebTools())
{
}

QList<TmAgent::Tool> WebToolProvider::toolSchemas()
{
    return buildWebTools();
}

QList<TmAgent::Tool> WebToolProvider::listTools() const
{
    return m_tools;
}

TmAgent::ToolResult WebToolProvider::execute(const TmAgent::ToolCall& call)
{
    QJsonObject input = call.input;
    input.insert(QStringLiteral("_tool_call_id"), call.id);

    if (call.name == QStringLiteral("web_fetch")) {
        return wrapSimpleResult(
            WebTool::executeWebFetch(input),
            QStringLiteral("[OK] 网页抓取完成"),
            QStringLiteral("[FAIL] 网页抓取失败"));
    }
    if (call.name == QStringLiteral("websearch")) {
        return wrapSimpleResult(
            ExternalSearchTool::executeWebSearch(input),
            QStringLiteral("[OK] 网页搜索完成"),
            QStringLiteral("[FAIL] 网页搜索失败"));
    }

    return TmAgent::ToolResult(
        QStringLiteral("错误: 未知的工具 %1").arg(call.name),
        QStringLiteral("执行失败"),
        false);
}
