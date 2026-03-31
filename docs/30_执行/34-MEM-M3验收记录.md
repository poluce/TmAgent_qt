# MEM-M3 验收记录（记忆 M3 收口）

> 状态：active（已验收记录）
> 日期：2026-02-18
> 实现验收：通过
> 发布验收：不通过
> 依据：`docs/30_执行/30-当前执行主线.md`、`docs/10_方案/11-记忆系统规划方案.md`

---

## 1. 验收范围

1. SQLite FTS5 派生索引可重建，且不替代 Markdown 真源。
2. `memory_search` 工具可用（索引优先，失败回退 Markdown）。
3. `memory_reindex` 工具可用（按单助手/全助手重建）。
4. 索引生命周期接线完整（记忆写入后自动重建并发事件）。
5. 用户可从设置页手动触发索引重建并获取结果摘要。
6. 索引损坏场景可自动自愈（删库重建 + 检索重试）。

## 2. 验收项结果

1. FTS5 派生索引重建：通过。
说明：`MemoryManager::rebuildSearchIndex` 已实现索引重建并输出索引元数据。
证据：`src/core/memory/MemoryManager.cpp:603`

2. `memory_search` 工具注册与执行：通过。
说明：工具已注册，并可执行检索输出。
证据：旧静态工具注册头（现已移除）、`src/core/tools/MemoryTool.h:19`
补充：对应能力现由工具插件链路承接。

3. `memory_reindex` 工具注册与执行：通过。
说明：支持重建目标助手或全量助手索引。
证据：旧静态工具注册头（现已移除）、`src/core/tools/MemoryTool.h:138`
补充：对应能力现由工具插件链路承接。

4. 自动重建索引生命周期：通过。
说明：手动记忆和回合 retain 成功后会自动触发重建并发出 `memory.index.*` 事件。
证据：`src/core/service/ChatService.cpp:770`、`src/core/service/ChatService.cpp:1884`、`src/core/service/ChatService.cpp:2012`

5. 设置页手动重建入口：通过。
说明：信息设置页提供“一键重建记忆索引”按钮并展示重建结果。
证据：`src/ui/MainWindow.cpp:535`、`src/ui/MainWindow.cpp:554`、`src/ui/MainWindow.cpp:561`

6. 索引损坏自愈：通过。
说明：重建/检索命中损坏错误时自动删库并重建，检索端会自动重试一次。
证据：`src/core/memory/MemoryManager.cpp:645`、`src/core/memory/MemoryManager.cpp:664`、`src/core/tools/MemoryTool.h:74`、`src/core/tools/MemoryTool.h:447`

## 3. 结论说明

1. 已满足 `MEM-M3` 的实现验收口径：FTS5 派生索引、`memory_search` 与索引重建流程已落地。
2. 发布验收未通过，原因是完整 Qt/Windows 构建与交互回归尚未完成。
3. 在补齐完整构建与平台回归前，不应把本记录当作“发布通过”的依据。

## 4. 风险与后续

1. 风险：当前环境未执行完整 Qt/Windows 全量构建与交互回归。
2. 处置：在本地构建环境执行完整构建回归后，补充构建日志并更新发布验收结论。
3. 下一阶段建议：推进 `MEM-M4` 最小闭环，先落“质量评分与反思任务调度”，向量检索后置到后半程。

---

*文档版本：v2.0*
*创建日期：2026-02-18*
*更新日期：2026-03-20*
