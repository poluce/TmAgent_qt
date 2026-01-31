# 工具兼容层 + MCP Provider 计划

## 目标
- 保持 `ModelFactory` 仅负责模型 Provider 的职责边界。
- 新增“工具兼容层”，统一提供本地工具与 MCP 工具。
- 与现有 `LLMAgent`/`ToolDispatcher` 兼容，支持异步工具返回。

## 现状简述
- `LLMAgent` 通过 `ToolDispatcher` 获取工具 schema 并分发调用。
- `ToolDispatcher` 目前只管理本地 `ITool` 实例。
- 已有延迟工具返回机制（`__DEFERRED__` + `AgentEventBus::toolResultReady`）。

## 方案方向
新增一个工具工厂/提供者层（`ToolProvider` / `ToolFactory`），由 `ToolDispatcher` 聚合多个 Provider，完成 schema 汇聚与调用路由。

- 本地工具 Provider：封装现有 `ToolRegistry`/`ITool`。
- MCP Provider：从 MCP server 拉取 tools，映射为 `Tool` schema；执行时转发 MCP RPC；异步回传沿用现有机制。

## 关键接口草案
- `IToolProvider`（接口）
  - `QList<Tool> listTools() const`
  - `ToolResult execute(const ToolCall& call)`

- `ToolDispatcher`（扩展）
  - `void registerProvider(IToolProvider* provider, const QString& name)`
  - `QList<Tool> getAllToolSchemas()` 聚合所有 provider
  - `ToolResult dispatch(const ToolCall& call)` 依据 tool 名称路由到对应 provider

- `McpToolProvider`（新增）
  - 维护 MCP server 列表与 tool cache
  - `listTools()` 返回 MCP tool schema（缓存 + 失效策略）
  - `execute()` 发送 MCP 调用，返回同步结果或 deferred 结果

## 实施步骤
1. 抽象接口：新增 `IToolProvider`，并提供 `LocalToolProvider` 适配现有 `ITool`。
2. ToolDispatcher 改造：支持多个 provider 注册与路由。
3. MCP Provider 骨架：工具发现、schema 映射、执行转发、deferred 回传。
4. 接线：`LLMAgent` 仍只调用 `ToolDispatcher`。
5. 示例/测试：本地工具 + MCP 工具并存，延迟结果回传流程。

## 里程碑
- M1：`IToolProvider` + ToolDispatcher 聚合完成（本地工具不回退）。
- M2：`McpToolProvider` 可拉取 tools 并可执行至少一个工具。
- M3：异步工具回传打通（deferred + `toolResultReady`）。

## 风险与对策
- 工具名冲突：增加 provider 前缀或冲突检测策略。
- MCP 网络不稳定：缓存 + 超时 + 清晰错误提示。
- 异步回传时序：使用 `tool_call_id` 作为全局关联键。

## 验收标准
- 运行时可同时注册本地工具与 MCP 工具。
- LLMAgent 发起调用能正确路由并得到最终回答。
- 异步 MCP 工具能通过 `toolResultReady` 完成二次回复。
