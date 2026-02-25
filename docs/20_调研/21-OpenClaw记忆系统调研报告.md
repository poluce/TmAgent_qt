# OpenClaw 记忆系统调研报告

> 状态：reference（调研输入，不直接作为开发指令）
> 阅读建议：先读 `docs/20_调研/22-调研结论汇总.md`，再按需查看本原始报告。

## 一、核心理念

OpenClaw 的记忆哲学：**"Mental notes don't survive session restarts. Files do."**

模型本身不保留任何信息，所有记忆都以 Markdown 文件存储在本地磁盘，这是唯一的真实来源（Single Source of Truth）。

---

## 二、文档体系（6 类核心文件）

存储位置：`~/.openclaw/workspace/`

| 文件 | 类比 | 作用 | 每轮注入 |
|------|------|------|----------|
| **SOUL.md** | 灵魂/人格 | 定义 Agent "是谁"：核心准则、行为边界、语气风格 | 是 |
| **IDENTITY.md** | 身份证 | Agent 的外在标识：名字、类型、气质、头像、emoji | 是 |
| **USER.md** | 用户画像 | 记录用户信息：称呼、时区、偏好、正在做的项目 | 是 |
| **AGENTS.md** | 行为总纲 | 工作空间级配置：会话协议、安全规则、心跳、技能 | 是 |
| **MEMORY.md** | 长期记忆 | 精选的持久事实、决策、偏好（跨会话） | 是 |
| **memory/YYYY-MM-DD.md** | 日记本 | 每日原始日志，追加式记录 | 否（按需检索） |

额外文件：
- `TOOLS.md` — 工具环境说明
- `HEARTBEAT.md` — 心跳/主动轮询配置
- `BOOTSTRAP.md` — 首次引导脚本（用完自毁）
- `bank/` 目录（实验性）— world.md、experience.md、opinions.md、entities/*.md

---

## 三、各文档详细说明

### 3.1 SOUL.md — 灵魂文档

**定位**：Agent 的人格定义文件，标语 *"You're not a chatbot. You're becoming someone."*

**结构**：
1. **Core Truths（核心真理）** — 5 条行为准则
   - 真诚帮助：跳过客套话，直接帮忙
   - 拥有观点：不做没个性的工具，要有自己的偏好
   - 自主解决：先查文件和上下文，尽量自己解决
   - 建立信任：内部操作大胆，外部操作谨慎
   - 尊重隐私：意识到拥有用户私人信息的访问权限

2. **Boundaries（边界）** — 4 条硬性规则
   - 保护隐私：不泄露私人信息
   - 外部操作需确认：不确定时先问再做
   - 不发残缺消息：避免不完整或格式混乱的消息
   - 群组场景谨慎：代表用户发言时格外小心

3. **Vibe（语气风格）** — 对话式且有能力，既不企业公文也不谄媚讨好

4. **Continuity（连续性）** — "These files ARE your memory. Read them. Update them."

5. **Evolution Note（进化说明）** — Agent 可自我修改此文件，但必须告知用户

**关键特性**：
- 子 Agent 不加载 SOUL.md，仅主 Agent 使用
- 存在 SOUL_EVIL.md 替换机制（测试/行为变异用）

### 3.2 IDENTITY.md — 身份文档

**定位**：Agent 的外在身份标识，"这是你开始弄清楚自己是谁的起点"

**字段**：

| 字段 | 用途 |
|------|------|
| Name | Agent 的名字 |
| Creature | 实体类型（AI？机器人？精灵？） |
| Vibe | 性格/气质表现（犀利？温暖？混乱？） |
| Emoji | 签名表情符号 |
| Avatar | 头像图片（支持路径、URL、data URI） |

**与 SOUL.md 的区别**：
- IDENTITY = "我是谁"（外在身份标识）
- SOUL = "我怎么做"（内在行为准则）
- 两者共同构成 Agent 的完整"自我认知"

### 3.3 USER.md — 用户文档

**定位**：记录用户个人信息，让 AI 跨会话记住用户偏好

**字段**：

| 字段 | 用途 | 是否必填 |
|------|------|----------|
| Name | 用户正式名称 | 是 |
| What to call them | 偏好称呼方式 | 是 |
| Pronouns | 代词偏好 | 可选 |
| Timezone | 所在时区 | 是 |
| Notes | 其他备注 | 可选 |

**Context 部分**（开放式，逐步积累）：
- 用户关心什么
- 正在进行的项目
- 什么让他们烦恼/开心

**伦理原则**：*"You're learning about a person, not building a dossier."*

### 3.4 AGENTS.md — 行为总纲

**定位**：工作空间级核心配置，定义 Agent 的行为规范

**核心内容**：
1. **工作区文件体系** — 定义所有文件的角色和加载规则
2. **会话协议** — 每次启动时读取哪些文件
3. **记忆哲学** — 区分原始日志和策划的长期记忆
4. **安全规则** — 禁止目录转储、破坏性命令需许可
5. **心跳系统** — 主动轮询机制（检查邮件、日历等）
6. **技能系统** — 每个技能有独立的 SKILL.md
7. **会话路由** — 直接聊天 vs 群组的路由规则

### 3.5 MEMORY.md — 长期记忆

**定位**：精选的持久记忆，存储决策、偏好和持久事实

**写入规则**：
- 决策/偏好/持久事实 → MEMORY.md
- 日常笔记/运行上下文 → memory/YYYY-MM-DD.md
- 用户说"记住这个"时必须写入磁盘

**类型前缀**：
- `W` — 世界知识
- `B` — 经历记录
- `O` — 观点（含置信度 `O(c=0.95)`）
- `S` — 观察

**实体标记**：`@Peter`、`@warelay`

### 3.6 memory/YYYY-MM-DD.md — 每日日志

**定位**：追加式原始日志，记录日常笔记和运行上下文

**特点**：
- 会话启动时自动读取今天和昨天的内容
- 不自动注入 system prompt，通过 `memory_search` / `memory_get` 按需检索
- `## Retain` 部分包含 2-5 个叙事性事实

---

## 四、System Prompt 组装流程

```
System Prompt = 固定部分 + 注入文件 + 运行时信息

固定部分: Tooling → Safety → Skills(懒加载) → Self-Update
    ↓
注入文件（按顺序，每轮消耗 token）:
  AGENTS.md → SOUL.md → TOOLS.md → IDENTITY.md
  → USER.md → HEARTBEAT.md → MEMORY.md
  → BOOTSTRAP.md（仅首次）
    ↓
运行时: Date/Time + Runtime + Sandbox + Reasoning
```

**关键限制**：
- 单文件最大 20,000 字符，超出截断
- 子 Agent 只注入 AGENTS.md + TOOLS.md（minimal 模式）
- Skills 懒加载：system prompt 只含名称+描述，需要时才读取 SKILL.md

**三种提示模式**：
- `full`（默认）— 包含所有部分
- `minimal`（子代理）— 仅 Tooling、Safety、Workspace、Sandbox、Runtime
- `none` — 仅基础身份行

---

## 五、记忆生命周期：Retain → Recall → Reflect

### 5.1 Retain（写入）
- 决策/偏好/持久事实 → `MEMORY.md`
- 日常笔记/运行上下文 → `memory/YYYY-MM-DD.md`
- 用户说"记住这个"时必须写入磁盘
- 支持类型前缀和实体标记

### 5.2 Recall（检索）
- **混合搜索**：`finalScore = 0.7 * vectorScore + 0.3 * textScore`
- 工具：`memory_search`（语义搜索）、`memory_get`（读取特定文件）
- 派生索引：SQLite FTS5 + 可选向量嵌入
- 索引始终可从 Markdown 重建
- 返回片段（约 700 字符上限）、文件路径、行范围、评分

### 5.3 Reflect（反思）
- 定期从日志中提炼更新实体摘要
- 基于强化/矛盾更新观点置信度
- 提议编辑核心记忆文件
- 观点演化：每个观点包含陈述、置信度 `c ∈ [0,1]`、最后更新时间和证据链接

---

## 六、会话与压缩机制

### 6.1 会话管理
- **会话键格式**：`agent:<agentId>:<mainKey>`
- **DM 隔离级别**：main / per-peer / per-channel-peer / per-account-channel-peer
- **每日重置**：默认凌晨 4:00，当会话最后更新早于最近重置时间时过期
- **空闲重置**（可选）：`idleMinutes` 滑动空闲窗口
- **手动重置**：`/new` 或 `/reset` 命令
- **状态存储**：`sessions.json` + `.jsonl` 转录记录

### 6.2 Context 上下文管理
- Context ≠ Memory：memory 存储在磁盘，context 是当前在模型窗口内的内容
- Context 组成：System prompt + Conversation history + Tool calls/results
- 管理命令：`/status`、`/context list`、`/context detail`、`/usage tokens`、`/compact`

### 6.3 Compaction（压缩）
- **自动触发**：上下文窗口接近上限时
- **手动触发**：`/compact` 命令
- **机制**：旧对话总结为摘要，保留最近消息完整，摘要持久化到 JSONL
- **预压缩 memory flush**：压缩前触发 silent agentic turn，提醒模型将重要信息写入磁盘
- **Compaction vs Pruning**：Compaction 总结并持久化；Pruning 仅修剪工具结果，不持久化

---

## 七、存储架构

### 规范存储（Markdown）
```
~/.openclaw/workspace/
  SOUL.md              # 灵魂/人格
  IDENTITY.md          # 身份标识
  USER.md              # 用户画像
  AGENTS.md            # 行为总纲
  MEMORY.md            # 长期记忆
  TOOLS.md             # 工具说明
  HEARTBEAT.md         # 心跳配置
  BOOTSTRAP.md         # 首次引导（用完自毁）
  memory/
    YYYY-MM-DD.md      # 每日日志
  bank/                # 实验性记忆页面
    world.md
    experience.md
    opinions.md
    entities/*.md
```

### 派生索引（SQLite）
- 位置：`~/.openclaw/memory/<agentId>.sqlite`
- FTS5 用于词法检索（BM25）
- 可选嵌入表用于语义检索
- 分块策略：约 400 token 目标，80 token 重叠
- 嵌入缓存：默认最大 50,000 条目

### 嵌入提供商优先级
1. `local` — 本地模型（默认 embeddinggemma-300M）
2. `openai` — OpenAI API
3. `gemini` — Gemini API
4. `voyage` — Voyage API

---

## 八、设计哲学总结

1. **Agent 是有个性的实体** — 不是无差别工具，SOUL.md 可自我进化
2. **文件即记忆** — 不依赖上下文窗口，Markdown 是唯一真实来源
3. **内外有别** — 内部操作自主大胆，外部操作谨慎确认
4. **渐进式复杂度** — 从简单 FTS 开始，按需升级到向量搜索
5. **隐私优先** — "你是在了解一个人，不是在建立档案"
6. **真诚优于客套** — 反对 AI 常见的讨好式回复
