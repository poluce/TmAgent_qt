#ifndef BUILTINTOOLS_H
#define BUILTINTOOLS_H

#include <QJsonArray>
#include "core/agent/ToolRegistry.h"
#include "core/tools/ToolRegistrationHelpers.h"
#include "core/tools/ShellTool.h"
#include "core/tools/CodeParserTool.h"
#include "core/tools/LspTool.h"
#include "core/tools/LspInstallTool.h"
#include "core/tools/WebTool.h"
#include "core/tools/ExternalSearchTool.h"
#include "core/tools/PatchTool.h"

/**
 * @brief 执行命令工具实现
 */
class ExecuteCommandTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("execute_command", "执行终端命令");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = ShellTool::execute(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 命令执行完成", "[FAIL] 命令执行失败");
    }
};
REGISTER_TOOL_INSTANCE(ExecuteCommandTool, "execute_command")

/**
 * @brief 查看文件大纲工具实现
 */
class ViewFileOutlineTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("view_file_outline", "查看代码文件大纲");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = CodeParserTool::executeViewFileOutline(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 已生成大纲", "[FAIL] 生成大纲失败");
    }
};
REGISTER_TOOL_INSTANCE(ViewFileOutlineTool, "view_file_outline")

/**
 * @brief 查看代码项工具实现
 */
class ViewCodeItemTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("view_code_item", "查看指定代码项");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = CodeParserTool::executeViewCodeItem(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 已获取代码项", "[FAIL] 获取代码项失败");
    }
};
REGISTER_TOOL_INSTANCE(ViewCodeItemTool, "view_code_item")

/**
 * @brief LSP 工具实现
 */
class LspProxyTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("lsp", "LSP 智能分析工具");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = LspTool::execute(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] LSP 请求完成", "[FAIL] LSP 请求失败");
    }
};
REGISTER_TOOL_INSTANCE(LspProxyTool, "lsp")

/**
 * @brief LSP 安装工具实现
 */
class LspInstallProxyTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("lsp_install", "安装 LSP 语言服务 (目前仅支持 clangd)");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = LspInstallTool::execute(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] LSP 安装已触发", "[FAIL] LSP 安装失败");
    }
};
REGISTER_TOOL_INSTANCE(LspInstallProxyTool, "lsp_install")

/**
 * @brief 网页抓取工具实现
 */
class WebFetchTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("web_fetch", "抓取网页内容");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = WebTool::executeWebFetch(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 网页抓取完成", "[FAIL] 网页抓取失败");
    }
};
REGISTER_TOOL_INSTANCE(WebFetchTool, "web_fetch")

/**
 * @brief 网页搜索工具实现 (DuckDuckGo)
 */
class WebSearchTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("websearch", "网页搜索");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = ExternalSearchTool::executeWebSearch(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 网页搜索完成", "[FAIL] 网页搜索失败");
    }
};
REGISTER_TOOL_INSTANCE(WebSearchTool, "websearch")

/**
 * @brief 补丁工具实现
 */
class ApplyPatchTool : public ITool {
public:
    Tool getSchema() const override {
        return ToolRegistrationHelpers::resolveToolSchema("apply_patch", "应用补丁");
    }

    ToolResult execute(const QJsonObject& args) override {
        QString res = PatchTool::execute(args);
        return ToolRegistrationHelpers::wrapResult(res, "[OK] 补丁已处理", "[FAIL] 补丁处理失败");
    }
};
REGISTER_TOOL_INSTANCE(ApplyPatchTool, "apply_patch")

#endif // BUILTINTOOLS_H
