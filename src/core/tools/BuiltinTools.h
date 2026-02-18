#ifndef BUILTINTOOLS_H
#define BUILTINTOOLS_H

#include "core/agent/ToolRegistry.h"
#include "core/tools/CodeParserTool.h"
#include "core/tools/ExternalSearchTool.h"
#include "core/tools/LspInstallTool.h"
#include "core/tools/LspTool.h"
#include "core/tools/MemoryTool.h"
#include "core/tools/PatchTool.h"
#include "core/tools/SessionSearchTool.h"
#include "core/tools/ShellTool.h"
#include "core/tools/ToolRegistrationHelpers.h"
#include "core/tools/WebTool.h"

DEFINE_SIMPLE_TOOL(ExecuteCommandTool, "execute_command", "执行终端命令", ShellTool::execute, "[OK] 命令执行完成", "[FAIL] 命令执行失败")

DEFINE_SIMPLE_TOOL(ViewFileOutlineTool, "view_file_outline", "查看代码文件大纲", CodeParserTool::executeViewFileOutline, "[OK] 已生成大纲", "[FAIL] 生成大纲失败")

DEFINE_SIMPLE_TOOL(ViewCodeItemTool, "view_code_item", "查看指定代码项", CodeParserTool::executeViewCodeItem, "[OK] 已获取代码项", "[FAIL] 获取代码项失败")

DEFINE_SIMPLE_TOOL(LspProxyTool, "lsp", "LSP 智能分析工具", LspTool::execute, "[OK] LSP 请求完成", "[FAIL] LSP 请求失败")

DEFINE_SIMPLE_TOOL(LspInstallProxyTool, "lsp_install", "安装 LSP 语言服务 (目前仅支持 clangd)", LspInstallTool::execute, "[OK] LSP 安装已触发", "[FAIL] LSP 安装失败")

DEFINE_SIMPLE_TOOL(WebFetchTool, "web_fetch", "抓取网页内容", WebTool::executeWebFetch, "[OK] 网页抓取完成", "[FAIL] 网页抓取失败")

DEFINE_SIMPLE_TOOL(WebSearchTool, "websearch", "网页搜索", ExternalSearchTool::executeWebSearch, "[OK] 网页搜索完成", "[FAIL] 网页搜索失败")

DEFINE_SIMPLE_TOOL(ApplyPatchTool, "apply_patch", "应用补丁", PatchTool::execute, "[OK] 补丁已处理", "[FAIL] 补丁处理失败")

DEFINE_SIMPLE_TOOL(MemorySearchTool, "memory_search", "检索助手记忆", MemoryTool::executeSearch, "[OK] 记忆检索完成", "[FAIL] 记忆检索失败")

DEFINE_SIMPLE_TOOL(MemoryReindexTool, "memory_reindex", "重建助手记忆检索索引", MemoryTool::executeRebuild, "[OK] 记忆索引重建完成", "[FAIL] 记忆索引重建失败")

DEFINE_SIMPLE_TOOL(SessionSearchToolImpl, "session_search", "检索会话历史", SessionSearchTool::executeSearch, "[OK] 会话历史检索完成", "[FAIL] 会话历史检索失败")

#endif // BUILTINTOOLS_H
