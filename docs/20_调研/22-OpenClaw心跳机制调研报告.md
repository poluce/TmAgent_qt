# OpenClaw 心跳机制调研报告

> 状态：reference（调研输入，不直接作为开发指令）
> 调研日期：2026-02-21
> 调研对象：openclaw/openclaw（GitHub 215k+ stars，TypeScript，MIT 协议）

---

## 一、核心概念：心跳 ≠ 网络心跳

OpenClaw 的"心跳"（Heartbeat）与传统网络编程中的 TCP keepalive / WebSocket ping-pong **完全不同**。

| 维度 | 传统网络心跳 | OpenClaw 心跳 |
|------|-------------|--------------|
| 目的 | 检测连接存活 | 触发 Agent 主动行为 |
| 方向 | 双向探测 | 系统 → Agent（单向唤醒） |
| 载荷 | 空包或极小 | 完整的 Agent Turn（含 system prompt） |
| 频率 | 秒级 | 分钟~小时级 |
| 失败处理 | 断线重连 | 指数退避 + 自动禁用 |

OpenClaw 的心跳本质是一个 **Cron 调度系统**，定期唤醒 Agent 执行预定义任务，赋予 Agent "内在生活"（inner life）。

---

## 二、架构总览

```
┌─────────────────────────────────────────────┐
│                CronService                   │
│  ┌─────────┐  ┌─────────┐  ┌─────────────┐ │
│  │  Store   │  │  Timer  │  │    Jobs      │ │
│  │(持久化)  │  │(调度器) │  │(任务管理)   │ │
│  └────┬─────┘  └────┬────┘  └──────┬──────┘ │
│       │             │              │         │
│       └─────────────┼──────────────┘         │
│                     │                        │
│              ┌──────▼──────┐                 │
│              │  Execution  │                 │
│              │   Engine    │                 │
│              └──────┬──────┘                 │
│                     │                        │
│         ┌───────────┼───────────┐            │
│         ▼           ▼           ▼            │
│    Main Session  Isolated    Webhook         │
│    (systemEvent) Agent Turn  Delivery        │
│                  (agentTurn)                  │
└─────────────────────────────────────────────┘
```

核心源码位置：`src/cron/`

| 文件/目录 | 职责 |
|-----------|------|
| `service.ts` | CronService 门面类，暴露 start/stop/add/run/wake 等 API |
| `service/timer.ts` | 定时器管理，到期执行，指数退避 |
| `service/ops.ts` | 操作实现（CRUD + wakeNow） |
| `service/jobs.ts` | 任务生命周期管理，调度计算，防雷群 |
| `service/state.ts` | 服务状态与依赖注入定义 |
| `service/store.ts` | 持久化存储（JSON 文件） |
| `schedule.ts` | 下次执行时间计算（支持 cron/every/at 三种模式） |
| `stagger.ts` | 防雷群（thundering herd）错峰策略 |
| `delivery.ts` | 执行结果投递计划 |
| `isolated-agent/run.ts` | 隔离 Agent Turn 执行 |
| `session-reaper.ts` | 过期会话清理 |
| `run-log.ts` | 执行日志（JSONL 格式） |

---

## 三、调度模型（三种 Schedule 类型）

### 3.1 Cron 表达式（`kind: "cron"`）

标准 cron 表达式，支持 5 字段和 6 字段（含秒）格式，使用 `croner` 库解析。

```typescript
{
  kind: "cron",
  expr: "0 8 * * *",      // 每天 8:00
  tz: "America/Chicago",   // 时区（默认系统时区）
  staggerMs: 300000         // 错峰窗口（可选）
}
```

### 3.2 固定间隔（`kind: "every"`）

```typescript
{
  kind: "every",
  everyMs: 7200000,        // 每 2 小时
  anchorMs: 1708300800000  // 锚点时间戳（可选）
}
```

计算逻辑：从 `anchorMs` 开始，每隔 `everyMs` 触发一次。若未设锚点，以当前时间为起点。

### 3.3 一次性执行（`kind: "at"`）

```typescript
{
  kind: "at",
  at: "2026-02-21T15:00:00Z"  // ISO 时间字符串
}
```

执行后自动禁用或删除（由 `deleteAfterRun` 控制）。

---

## 四、心跳配置实践

### 4.1 配置文件（openclaw.json）

```json
{
  "agentDefaults": {
    "heartbeat": {
      "schedule": "0 */2 8-23 * *",
      "tz": "America/Chicago"
    }
  },
  "cron": {
    "enabled": true,
    "maxConcurrentRuns": 4,
    "sessionRetention": "24h"
  }
}
```

心跳默认配置：活跃时段（8:00-23:00）每 2 小时触发一次。

### 4.2 HEARTBEAT.md（行为指令文件）

HEARTBEAT.md 是注入到 Agent system prompt 的文件，定义心跳触发时 Agent 应执行的任务清单：

```markdown
# Heartbeat

Checked every heartbeat cycle.

## Verify Scheduled Tasks
- 8:30 AM → morning brief should have posted
- (如果失败，手动执行并调查原因)

## Auth Check
- 任何任务因认证失败，立即通知人类
- 记录：哪个服务、什么错误

## Skip if
- 深夜（23:00-08:00）— 人类在睡觉
- 没有可操作的事项
```

### 4.3 典型日程编排（来自社区模板）

| 时间 | 任务 | 类型 |
|------|------|------|
| 08:00 | 晨间简报（日历+天气） | agentTurn |
| 09:00 | 自主学习/代码探索 | agentTurn |
| 10:25 | 随意问候（非整点，更自然） | agentTurn |
| 14:00 | 下午检查 | agentTurn |
| 15:47 | 不规则时间检查（模拟人类节奏） | agentTurn |
| 20:00 | 晚间检查 | agentTurn |
| 22:15 | 晚安消息 | agentTurn |
| 23:00 | 静默 git 备份 | systemEvent |
| 周日 20:00 | 每周记忆回顾 | agentTurn |

设计哲学：**故意使用非整点时间**（如 10:25、15:47），让 Agent 行为更像人类而非机器。

---

## 五、执行引擎详解

### 5.1 定时器机制（timer.ts）

```
armTimer() → setTimeout(onTimer, delay)
                    │
                    ▼
              onTimer() 触发
                    │
          ┌─────────┼─────────┐
          ▼         ▼         ▼
      找到到期任务  执行任务   重新 armTimer()
```

关键设计：

- **最大等待间隔**：60 秒。即使下个任务在 1 小时后，定时器也最多等 60 秒就重新检查，防止系统休眠/时钟跳变导致错过任务。
- **最小重触发间隔**：2 秒（`MIN_REFIRE_GAP_MS`），防止自旋循环。
- **定时器自愈**：即使任务正在执行，定时器也会重新 arm，确保调度器不会因异常而停止。

### 5.2 并发控制

```typescript
{
  maxConcurrentRuns: 4  // 最大并发执行数
}
```

当并发任务达到上限时，新到期任务会等待下一个定时器周期。

### 5.3 任务执行目标

| 目标 | 载荷类型 | 说明 |
|------|---------|------|
| `main` | `systemEvent` | 在主会话中触发系统事件（如 git 备份） |
| `isolated` | `agentTurn` | 创建隔离会话执行完整 Agent Turn |

隔离会话的 key 格式：`cron:<jobId>:run:<uuid>`

### 5.4 模型选择优先级

隔离 Agent Turn 执行时的模型选择链：

```
Job 指定模型 > Hook 模型 > 会话覆盖 > Agent 默认 > 系统默认
```

---

## 六、容错与恢复机制

### 6.1 指数退避（Exponential Backoff）

连续失败时自动增加重试间隔：

| 连续失败次数 | 退避时间 |
|-------------|---------|
| 1 | 30 秒 |
| 2 | 2 分钟 |
| 3 | 5 分钟 |
| 4 | 15 分钟 |
| 5+ | 60 分钟 |

### 6.2 自动禁用

- 连续 **3 次调度计算错误**（如 cron 表达式解析失败）→ 自动禁用任务
- 一次性任务（`at`）执行后任何终态 → 自动禁用，防止紧循环

### 6.3 卡死检测

运行超过 **2 小时** 的任务被判定为卡死（stuck），启动时清除其 `runningAtMs` 标记。

### 6.4 启动恢复

```typescript
// start() 中的恢复逻辑
1. 清除所有 stale 的 runningAtMs 标记
2. 保留已有的 nextRunAtMs（不重新计算，避免跳过到期任务）
3. 立即执行所有已过期的任务
4. arm 定时器
```

### 6.5 会话清理（Session Reaper）

- 默认保留 **24 小时**，可配置（如 `"48h"`）或禁用（`false`）
- 节流：最多每 **5 分钟** 执行一次清理
- 匹配模式：`...:cron:<jobId>:run:<uuid>` 的临时会话
- 锁安全：在 CronService 锁外部执行，避免死锁

---

## 七、防雷群策略（Anti-Thundering Herd）

### 7.1 问题

多个 cron 任务设置为整点执行（如 `0 * * * *`），会导致同一时刻大量任务并发。

### 7.2 解决方案：SHA-256 稳定哈希错峰

```typescript
// 使用任务 ID 的 SHA-256 哈希生成稳定偏移量
const offset = sha256(jobId) % staggerWindowMs;
const actualRunTime = scheduledTime + offset;
```

- 默认整点任务错峰窗口：**5 分钟**（`DEFAULT_TOP_OF_HOUR_STAGGER_MS`）
- 同一任务每次执行的偏移量相同（稳定哈希），便于调试
- 可通过 `staggerMs` 字段自定义窗口大小

### 7.3 自动检测

`isRecurringTopOfHourCronExpr()` 自动识别整点 cron 表达式（如 `0 * * * *`、`0 0 * * * *`），自动应用默认错峰。

---

## 八、执行日志与可观测性

### 8.1 运行日志（run-log.ts）

- 格式：JSONL（每行一条 JSON 记录）
- 存储：`runs/<jobId>.jsonl`，每个任务独立文件
- 自动修剪：文件超过 **2MB** 时保留最近 **2000 行**
- 并发安全：使用 Promise 链序列化同一文件的写入
- 原子写入：先写临时文件，再 rename

### 8.2 日志字段

```typescript
interface CronRunLogEntry {
  startedAtMs: number;      // 开始时间
  finishedAtMs: number;     // 结束时间
  durationMs: number;       // 执行耗时
  status: "ok" | "error" | "skipped";
  error?: string;           // 错误信息
  summary?: string;         // Agent 输出摘要
  sessionKey?: string;      // 会话标识
  // Token 使用统计
  inputTokens?: number;
  outputTokens?: number;
  cacheReadTokens?: number;
  cacheWriteTokens?: number;
}
```

### 8.3 CronEvent 事件

```typescript
type CronEvent = {
  action: "added" | "updated" | "removed" | "started" | "finished";
  jobId: string;
  jobName?: string;
  // ... 其他元数据
}
```

---

## 九、Wake 机制（即时唤醒）

除了定时触发，CronService 还提供即时唤醒能力：

```typescript
// CronService.wake()
wake(opts: {
  mode: "now" | "next-heartbeat";
  text: string;
})
```

| 模式 | 行为 |
|------|------|
| `now` | 立即触发一次 Agent Turn |
| `next-heartbeat` | 在下一个心跳周期触发 |

用途：外部事件（如收到消息、webhook 回调）需要唤醒 Agent 时使用。

---

## 十、安全机制

### 10.1 外部内容安全

隔离 Agent Turn 执行时，检测外部内容（如 Gmail hook 传入的邮件）：

```typescript
// 检测可疑模式并包裹安全边界
if (isExternalContent && !allowUnsafeExternalContent) {
  prompt = buildSafeExternalPrompt(prompt);
  // 记录可疑模式日志
}
```

防止通过心跳任务注入恶意 prompt。

### 10.2 子代理安全

- 心跳触发的 Agent Turn 可以产生子代理
- 子代理的技能列表受 skill-filter 控制
- 抑制中间消息（如 "on it, pulling everything together"）

---

## 十一、与 Agent 生命周期的关系

```
Agent 启动
    │
    ▼
CronService.start()
    │
    ├── 加载持久化的任务列表
    ├── 清除 stale 标记
    ├── 执行过期任务
    └── arm 定时器
         │
         ▼
    ┌──────────┐
    │ 运行循环  │◄──── wake() 即时唤醒
    │          │
    │ 每 ≤60s  │
    │ 检查到期  │
    │ 任务并执行│
    └──────────┘
         │
         ▼
Agent 关闭
    │
    ▼
CronService.stop()
    │
    └── 停止定时器（任务状态已持久化）
```

关键点：
- CronService 随 Agent 启动/停止
- 任务状态持久化到磁盘，重启后恢复
- 心跳不依赖网络连接，是本地调度
- Agent 的 HEARTBEAT.md 文件定义心跳时的行为指令

---

## 十二、设计哲学总结

1. **心跳 = 主动性**：不是检测存活，而是赋予 Agent 自主行动能力
2. **Cron 即心跳**：统一的调度模型，心跳只是 cron 任务的一种
3. **隔离执行**：每次心跳创建独立会话，不污染主对话
4. **优雅降级**：指数退避 + 自动禁用，防止失败风暴
5. **防雷群**：SHA-256 哈希错峰，避免整点并发
6. **可观测**：完整的执行日志 + 事件系统
7. **人性化节奏**：非整点调度，模拟人类行为模式
8. **文件驱动**：HEARTBEAT.md 定义行为，配置文件定义调度

---

## 十三、对 TmAgent_qt 的启示

| OpenClaw 特性 | TmAgent_qt 可借鉴点 |
|--------------|---------------------|
| Cron 调度引擎 | 可用 QTimer + cron 表达式解析实现类似调度 |
| 隔离会话执行 | 心跳触发时创建独立 Session，不干扰用户对话 |
| 指数退避 | 工具调用失败时的重试策略 |
| 防雷群 | 多 Agent 同时心跳时的错峰 |
| HEARTBEAT.md | 可作为 Agent 配置的一部分，定义主动行为 |
| 执行日志 | 心跳执行结果的持久化记录 |
| Wake 机制 | 外部事件触发 Agent 即时响应 |
| 会话清理 | 定期清理过期的心跳会话 |
