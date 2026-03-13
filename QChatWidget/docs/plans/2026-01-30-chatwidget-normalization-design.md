# ChatWidget 规范化设计方案

## 背景与问题
当前 ChatWidget 模块对外暴露三层 API（ChatWidget / ChatWidgetView / ChatWidgetModel），但职责边界不够清晰，接口存在重复与语义重叠，文档与实现不完全对齐。结果是“能用但不够规范”，对外使用难以形成稳定的最佳实践。

## 目标
- 明确三层职责与边界，规范 API 层级。
- 收敛与统一命名，减少歧义与重载滥用。
- 形成可执行的“黄金路径”与高级路径（Model/View）。
- 同步更新资源与示例，保证新规范可直接落地。
- 允许不兼容变更，以清理历史包袱。

## 非目标
- 不新增功能类型（如音视频、网络传输）。
- 不替换 Markdown 引擎或更改渲染体系。
- 不覆盖 chatlist / modelconfig 模块。

## 范围
- 代码：`src/chatwidget/` 为主。
- 同步调整：`common/`、`resources/`、`test/`。
- 文档：新增规范化设计文档，更新开发者手册与示例说明。

## 架构分层与职责
- **ChatWidget（入口层）**：负责组装、流程协调与“黄金路径”接口（默认可用体验），向下驱动 View/Model。
- **ChatWidgetView（展示层）**：只处理展示与交互，不承担数据变换或业务流程。
- **ChatWidgetModel（数据层）**：只管理消息数据与状态，负责排序、去重、更新与检索。

**规则**：
- 数据变换（去重、排序、参与者归并）只允许在 Model 或独立转换器中完成。
- View 只保留展示相关接口（如 set/append/prependMessages、selection、highlight、context menu）。
- ChatWidget 统一对外转发更新类接口，形成稳定入口。

## API 规范化策略
- **减少重载与歧义**：用结构体参数替代多重重载（如 `addMessage(...)`）。
- **合并语义重复**：`setCurrentUserId` 与 `setCurrentUser` 合并为 `setCurrentUser`；`updateParticipant` 内部化，仅保留 `upsertParticipant`。
- **统一命名前缀**：消息更新类方法统一为 `updateMessage*`。
- **显式前置条件**：`streamOutput` 与 `sendingState` 的依赖关系写入注释与文档。
- **分层最小暴露**：View 保留最少更新接口，Model 作为高级入口，ChatWidget 提供黄金路径。

## 数据流与交互
- 用户输入 -> ChatWidget ->（生成 message）-> Model -> View。
- 外部更新（状态/内容/附件）-> ChatWidget -> Model -> View。
- 高级用户可直接调用 Model，并由 View 订阅变化。

## 资源与样式
- 统一样式文件命名与入口调用方式（`applyStyleSheetFile`）。
- 保证 `resources/styles.qrc` 与新入口一致；示例显式加载样式。

## 示例与文档
- 更新 `docs/ChatWidget开发者手册.md`：增加“黄金路径 + 高级路径”说明。
- `test/` 示例至少包含：
  - 最短可运行示例（仅 ChatWidget）。
  - 高级示例（直接操作 Model）。

## 测试建议
- 模型层：排序/去重/状态更新/参与者更新单元测试。
- 示例验证：至少一个示例工程可编译运行，验证资源加载与流式输出。

## 迁移策略（允许不兼容）
- 以新 API 为主线，提供旧接口到新接口的映射表。
- 对外文档标注“破坏性变更点”，并给出迁移示例。

## 交付物
- 规范化设计文档（本文）。
- 更新后的开发者手册与示例。
- API 变更与迁移说明。
