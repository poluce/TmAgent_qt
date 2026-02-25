#ifndef FILEOPERATIONTOOLS_H
#define FILEOPERATIONTOOLS_H

#include "FileTool.h"
#include "core/agent/ToolRegistry.h"
#include "core/tools/ToolRegistrationHelpers.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>

DEFINE_SIMPLE_TOOL(CreateFileTool, "create_file", "在指定目录创建新文件", FileTool::executeCreateFile, "[OK] 文件已创建", "[FAIL] 创建文件失败")

/**
 * @brief 读取文件工具实现（自定义摘要，不使用宏）
 */
class ViewFileTool : public ITool {
public:
    Tool getSchema() const override
    {
        return ToolRegistrationHelpers::resolveToolSchema("view_file", "查看文件完整内容");
    }

    ToolResult execute(const QJsonObject& args) override
    {
        QString res = FileTool::executeViewFile(args);
        QString summary = QString("[OK] 已读取 %1").arg(args["file_path"].toString());
        return ToolRegistrationHelpers::wrapResult(res, summary, "[FAIL] 读取文件失败");
    }
};
REGISTER_TOOL_INSTANCE(ViewFileTool, "view_file")

DEFINE_SIMPLE_TOOL(ReadFileLinesTool, "read_file_lines", "读取文件指定行", FileTool::executeReadFileLines, "[OK] 已读取指定行", "[FAIL] 读取文件行失败")

DEFINE_SIMPLE_TOOL(ReplaceInFileTool, "replace_in_file", "替换文件中的指定内容", FileTool::executeReplaceInFile, "[OK] 替换完成", "[FAIL] 替换失败")

DEFINE_SIMPLE_TOOL(DeleteFileTool, "delete_file", "删除指定文件", FileTool::executeDeleteFile, "[OK] 删除完成", "[FAIL] 删除失败")

DEFINE_SIMPLE_TOOL(ListDirectoryTool, "list_directory", "列出目录内容", FileTool::executeListDirectory, "[OK] 已列出目录", "[FAIL] 列出目录失败")

DEFINE_SIMPLE_TOOL(GrepSearchTool, "grep_search", "在目录中搜索内容", FileTool::executeGrepSearch, "[OK] 搜索完成", "[FAIL] 搜索失败")

DEFINE_SIMPLE_TOOL(FindByNameTool, "find_by_name", "按文件名模式搜索", FileTool::executeFindByName, "[OK] 搜索完成", "[FAIL] 搜索失败")

DEFINE_SIMPLE_TOOL(InsertContentTool, "insert_content", "在文件指定行插入内容", FileTool::executeInsertContent, "[OK] 插入完成", "[FAIL] 插入失败")

DEFINE_SIMPLE_TOOL(MultiReplaceInFileTool, "multi_replace_in_file", "一次替换文件中多处内容", FileTool::executeMultiReplaceInFile, "[OK] 多处替换完成", "[FAIL] 多处替换失败")

class SendFileTool : public ITool {
public:
    Tool getSchema() const override
    {
        return ToolRegistrationHelpers::resolveToolSchema("send_file", "将内容保存为文件发送给用户，用户可点击打开查看");
    }

    ToolResult execute(const QJsonObject& args) override
    {
        QString res = FileTool::executeSendFile(args);
        bool ok = ToolRegistrationHelpers::isOkResult(res);
        QString summary = ok ? QString("[OK] 文件已发送: %1").arg(args["file_name"].toString())
                             : QStringLiteral("[FAIL] 发送文件失败");

        QJsonObject data;
        if (ok) {
            // 从结果中提取文件路径
            // 结果格式: "成功: 文件已发送 <path> (<size> 字节)"
            QString fileName = args["file_name"].toString();
            // 从结果文本中提取实际路径
            int pathStart = res.indexOf(QStringLiteral("文件已发送 ")) + 6;
            int pathEnd = res.indexOf(QStringLiteral(" ("), pathStart);
            QString filePath = res.mid(pathStart, pathEnd - pathStart);
            QFileInfo fileInfo(filePath);

            data.insert("file_path", filePath);
            data.insert("file_name", fileName);
            data.insert("file_size", fileInfo.size());
            data.insert("description", args.value("description").toString());
        }
        return ToolResult(res, summary, ok, data);
    }
};
REGISTER_TOOL_INSTANCE(SendFileTool, "send_file")

#endif // FILEOPERATIONTOOLS_H
