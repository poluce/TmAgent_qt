# Agent 多模型支持落地 SOP（Qt Widgets）

> 目标读者：Agent 开发  
> 目标结果：多模型路由落地、DeepSeek 接入、UI 调试与回放可用  
> 约束：配置为 YAML；线程模型为 `QThread`；默认模型为 DeepSeek

---

## SOP-0 执行前准备

**输入**  
- 当前工程代码  
- DeepSeek API Key  

**步骤**  
1. 确认工程可编译运行  
2. 确认 `resources/` 可新增 YAML 文件  
3. 确认 UI 主界面可启动  

**完成标准**  
- UI 可启动  
- 配置路径明确  

**任务卡**  
T0.1 运行一次 UI 启动验证  
- 入口建议：`src/main.cpp`  
- 完成标准：窗口显示且可发送一条消息

---

## SOP-1 事件与 Envelope 基础

**新增文件**  
- `src/core/trace/TraceTypes.h`

**最小结构定义**  
- `TraceEvent`  
  - `trace_id`  
  - `span_id`  
  - `event_type`  
  - `timestamp`  
  - `payload`  
- `RequestEnvelope`  
  - `task_id / trace_id / agent_id / constraints / model_decision`  
- `ResultEnvelope`  
  - `result / usage / error / tool_calls`  
- `TraceEnvelope`  
  - `trace_id / events[] / started_at / ended_at`

**改动文件**  
- `src/core/agent/AgentEventBus.h`  
  - 新增 `postTraceEvent(const TraceEvent&)`  
  - 新增 `traceEventReceived` 信号  
- `src/core/agent/LLMAgent.h/.cpp`  
  - `sendRequest` 中创建 `trace_id`  
  - `onDelta/onToolCalls/onFinished/onClientError` 发 `TraceEvent`

**完成标准**  
- TraceEvent 可被 UI 订阅  
- 每次请求至少产出 1 条 trace 事件  

**任务卡**  
T1.1 设计 TraceTypes 数据结构  
- 文件：`src/core/trace/TraceTypes.h`  
- 约束：仅包含 POD 结构体与 `enum class TraceEventType`  
- 建议字段：`trace_id`、`span_id`、`event_type`、`timestamp_ms`、`payload`（`QJsonObject`）  

T1.2 扩展 EventBus  
- 文件：`src/core/agent/AgentEventBus.h`  
- 新增接口：`postTraceEvent(const TraceEvent&)`  
- 新增信号：`traceEventReceived(const TraceEvent&)`  

T1.3 在 LLMAgent 发 Trace 事件  
- 文件：`src/core/agent/LLMAgent.cpp`  
- 插入点：`sendRequest`、`onDeltaReceived`、`onToolCallsReceived`、`onClientFinished`、`onClientError`  
- 要求：同一请求复用 `trace_id`；每个阶段 `event_type` 不同  

---

## SOP-2 路由与 Provider 体系

**新增文件**  
- `src/core/llm/ModelRouter.h/.cpp`  
- `src/core/llm/ModelFactory.h/.cpp`  
- `src/core/llm/LLMProvider.h/.cpp`

**方法级要求**  
- `ModelRouter::selectModel(request)`  
  - 输出 `model_id + fallback_chain + decision_reason`  
- `ModelFactory::getProvider(capabilities, constraints)`  
  - 返回可用 Provider  
- `LLMProvider::generate/generateStream`  
  - 统一输出 `error_code + user_message`

**完成标准**  
- 路由结果可用于实际调用  
- 回退链可被记录  

**任务卡**  
T2.1 定义模型选择输入  
- 文件：`src/core/llm/ModelRouter.h`  
- 入参建议：`capabilities[]`、`deadline_ms`、`budget_level`、`required_vendor`  

T2.2 产出回退链  
- 文件：`src/core/llm/ModelRouter.cpp`  
- 规则：默认返回 `deepseek-default`；失败时按 `fallback_chain` 回退  

T2.3 Provider 统一错误语义  
- 文件：`src/core/llm/LLMProvider.h/.cpp`  
- 统一输出：`error_code` + `user_message`  

---

## SOP-3 DeepSeek 接入与 YAML 配置

**新增文件**  
- `resources/models.yaml`  
- `src/core/utils/ModelConfigLoader.h/.cpp`

**YAML 最小字段**  
- `model_id`  
- `provider`  
- `protocol_type`  
- `endpoint` 或 `command`  
- `api_key`  
- `capabilities[]`  
- `default`

**启动流程**  
`AppSettings::load()` → `ModelConfigLoader::loadModels()` → 构建 Adapter/Provider → 注册 Router → `ModelFactory` 对外提供

**完成标准**  
- YAML 修改即可切换模型  
- 启动失败仅提示错误，不阻断 UI  

**任务卡**  
T3.1 设计 YAML 解析器  
- 文件：`src/core/utils/ModelConfigLoader.h/.cpp`  
- 输出：模型配置数组（带 `capabilities[]` 与 `default`）  

T3.2 DeepSeek 配置生效  
- 文件：`resources/models.yaml`  
- 要求：`default: true` 的模型为默认路由目标  

---

## SOP-4 HistoryManager 落地

**新增文件**  
- `src/core/agent/HistoryManager.h/.cpp`

**方法级要求**  
- `buildContext(session_id)` 只负责裁剪与拼接  
- `LLMAgent` 内部历史拼接迁移至 `HistoryManager`

**完成标准**  
- 历史逻辑集中在 HistoryManager  
- Agent 仅消费组装结果  

**任务卡**  
T4.1 提取历史组装逻辑  
- 文件：`src/core/agent/HistoryManager.h/.cpp`  
- 输出：`QJsonArray messages`  
- 约束：不关心模型调用与工具执行  

---

## SOP-5 UI 落地（聊天 + 调试 + 回放）

**组件清单（优先完成）**  
- Chat 主界面  
- MessageBubble（流式）  
- AgentTreeView  
- MessageFlowView  
- LLMIOInspector  
- ToolInvocationView  
- DecisionTraceView  
- TimelineReplayView  
- SettingsPanel

**事件驱动要求**  
- 聊天流式渲染仅依赖 `delta/complete` 事件  
- 决策与回退展示 `decision_reason`  
- 回放组件只依赖 `TraceEnvelope.events[]`

**接入点**  
- `src/ui/AgentChatWidget.cpp` 订阅 `traceEventReceived`

**完成标准**  
- UI 可展示流式内容  
- 调试面板可展示事件  
- Timeline 可回放  

**任务卡**  
T5.1 接入 Trace 事件  
- 文件：`src/ui/AgentChatWidget.cpp`  
- 动作：订阅 `traceEventReceived` 并驱动 UI 更新  

T5.2 调试面板最小落地  
- 要求：至少能显示 `event_type + timestamp + payload`  

---

## SOP-6 回放与调试验收

**Trace 回放要求**  
- 每个任务生成 `trace_id`  
- Timeline 可按 `trace_id` 回放事件序列  
- 工具调用可在 `ToolLogWidget` 完整展示 RAW 结果

**完成标准**  
- 任一任务可回放  
- 事件顺序一致  

**任务卡**  
T6.1 时间轴回放  
- 事件源：`TraceEnvelope.events[]`  
- 要求：按 `timestamp_ms` 排序回放  

---

## SOP-7 验收清单（必须全部通过）

- YAML 配置后启动成功，缺失 api_key 仅提示错误  
- 非流式调用能返回完整结果  
- 流式调用能增量渲染且不重复  
- 路由决策写入 RequestEnvelope  
- 人为触发 timeout 可走回退链  
- Tool 调用事件可在 ToolLogWidget 完整展示  
- 每个任务生成 trace_id，Timeline 可回放  

---

## 附录 A：事件类型枚举建议

- `LLM_REQUEST_START`  
- `LLM_DELTA`  
- `LLM_COMPLETE`  
- `LLM_ERROR`  
- `MODEL_SELECTED`  
- `MODEL_FALLBACK`  
- `TOOL_STARTED`  
- `TOOL_PROGRESS`  
- `TOOL_COMPLETED`

---

## 附录 B：YAML 示例（DeepSeek）

```yaml
models:
  - model_id: deepseek-default
    provider: openai_compatible
    protocol_type: http_sse
    endpoint: https://api.deepseek.com/chat/completions
    api_key: ${DEEPSEEK_API_KEY}
    capabilities: [text, tool_calling, code]
    default: true
```

---

## 附录 C：UI 组件映射与复用建议

- 聊天主界面：复用 `ChatWidget`  
- 工具日志：复用 `ToolLogWidget`  
- 调试面板：新增轻量 `TraceLogWidget`（显示事件流）  
- Timeline 回放：新增 `TraceTimelineWidget`  

---

## 附录 D：工程执行顺序清单（按依赖）

1. 定义 `TraceTypes`（SOP-1 / T1.1）  
2. 扩展 `AgentEventBus`（SOP-1 / T1.2）  
3. 在 `LLMAgent` 发 Trace 事件（SOP-1 / T1.3）  
4. 新增 `ModelRouter / ModelFactory / LLMProvider`（SOP-2）  
5. 新增 `ModelConfigLoader` 与 `models.yaml`（SOP-3）  
6. 接入 DeepSeek 作为默认模型（SOP-3 / T3.2）  
7. 新增 `HistoryManager` 并迁移历史组装（SOP-4）  
8. UI 订阅 trace 事件并展示（SOP-5 / T5.1）  
9. 时间轴回放（SOP-6 / T6.1）  
10. 走一遍 SOP-7 验收清单

---

## 附录 E：关键函数签名（最小可实现）

**TraceTypes.h**  
- `enum class TraceEventType { LLM_REQUEST_START, LLM_DELTA, LLM_COMPLETE, LLM_ERROR, MODEL_SELECTED, MODEL_FALLBACK, TOOL_STARTED, TOOL_PROGRESS, TOOL_COMPLETED };`  
- `struct TraceEvent { QString trace_id; QString span_id; TraceEventType event_type; qint64 timestamp_ms; QJsonObject payload; };`

**AgentEventBus.h**  
- `void postTraceEvent(const TraceEvent& event);`  
- `signals: void traceEventReceived(const TraceEvent& event);`

**ModelRouter.h**  
- `struct ModelSelectRequest { QStringList capabilities; int deadline_ms; QString budget_level; QString required_vendor; };`  
- `struct ModelSelectResult { QString model_id; QStringList fallback_chain; QString decision_reason; };`  
- `ModelSelectResult selectModel(const ModelSelectRequest& request) const;`

**ModelFactory.h**  
- `LLMProvider* getProvider(const QStringList& capabilities, const QJsonObject& constraints);`

**LLMProvider.h**  
- `LLMResponse generate(const LLMRequest& request);`  
- `void generateStream(const LLMRequest& request);`  
- `bool supports(const QString& capability) const;`

**HistoryManager.h**  
- `QJsonArray buildContext(const QString& session_id, const QJsonObject& userMsg, bool saveToHistory);`

---

## 附录 F：文件改动清单（最小集）

- 新增：`src/core/trace/TraceTypes.h`  
- 新增：`src/core/llm/ModelRouter.h/.cpp`  
- 新增：`src/core/llm/ModelFactory.h/.cpp`  
- 新增：`src/core/llm/LLMProvider.h/.cpp`  
- 新增：`src/core/agent/HistoryManager.h/.cpp`  
- 新增：`src/core/utils/ModelConfigLoader.h/.cpp`  
- 新增：`resources/models.yaml`  
- 修改：`src/core/agent/AgentEventBus.h`  
- 修改：`src/core/agent/LLMAgent.h/.cpp`  
- 修改：`src/ui/AgentChatWidget.cpp`  

