# MEM-M2 验收记录（记忆 M2 收口）

> 状态：active（已验收记录）
> 日期：2026-02-17
> 实现验收：通过
> 发布验收：不通过
> 依据：`docs/30_执行/30-当前执行主线.md`、`docs/10_方案/11-记忆系统规划方案.md`

---

## 1. 验收范围

1. 长期记忆提炼策略落地并可持续写入 `memory.md`。
2. 去重规则有效，避免长期记忆重复污染。
3. 手动“记住这条”入口可用，且可追溯 `trace_id/turn_id`。
4. 记忆事件可观测（`memory.updated/memory.compacted/memory.error`）并可在 UI 排障。
5. 记忆策略可配置（自动提炼开关、最小长度、每回合上限）。

## 2. 验收项结果

1. 长期提炼策略：通过。
说明：回合完成后根据规则提炼候选并写入 `memory.md`。
证据：`src/core/memory/MemoryManager.cpp:113`、`src/core/memory/MemoryManager.cpp:640`

2. 去重规则：通过。
说明：使用 `[fp:hash]` 指纹去重，已存在则跳过写入。
证据：`src/core/memory/MemoryManager.cpp:33`、`src/core/memory/MemoryManager.cpp:665`

3. 手动记住入口：通过。
说明：聊天气泡右键“记住这条”直连后端 `rememberMessageAs`，写入长期记忆。
证据：`src/ui/IdentityView.cpp:1017`、`src/core/service/ChatService.cpp:601`

4. 手动记忆可追溯：通过。
说明：无活跃回合时补齐合成回合标识，并带 `source_trace_id/source_turn_id`。
证据：`src/core/service/ChatService.cpp:692`、`src/core/service/ChatService.cpp:706`

5. 记忆事件观测：通过。
说明：`memory.*` 事件进入会话事件流并镜像到请求/响应历史。
证据：`src/core/service/ChatService.cpp:1499`、`src/core/service/ChatService.cpp:1511`

6. 记忆策略参数化：通过。
说明：新增 `memory_rules` 并在提炼时按策略执行。
证据：`src/core/memory/MemoryManager.cpp:269`、`src/core/memory/MemoryManager.cpp:623`

7. 设置页可配置策略：通过。
说明：信息设置页可配置自动提炼、最小长度、每回合上限，并写入 `memory_policy.json`。
证据：`src/ui/MainWindow.cpp:484`、`src/ui/MainWindow.cpp:694`

## 3. 结论说明

1. 已满足 `MEM-M2` 的实现验收口径：提炼策略、去重、手动入口均已落地。
2. 发布验收未通过，原因是完整 Windows/Qt 构建回归尚未完成。
3. 在补齐完整构建与交互回归前，不应把本记录当作“发布通过”的依据。

## 4. 风险与后续

1. 风险：当前会话环境下 `make` 无法完成对象编译（`Makefile:73 multiple target patterns`），未完成本机全量编译回归。
2. 处置：在 Windows/Qt 原生构建环境执行完整编译与回归后，再更新发布验收结论。
3. 下一阶段建议：推进 `MEM-M3`，落地 FTS5 派生索引与 `memory_search` 工具。

---

*文档版本：v2.0*
*创建日期：2026-02-17*
*更新日期：2026-03-20*
