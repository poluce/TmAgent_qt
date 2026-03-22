# MEM-M4 验收记录（记忆 M4 收口）

> 状态：draft（验收准备中）
> 日期：2026-03-20（按代码现状同步）
> 实现验收：进行中（代码已落地，待统一核验）
> 发布验收：未开始
> 依据：`docs/30_执行/30-当前执行主线.md`、`docs/10_方案/11-记忆系统规划方案.md`

---

## 1. 验收范围

1. 向量检索接入并与 BM25 混合排序协同工作。
2. 记忆质量评分与定期反思任务形成稳定闭环。
3. `session_search` 作为历史兜底检索可用。
4. M4 链路不破坏 Markdown 真源、Recall/Retain/Reflect 既有边界。

## 2. 验收项结果

1. `memory.reflected` / `memory.quality` 事件已落地。
代码依据：`src/core/service/MemoryMaintenanceService.cpp`
2. 向量索引文件 `memory_vector_index.json` 已落地。
代码依据：`src/core/tools/MemoryTool.cpp`
3. `memory_search` / `memory_reindex` / `session_search` 工具已注册。
代码依据：旧静态工具注册头（现已移除）
补充：对应能力现由工具插件链路承接。
4. 混合检索关键场景已有测试覆盖。
代码依据：`tests/tools/MemoryToolTest.cpp`

## 3. 结论说明

1. 本记录已作为 `MEM-M4` 的固定验收出口建立。
2. 当前代码已能证明 `MEM-M4` 关键能力存在，但尚未完成统一核验，因此不能标记为实现验收通过。
3. 在正式核验和平台回归完成前，不得将 `MEM-M4` 标记为发布验收通过。

## 4. 风险与后续

1. 风险：混合检索和反思评分仍在调优，效果稳定性尚未形成最终结论。
2. 后续：待主线补齐正式验证结果、效果证据和平台回归后更新本记录。

---

*文档版本：v1.0*
*创建日期：2026-03-20*
