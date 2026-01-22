---
description: 自动分析代码变更，生成符合 Conventional Commits 规范的日志，以 poluce_auto <fishsummer@126.com> 身份提交，并自动推送到远程仓库。
---

# 自动提交并推送代码工作流

// turbo-all

1. **检查最近的提交记录 (Baseline)**
   `git log -n 5 --oneline`

   > [!IMPORTANT]
   > 必须核对最近的提交内容，防止将已经入库的历史任务（如重构、删除等）重复写进本次提交信息中。

2. 检查当前 Git 状态并分析暂存区
   `git status`

3. 将目标变更添加到暂存区（如果尚未添加）
   `git add .`

4. **精确分析变更数据**（核心步骤）
   强制执行 `git diff --cached` 深度分析**本次提交**真正改变的内容。

   > [!IMPORTANT]
   > 严禁凭记忆描述任务。必须以 Diff 结果为唯一真理来源，准确区分“本次新增数据”与“历史已提交逻辑”。

5. **生成提交信息**
   提交信息必须基于第 1 步和第 4 步的真实对比结果，包含：
   - **类型**（feat, fix, refactor, docs, style, test, chore）
   - **简短描述**（Subject）
   - **详细描述（Body）**：必须列出**数据级的增量对比**。
     - **新增数据**：列出具体新增的文件或配置。
     - **实际变更**：描述从旧值到新值的具体变化（A -> B）。
     - **实际删除**：列出本次 Diff 中真正消失的行或文件。

## ⚠️ 避坑指南 (典型反例)

**问题场景**：
在同一个对话 Session 中，Agent 连续处理了多个重构任务（例如：1. 拆分 UI；2. 移除种子数据）。如果任务 1 已经 commit，但在生成任务 2 的 commit 信息时，Agent 的记忆中仍保留着任务 1 的内容，导致信息冗余甚至错误。

**解决办法**：

1. **强制环境核对**：在生成 Body 前，必须显式列出 `git diff --cached` 的结果，而不是依赖之前的对话记录。
2. **三方验证**：将 Staged 内容与最近一次 `git log` 内容对比，确保描述的是“增量变更”。

3. 配置本地临时 Git 身份并提交
   `git -c user.name="poluce_auto" -c user.email="fishsummer@126.com" commit -m "[生成的提交信息]"`

4. 推送到远程仓库
   `git push`
