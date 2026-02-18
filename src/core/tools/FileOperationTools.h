#ifndef FILEOPERATIONTOOLS_H
#define FILEOPERATIONTOOLS_H

#include "FileTool.h"
#include "core/agent/ToolRegistry.h"
#include "core/tools/ToolRegistrationHelpers.h"
#include <QJsonArray>

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

#endif // FILEOPERATIONTOOLS_H
