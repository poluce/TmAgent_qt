# Agent 多模型支持设计蓝图（Qt Widgets）

> 目的：在不改动 Agent 核心逻辑的前提下，支持多种大模型来源（云端 API、本地模型、不同厂商、不同能力侧重），并在桌面端具备可调试、可回放、可治理的工程形态。

---

## Part I. UI 层（Qt Widgets）

### 1. UI 范围

* UI 技术栈：Qt Widgets（QWidget + QObject + 信号槽 + QThread 或 QtConcurrent）
* UI 关注点：交互、渲染、状态展示、调试面板承载

### 2. UI 形态与功能

#### 2.1 微信式对话主界面

* 私聊：用户与顶层主 Agent
* 任务群：多个主 Agent 围绕任务协作
* @Agent：定向询问
* 系统消息：分配、完成、验收等

#### 2.2 流式渲染

* 支持 `generateStream` 的 chunk 增量更新
* 消息气泡可边生成边展示

#### 2.3 调试控制台（开发者视角）

建议 Tab 或 Dock 视图：

1. Agent Tree View
2. Message Flow View
3. LLM I/O Inspector
4. Decision Trace View
5. Tool Invocation View
6. Timeline / Replay View

#### 2.4 多角色视角

* 用户：只看结果与高层汇报
* 产品 Agent 视角：看拆解与进度
* 领域 Agent 视角：看自己负责范围
* 观察者或调试者：只读查看全量

#### 2.5 任务时间轴

面向用户与管理者：

* 时间排序与筛选
* 展示摘要、完成时间、参与 Agent
* 关联 ExperienceSnapshot 与验收记录
* 一键打开 Debug Console 回放

#### 2.6 设置与运行期切换

* 设置面板展示模型能力描述 CapabilityDescriptor
* 支持运行期手动切换、自动降级与失败重试的交互入口

### 3. UI 与后端交互契约

* UI 通过顶层主 Agent 发起任务，并订阅事件流更新进度
* UI 的可观测性展示依赖 AgentEventBus 事件模型
* UI 的对话渲染与调试视图以 Envelope 与 Trace 字段做关联
* Trace 数据来源：调试/回放视图通过订阅 AgentEventBus，按 `trace_id` 过滤并聚合事件；若存在 Trace 查询服务，可通过 `trace_id` 拉取完整 TraceEnvelope，否则仅基于事件流在客户端聚合成 Trace 视图

---

## Part II. 后端与核心层

### 4. 目标与范围

#### 4.1 目标

* 通过统一入口与稳定接口，让 Agent 可按需使用不同模型
* 支持流式与非流式两种调用
* 支持多模型路由、并行、回退
* 支持工具调用的统一接管与权限控制
* 支持会话历史裁剪组装与可观测性
* 可选支持渐进式 Skill 加载与经验沉淀

#### 4.2 明确范围

* 协议：HTTP JSON、HTTP SSE、WebSocket、本地进程或本地 HTTP 服务
* 不纳入：gRPC、实时语音强实时链路

### 5. 总体设计原则

#### 5.1 能力抽象优先

Agent 依赖“能力标签”，模型仅提供这些能力的实现来源。

常见能力标签示例：

* 文本生成
* 结构化输出
* 代码生成
* 工具调用
* 多模态

#### 5.2 接口稳定与可插拔

Agent 上层仅依赖稳定抽象接口，例如 `LLMProvider`。

#### 5.3 解耦三要素

* 模型能力：ModelFactory 与 LLMProvider 体系
* 工具能力：ToolServer 与 Skill Registry
* 历史管理：HistoryManager

三者在架构上分离，通过明确的数据流与事件流连接。

### 6. 核心模块与职责边界

> 本节作为“接口与责任的单一事实来源”。

#### 6.1 ModelFactory（模型工厂，统一入口）

定位：应用级服务，提供模型能力出借。

职责：

* 统一创建与管理 `LLMProvider` 实例
* 按请求条件返回可用的 `LLMProvider`
* 屏蔽模型生命周期与复用策略
* 内部持有 ModelRouter 与模型目录（只读）

约束：

* 不保存会话状态
* 不管理工具
* 不编辑或裁剪历史

典型形态：QObject 单例或 ApplicationContext 服务。

#### 6.2 ModelRouter（模型选择策略组件）

定位：ModelFactory 内部组件，作为模型选择策略入口。

职责：

* 根据任务与约束选择模型
* 支持成本、延迟、可用性等策略
* 支持失败回退与降级

输入示例：任务类型、预算、延迟要求、必需能力标签。

输出：选定的 `model_id`，以及 `decision_reason`、`fallback_chain`。

约束：

* 不持有会话历史
* 不直接发起模型调用

#### 6.3 LLMProvider（统一模型调用接口）

定位：上层唯一依赖的模型调用抽象。

职责：

* 接收“已组装好的请求”（上下文由上层完成）
* 提供非流式与流式两套入口
* 返回标准化结果与标准化错误

约束：

* 不拼接历史
* 不直接执行工具

最小契约（建议）：

* `LLMRequest`：`request_id`、`trace_id`、`model_id`、`capabilities`、`stream`、`messages`
* `LLMResponse`：`result`、`usage`、`error`
* 流式事件：`delta`、`complete`、`error`（顺序一致）

失败语义边界：

* 统一输出 `error_code` 与可读 `user_message`
* 仅允许在 Provider 层基于 `DeadlineBudget` 与 `CancellationToken` 做预算内重试
* 连接/协议细节不在 Provider 层处理

概念级接口：

* `generate(request)`：非流式，一次性返回
* `generateStream(request)`：流式，逐段返回
* `supports(capability)`：能力探测

#### 6.4 ModelAdapter（厂商与实现适配层）

定位：对接具体厂商 SDK 或通信协议的适配器。

职责：

* 处理具体请求发送与异步回调
* 维护连接状态（如 WebSocket）
* 将原始响应转为统一事件或统一结果

边界约束（建议）：

* 仅处理厂商语义与协议细节（含 SSE/WS 分片解析、心跳、重连）
* 输出统一事件给 Provider，不改变上层调用语义
* 鉴权/限流/熔断策略由上层决定，Adapter 不独立执行

实现建议：QObject 派生类，使用 QNetworkAccessManager 或本地推理接口。

#### 6.5 ProtocolAdapter（协议子层，可选）

定位：将“协议细节”与“厂商语义”拆分，供 ModelAdapter 组合。

职责：

* HTTP JSON、HTTP SSE、WebSocket、本地进程等协议适配
* 处理重连、分片、SSE 事件解析、进程 IO 等底层细节

关系：

* ModelAdapter 组合 ProtocolAdapter
* 是否使用流式由上层 LLMProvider 的调用入口决定

协议与流式能力提示：

* HTTP JSON：通常只支持非流式
* HTTP SSE：天然支持流式
* WebSocket：天然支持流式
* LocalProcess：取决于实现

#### 6.6 CapabilityDescriptor（能力描述对象）

定位：运行时描述模型能力的只读对象。

用途：

* 设置面板与调试窗口展示
* ModelRouter 的能力匹配输入
* 日志与审计输出

字段示例：

* `context_length`
* `tool_calling`
* `code_quality`
* `cost_level`
* `latency`

#### 6.7 HistoryManager（会话历史管理层）

定位：对话历史的统一建模、编辑、裁剪与上下文组装。

职责：

* 将会话信息建模为结构化单元
* 按策略裁剪、摘要与拼装上下文
* 支持可回放与可审计的数据组织

约束：

* 不负责模型调用
* 不负责工具执行

建议：每个 Agent 绑定独立 HistoryManager 实例或独立策略，避免跨 Agent 污染。

#### 6.8 ToolServer（工具统一接管层）

定位：类似 MCP Server 的工具基础设施。

职责：

* 工具注册、发现、调用
* 权限控制与审计
* 执行环境隔离（按工程能力逐步增强）

ToolServer 同时承担 Skill Registry 的落地承载。

授权变更由 ToolServer 写入 EventBus 并归档到 TraceEnvelope。

#### 6.9 Skill Registry（技能注册表，渐进式加载入口）

定义：

* Tool：最小执行单元
* Skill：Tool 的组合，加上语义说明、约束与风险等级

Registry 字段建议：

* `skill_id`
* `description`
* `tools[]`
* `applicable_task_types[]`
* `cost_level`
* `risk_level`
* `requires_confirmation` 或 `requires_review`

### 7. Agent 组织模型（主 Agent 与子 Agent）

#### 7.1 结构

* 顶层主 Agent（Coordinator 或 Product Agent）：目标拆解与最终交付
* 中层主 Agent（Lead 或 Domain Agent）：领域拆分与阶段推进
* 子 Agent（Worker Agent）：单一、明确、可验证的子任务执行
* 校验 Agent（Review 或 Verifier Agent）：独立验收与质量控制

#### 7.2 关键约束

* Agent 之间通过显式消息协作，避免共享隐式上下文
* 不同 Agent 的历史隔离
* Agent 不与具体模型绑定，通过 ModelFactory 按次借用模型能力

### 8. 配置驱动（避免硬编码）

#### 8.1 配置覆盖范围

* 模型配置：厂商、协议、端点或命令、Key、能力标签
* 工具配置：工具列表、权限策略、按 Agent 授权规则

#### 8.2 启动加载与模型注册

建议在 ApplicationContext 或 CoreService 启动阶段：

1. 解析 JSON 或 YAML
2. 构建 ModelAdapter 与可选 ProtocolAdapter
3. 构建 LLMProvider
4. 向 ModelRouter 注册

模型配置字段示例：

* `model_id`
* `provider`
* `protocol_type`
* `endpoint` 或 `command`
* `api_key`
* `capabilities`

#### 8.3 运行期切换

支持：

* 手动切换
* 自动降级
* 失败重试

建议补充：重试次数、退避策略、超时策略、熔断策略。

### 9. 执行机制（并行、回退、流式、工具调用）

#### 9.1 典型调用链（落地主链路）

1. UI 发起任务给顶层主 Agent
2. Agent 调用 HistoryManager 组装上下文
3. Agent 向 ModelFactory 请求模型能力
4. ModelRouter 选择合适模型
5. 经 Scheduler 排队与限流后，LLMProvider 调用 ModelAdapter 执行请求
6. 若 LLM 返回中含 `tool_calls`：Agent（或编排层）将各次工具调用转交 ToolServer；ToolServer 先做授权检查，再执行 Tool，并写入事件
7. 结果返回给 Agent，写入事件与历史
8. UI 展示结果与进度

#### 9.2 并行调用

实现建议：

* QtConcurrent 或 QThread 每个调用独立执行
* 结果通过 signal 汇聚
* 由 Router 或评估器进行裁决

并行策略建议仅用于高价值任务，避免成本与延迟放大。

#### 9.3 回退机制

* Adapter 发出 error 或 timeout 信号
* Router 触发备用模型选择
* 上层 Agent 保持接口一致

#### 9.4 流式处理

* `generateStream` 对应 SSE 或 WebSocket 或本地流式实现

#### 9.5 工具调用

* LLMProvider 不直接执行工具，仅返回含 `tool_calls` 的 ResultEnvelope
* Agent（或编排层）在收到含 `tool_calls` 的 ResultEnvelope 后，将各次工具调用转交 ToolServer，由 ToolServer 做授权与执行
* 工具执行入口统一走 ToolServer；调用形态上以 `tool_id` 为准，Skill 用于注册、权限与可见性分组，ToolServer 按 tool_id 派发执行
* 授权策略支持最小权限与按任务回收

#### 9.6 关键缺失模块补齐（建议优先落地）

##### 9.6.1 Envelope 标准对象（贯穿全链路的数据骨架）

建议最小三件套：

* RequestEnvelope：任务元信息、约束、上下文引用、模型选择记录、超时预算
* ResultEnvelope：模型输出、结构化字段、工具调用记录、质量指标、错误对象
* TraceEnvelope：事件序列与关联标识集合

强制字段建议：`task_id`、`trace_id`、`agent_id`、`span_id`、`created_at`。

关联约定（建议）：

* RequestEnvelope 在 Agent 接收任务时生成并写入 `trace_id`
* EventBus 事件必须携带 `trace_id`、`span_id`，并可回填到 TraceEnvelope
* ResultEnvelope 由 Provider/ToolServer 产出并与 TraceEnvelope 关联

##### 9.6.2 Scheduler（全局调度与资源治理）

职责：

* 模型调用队列与并发控制
* 工具调用队列与并发控制
* 每模型限流策略与熔断策略
* 任务优先级与抢占策略
* 取消与超时预算的统一传播

调用链中的位置：所有对 LLMProvider 的调用、以及对 ToolServer 的调用，在进入执行前均经 Scheduler 排队与限流；Scheduler 由 ModelFactory / 编排层在发起请求前接入，或作为 Agent 与 ModelFactory 之间的统一入口。

##### 9.6.3 Evaluator（质量评估与裁决器）

职责：

* 结构化输出校验，例如 JSON Schema
* 可执行验证，例如编译、单测、静态检查
* 结果一致性检查，例如工具输出与声明结果的对齐
* 评分与排序，输出可解释的指标

触发时机：在并行多候选结果裁决时由 Router 或 Evaluator 进行裁决（见 9.2）；在校验 Agent 的验收阶段执行质量校验。主链路单次调用是否做轻量校验由实现策略决定。

##### 9.6.4 CancellationToken 与 DeadlineBudget（可控性基础）

要求：

* LLMProvider、ModelAdapter、ProtocolAdapter、ToolServer 支持取消
* 所有异步任务在结束时释放资源并写入终态事件

##### 9.6.5 Error Taxonomy（错误分类与用户可行动提示）

建议三层结构：

* error_code：timeout、rate_limited、auth_failed、protocol_error、model_refusal、tool_failed
* diagnostics：HTTP 状态、SSE 事件、堆栈与重试信息
* user_message：建议重试、切换模型、检查权限

归属约定：Provider 统一产出 `error_code` 与 `user_message`，Adapter 仅提供 diagnostics。

### 10. 可观测性、调试与回放

#### 10.1 AgentEventBus（事件总线）

原则：Agent 的关键行为以事件为中心建模，调试窗口与回放系统基于事件流构建。

事件建议包含：

* Agent 创建与销毁
* 父子关系变化
* 任务分配与完成
* 决策节点（拆分、验收、回退）
* LLM 请求与响应（脱敏）
* 工具调用请求与结果
* Skill 授权变化

事件字段最小集合：

* `agent_id`
* `parent_agent_id`
* `trace_id`
* `span_id`
* `event_type`
* `timestamp`
* `payload`（结构化）

### 11. 决策层中的 LLM 解释信号（Interpretation Signal）

定位：LLM 作为解释器提供结构化信号，供决策层参考。

设计要点：

* 输出为结构化中间结果
* 经过 JSON 解析与 Schema 校验
* 解析失败回退到默认规则或历史经验
* 不允许由该信号直接触发工具调用、授权或创建 Agent

概念级结构示例：

```json
{
  "task_type": "code_generation",
  "task_stage": "execution",
  "suggested_capabilities": ["code", "filesystem"],
  "risk_level": "medium",
  "confidence": 0.82,
  "notes": "涉及生成可运行代码"
}
```

可观测性：每次解释调用写入事件流，调试窗口可查看原始信号、校验结果与采纳情况。

### 12. 经验与知识沉淀（Experience & Knowledge）

#### 12.1 概念区分

* Session History：一次会话内的上下文与过程信息，可编辑、可裁剪、可丢弃
* Experience Knowledge：跨任务稳定沉淀的结论、方法、模式、偏好，可检索、可复用

#### 12.2 ExperienceManager（经验管理器）

职责：

* 接收成功任务的结果快照
* 结构化、摘要、标签化
* 持久化与查询

约束：

* 不直接参与实时对话与推理

#### 12.3 写入触发条件

* 用户确认满意
* 校验 Agent 验收通过
* 顶层主 Agent 标记为可沉淀

#### 12.4 使用方式

* 新任务执行时按需检索
* 返回相似任务、最近成功案例、偏好模式
* 采纳由 Agent 自行决定

### 13. 渐进式 Skill 加载（Progressive Skill Loading）

目的：控制工具暴露范围，降低上下文干扰，强化最小权限。

策略要点：

* 初始仅暴露基础 Skill
* 子任务阶段按需授予
* 高风险 Skill 需要用户确认或 Review
* 子 Agent 结束后自动回收

可观测性：授权变更写入事件流。

### 14. 常见反模式清单

* 用共享聊天上下文承载协作，缺少显式消息协议
* 试图用一个全局对话窗口承载全部协作与调试
* 让 Agent 之间共享 HistoryManager 或共享未经裁剪的上下文
* 在 LLMProvider 或 ModelAdapter 中拼接历史
* 在 Agent 内部保存模型实例或将对话状态放入 Adapter
* 在 Agent 里写死模型名或在 Prompt 中固化厂商字段
* 让模型输出来判断关键业务逻辑或权限决策

### 15. 一句话总结

在 Qt Widgets 架构中，多模型支持的关键在于：Agent 只依赖能力接口与信号槽，通过 ModelFactory 按次获取模型能力，通过 ToolServer 统一接管工具执行，通过 HistoryManager 统一管理上下文裁剪与组装。

---

## Part III. 开发指导（落地 SOP）

> 目标读者：Agent 开发。目标结果：多模型路由落地、DeepSeek 接入、UI 调试与回放可用。

### SOP-0 执行前准备

1. 统一使用 YAML 配置  
2. 线程模型固定为 `QThread`  
3. 默认模型为 DeepSeek，其它模型保留扩展点

### SOP-1 基础数据结构与事件流

**新增文件**  
- `src/core/trace/TraceTypes.h`

**最小结构**  
- `RequestEnvelope / ResultEnvelope / TraceEnvelope / TraceEvent`  
- 必含字段：`trace_id / span_id / event_type / timestamp / payload`

**改动文件**  
- `src/core/agent/AgentEventBus.h`  
  - 增加 `postTraceEvent(const TraceEvent&)`  
  - 增加 `traceEventReceived` 信号  
- `src/core/agent/LLMAgent.h/.cpp`  
  - 在 `sendRequest` 创建 `trace_id`  
  - 在 `onDelta/onToolCalls/onFinished/onClientError` 发 TraceEvent

### SOP-2 路由与 Provider 体系

**新增文件**  
- `src/core/llm/ModelRouter.h/.cpp`  
- `src/core/llm/ModelFactory.h/.cpp`  
- `src/core/llm/LLMProvider.h/.cpp`

**方法级要求**  
- `ModelRouter::selectModel(request)` 产出 `model_id + fallback_chain + decision_reason`  
- `ModelFactory::getProvider(capabilities, constraints)` 返回可用 Provider  
- `LLMProvider::generate/generateStream` 统一输出 `error_code + user_message`

### SOP-3 DeepSeek 接入与 YAML 配置

**新增文件**  
- `resources/models.yaml`  
- `src/core/utils/ModelConfigLoader.h/.cpp`

**YAML 最小字段**  
`model_id / provider / protocol_type / endpoint(or command) / api_key / capabilities[] / default`

**启动流程**  
`AppSettings::load()` → `ModelConfigLoader::loadModels()` → 构建 Adapter/Provider → 向 ModelRouter 注册各 `model_id` 与对应 Provider（或能力映射）→ `ModelFactory` 对外提供

### SOP-4 HistoryManager 落地

**新增文件**  
- `src/core/agent/HistoryManager.h/.cpp`

**方法级要求**  
- `buildContext(session_id)` 只负责裁剪与拼接  
- `LLMAgent` 内部历史拼接迁移至 `HistoryManager`

### SOP-5 UI 落地（聊天 + 调试 + 回放）

**组件清单（优先完成）**  
Chat 主界面、MessageBubble（流式）、AgentTreeView、MessageFlowView、LLMIOInspector、ToolInvocationView、DecisionTraceView、TimelineReplayView、SettingsPanel。

**事件驱动要求**  
- 聊天流式渲染仅依赖 `delta/complete` 事件  
- 决策与回退展示 `decision_reason`  
- 回放组件只依赖 `TraceEnvelope.events[]`

**接入点**  
- `src/ui/AgentChatWidget.cpp` 订阅 `traceEventReceived`

### SOP-6 回放与调试验收

**Trace 回放要求**  
- 每个任务生成 `trace_id`  
- Timeline 可按 `trace_id` 回放事件序列  
- 工具调用可在 `ToolLogWidget` 完整展示 RAW 结果

### SOP-7 验收清单（必须全部通过）

- YAML 配置后启动成功，缺失 api_key 仅提示错误  
- 非流式调用能返回完整结果  
- 流式调用能增量渲染且不重复  
- 路由决策写入 RequestEnvelope  
- 人为触发 timeout 可走回退链  
- Tool 调用事件可在 ToolLogWidget 完整展示  
- 每个任务生成 trace_id，Timeline 可回放  
