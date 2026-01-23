#ifndef FILEOPERATIONTOOLS_H
#define FILEOPERATIONTOOLS_H

#include "FileTool.h"
#include "core/agent/ToolRegistry.h"
#include "core/utils/ToolSchemaLoader.h"
#include "core/tools/ToolRegistrationHelpers.h"
#include <QJsonArray>

/**
 * @brief 创建文件工具实现
 */
class CreateFileTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("create_file", "在指定目录创建新文件");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeCreateFile(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 文件已创建", "[FAIL] 创建文件失败");
    }
};
REGISTER_TOOL_INSTANCE(CreateFileTool, "create_file")

/**
 * @brief 读取文件工具实现
 */
class ViewFileTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("view_file", "查看文件完整内容");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeViewFile(args);
        QString summary = QString("[OK] 已读取 %1").arg(args["file_path"].toString());
        return ToolRegistrationHelpers::wrapResult(res, summary, "[FAIL] 读取文件失败");
    }
};
REGISTER_TOOL_INSTANCE(ViewFileTool, "view_file")

/**
 * @brief 读取文件行工具实现
 */
class ReadFileLinesTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("read_file_lines", "读取文件指定行");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeReadFileLines(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 已读取指定行", "[FAIL] 读取文件行失败");
    }
};
REGISTER_TOOL_INSTANCE(ReadFileLinesTool, "read_file_lines")

/**
 * @brief 替换文件内容工具实现
 */
class ReplaceInFileTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("replace_in_file", "替换文件中的指定内容");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeReplaceInFile(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 替换完成", "[FAIL] 替换失败");
    }
};
REGISTER_TOOL_INSTANCE(ReplaceInFileTool, "replace_in_file")

/**
 * @brief 删除文件工具实现
 */
class DeleteFileTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("delete_file", "删除指定文件");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeDeleteFile(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 删除完成", "[FAIL] 删除失败");
    }
};
REGISTER_TOOL_INSTANCE(DeleteFileTool, "delete_file")

/**
 * @brief 列出目录工具实现
 */
class ListDirectoryTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("list_directory", "列出目录内容");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeListDirectory(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 已列出目录", "[FAIL] 列出目录失败");
    }
};
REGISTER_TOOL_INSTANCE(ListDirectoryTool, "list_directory")

/**
 * @brief 搜索内容工具实现
 */
class GrepSearchTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("grep_search", "在目录中搜索内容");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeGrepSearch(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 搜索完成", "[FAIL] 搜索失败");
    }
};
REGISTER_TOOL_INSTANCE(GrepSearchTool, "grep_search")

/**
 * @brief 按名称搜索工具实现
 */
class FindByNameTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("find_by_name", "按文件名模式搜索");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeFindByName(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 搜索完成", "[FAIL] 搜索失败");
    }
};
REGISTER_TOOL_INSTANCE(FindByNameTool, "find_by_name")

/**
 * @brief 插入内容工具实现
 */
class InsertContentTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("insert_content", "在文件指定行插入内容");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeInsertContent(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 插入完成", "[FAIL] 插入失败");
    }
};
REGISTER_TOOL_INSTANCE(InsertContentTool, "insert_content")

/**
 * @brief 多处替换工具实现
 */
class MultiReplaceInFileTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("multi_replace_in_file", "一次替换文件中多处内容");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = FileTool::executeMultiReplaceInFile(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 多处替换完成", "[FAIL] 多处替换失败");
    }
};
REGISTER_TOOL_INSTANCE(MultiReplaceInFileTool, "multi_replace_in_file")

#endif // FILEOPERATIONTOOLS_H
