#include "ShellTool.h"
#include "ShellToolProvider.h"
#include "ShellToolSchemas.h"

#include <QRegularExpression>
#include <QString>

namespace {

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

ToolResult wrapResult(const QString& raw, const QString& okSummary, const QString& failSummary)
{
    const bool ok = isOkResult(raw);
    return ToolResult(raw, ok ? okSummary : failSummary, ok);
}

ToolResult wrapSimpleResult(const QString& raw, const QString& okSummary, const QString& failSummary)
{
    return wrapResult(raw, okSummary, failSummary);
}

} // namespace

ShellToolProvider::ShellToolProvider(QObject* parent)
    : QObject(parent)
    , m_tools(shellTools())
{
}

QList<Tool> ShellToolProvider::listTools() const
{
    return m_tools;
}

ToolResult ShellToolProvider::execute(const ToolCall& call)
{
    const QString& toolName = call.name;
    QJsonObject input = call.input;
    input.insert(QStringLiteral("_tool_call_id"), call.id);
    if (toolName == QLatin1String("execute_command")) {
        return wrapSimpleResult(ShellTool::execute(input),
                                QStringLiteral("[OK] 命令执行完成"),
                                QStringLiteral("[FAIL] 命令执行失败"));
    }

    return ToolResult(QStringLiteral("错误: 未知的工具 %1").arg(toolName),
                      QStringLiteral("执行失败"),
                      false);
}
