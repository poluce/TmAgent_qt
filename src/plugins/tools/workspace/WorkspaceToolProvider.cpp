#include "FileTool.h"
#include "PatchTool.h"
#include "WorkspaceToolProvider.h"
#include "WorkspaceToolSchemas.h"

#include <QFileInfo>
#include <QJsonObject>
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

WorkspaceToolProvider::WorkspaceToolProvider(QObject* parent)
    : QObject(parent)
    , m_tools(workspaceTools())
{
}

QList<TmAgent::Tool> WorkspaceToolProvider::listTools() const
{
    return m_tools;
}

TmAgent::ToolResult WorkspaceToolProvider::execute(const TmAgent::ToolCall& call)
{
    const QString& toolName = call.name;
    QJsonObject args = call.input;
    args.insert(QStringLiteral("_tool_call_id"), call.id);

    if (toolName == QLatin1String("create_file"))
        return wrapSimpleResult(FileTool::executeCreateFile(args), QStringLiteral("[OK] 文件已创建"), QStringLiteral("[FAIL] 创建文件失败"));
    if (toolName == QLatin1String("view_file")) {
        const QString raw = FileTool::executeViewFile(args);
        return wrapResult(
            raw,
            QStringLiteral("[OK] 已读取 %1").arg(args.value(QStringLiteral("file_path")).toString()),
            QStringLiteral("[FAIL] 读取文件失败"));
    }
    if (toolName == QLatin1String("read_file_lines"))
        return wrapSimpleResult(FileTool::executeReadFileLines(args), QStringLiteral("[OK] 已读取指定行"), QStringLiteral("[FAIL] 读取文件行失败"));
    if (toolName == QLatin1String("replace_in_file"))
        return wrapSimpleResult(FileTool::executeReplaceInFile(args), QStringLiteral("[OK] 替换完成"), QStringLiteral("[FAIL] 替换失败"));
    if (toolName == QLatin1String("delete_file"))
        return wrapSimpleResult(FileTool::executeDeleteFile(args), QStringLiteral("[OK] 删除完成"), QStringLiteral("[FAIL] 删除失败"));
    if (toolName == QLatin1String("list_directory"))
        return wrapSimpleResult(FileTool::executeListDirectory(args), QStringLiteral("[OK] 已列出目录"), QStringLiteral("[FAIL] 列出目录失败"));
    if (toolName == QLatin1String("grep_search"))
        return wrapSimpleResult(FileTool::executeGrepSearch(args), QStringLiteral("[OK] 搜索完成"), QStringLiteral("[FAIL] 搜索失败"));
    if (toolName == QLatin1String("find_by_name"))
        return wrapSimpleResult(FileTool::executeFindByName(args), QStringLiteral("[OK] 搜索完成"), QStringLiteral("[FAIL] 搜索失败"));
    if (toolName == QLatin1String("insert_content"))
        return wrapSimpleResult(FileTool::executeInsertContent(args), QStringLiteral("[OK] 插入完成"), QStringLiteral("[FAIL] 插入失败"));
    if (toolName == QLatin1String("multi_replace_in_file"))
        return wrapSimpleResult(FileTool::executeMultiReplaceInFile(args), QStringLiteral("[OK] 多处替换完成"), QStringLiteral("[FAIL] 多处替换失败"));

    if (toolName == QLatin1String("send_file")) {
        const QString raw = FileTool::executeSendFile(args);
        const bool ok = isOkResult(raw);
        QString summary = ok
            ? QStringLiteral("[OK] 文件已发送: %1").arg(args.value(QStringLiteral("file_name")).toString())
            : QStringLiteral("[FAIL] 发送文件失败");
        QJsonObject data;
        if (ok) {
            const int pathStart = raw.indexOf(QStringLiteral("文件已发送 ")) + 6;
            const int pathEnd = raw.indexOf(QStringLiteral(" ("), pathStart);
            const QString filePath = raw.mid(pathStart, pathEnd - pathStart);
            QFileInfo fileInfo(filePath);
            data.insert(QStringLiteral("file_path"), filePath);
            data.insert(QStringLiteral("file_name"), args.value(QStringLiteral("file_name")).toString());
            data.insert(QStringLiteral("file_size"), fileInfo.size());
            data.insert(QStringLiteral("description"), args.value(QStringLiteral("description")).toString());
        }
        return TmAgent::ToolResult(raw, summary, ok, data);
    }

    if (toolName == QLatin1String("apply_patch"))
        return wrapSimpleResult(PatchTool::execute(args), QStringLiteral("[OK] 补丁已处理"), QStringLiteral("[FAIL] 补丁处理失败"));

    return TmAgent::ToolResult(
        QStringLiteral("错误: 未知的工具 %1").arg(toolName),
        QStringLiteral("执行失败"),
        false);
}
