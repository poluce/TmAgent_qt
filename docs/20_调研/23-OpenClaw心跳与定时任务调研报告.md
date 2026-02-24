# OpenClaw 心跳与定时任务机制调研报告

> 状态：reference（调研输入，不直接作为开发指令）
> 阅读建议：先读本报告了解 OpenClaw 设计，再看 `docs/10_方案/14-心跳与定时任务方案.md` 了解落地计划。

---

## 一、调研来源

- **openclaw-mini**（GitHub: voocel/openclaw-mini）— OpenClaw 精简复现，heartbeat 模块与原版一一对应
- **Dithilli/openclaw-starter** — OpenClaw 官方工作区模板，含完整 Cron 配置示例
- **abakermi/clawpulse** — OpenClaw 独立 Cron 调度器（解决内置调度器可靠性问题）

---

## 二、OpenClaw 的两套定时机制

OpenClaw 明确区分两种定时机制：**Heartbeat（心跳）** 和 **Cron（定时任务）**。

| 维度 | Heartbeat（心跳） | Cron（定时任务） |
|------|-------------------|-----------------|
| 定位 | 主动唤醒，"内在生活" | 精准调度，"管理任务" |
| 精度 | 粗粒度（30min-2h） | 精确到分钟（cron 表达式） |
| 会话 | 共享主会话上下文 | 可隔离（isolated session） |
| 内容 | HEARTBEAT.md 原样传递给 LLM | 结构化 payload（systemEvent / agentTurn） |
| 输出 | LLM 自主决定是否通知 | 可配置投递渠道 |
| 实现 | 内置于 Agent 核心 | 外部独立进程 |

HEARTBEAT.md 中的原话：**"Management tasks stay in cron. This space is for being, not doing."**

---

## 三、Heartbeat 机制详解

### 3.1 两层分离架构

```
触发源 (interval / cron / exec / requested)
        │
        ▼
┌─────────────────────────────┐
│   HeartbeatWake（请求合并层）  │  ← 250ms 合并窗口 + 双重缓冲
└──────────┬──────────────────┘
           │ handler()
           ▼
┌─────────────────────────────┐
│  HeartbeatManager（调度策略层）│  ← setTimeout 精确调度 + 策略过滤
└──────────┬──────────────────┘
           │ callback()
           ▼
     LLM 回调（getReplyFromConfig）
```

核心理念：HEARTBEAT.md 不做结构化解析，内容原样传递给 LLM，由 LLM 自行决定如何响应。

### 3.2 HeartbeatWake — 请求合并层

五个核心状态变量：

```typescript
class HeartbeatWake {
  private handler: HeartbeatHandler | null = null;  // 执行处理器
  private pendingReason: string | null = null;       // 待处理的唤醒原因
  private scheduled = false;                          // 是否有排队请求
  private running = false;                            // 是否正在执行
  private timer: ReturnType<typeof setTimeout> | null = null;  // 合并定时器
}
```

状态机流转：

```
空闲 → request() → 设置 timer(250ms)
                         │
                    timer 到期
                         │
              ┌── running? ──┐
              │ Yes          │ No
              │              │
         scheduled=true   running=true
         重新 schedule    执行 handler
              │              │
              │         ┌── 结果 ──┐
              │         │          │
              │     成功/跳过    失败/in-flight
              │         │          │
              │    running=false  pendingReason="retry"
              │         │         schedule(1s)
              │         │
              │    有 pending/scheduled?
              │    ├─ Yes → schedule(250ms)
              │    └─ No  → 空闲
```

### 3.3 HeartbeatManager — 调度策略层

配置项：

```typescript
interface HeartbeatConfig {
  intervalMs?: number;          // 检查间隔，默认 30 分钟
  heartbeatPath?: string;       // HEARTBEAT.md 路径
  activeHours?: ActiveHours;    // 活跃时间窗口（支持跨午夜）
  enabled?: boolean;            // 是否启用
  coalesceMs?: number;          // 请求合并窗口，默认 250ms
  duplicateWindowMs?: number;   // 重复检测窗口，默认 24 小时
}
```

四种唤醒原因（WakeReason）：

| 原因 | 触发场景 | 特殊行为 |
|------|----------|----------|
| `interval` | 定时器到期 | 标准流程 |
| `exec` | 异步命令执行完成 | 豁免空内容跳过 |
| `requested` | 外部手动请求 | 标准流程 |
| `retry` | 上次因 in-flight 跳过后的自动重试 | 标准流程 |

单次执行流程（runOnce）：

1. 活跃时间窗口检查 → 不在窗口内则跳过
2. 读取 HEARTBEAT.md 内容（原样，不做解析）
3. 空内容检测（去除 frontmatter + HTML 注释后判断）→ exec 事件豁免
4. 调用回调获取回复
5. 空回复 = HEARTBEAT_OK（LLM 无话可说）
6. 重复消息抑制（24h 窗口 + 文本比较）
7. 更新状态 + 调度下一次

### 3.4 关键设计决策

| 设计点 | 做法 | 原因 |
|--------|------|------|
| 调度方式 | `setTimeout` 而非 `setInterval` | 精确计算下次运行时间，避免漂移 |
| 合并窗口 | 250ms | 防止多个事件同时触发导致重复执行 |
| 双重缓冲 | running + scheduled 标志 | 运行中收到新请求不丢失 |
| 重复抑制 | 24h 窗口 + 文本比较 | 防止频繁发送相同通知 |
| exec 豁免 | exec 事件跳过空内容检测 | 命令完成通知必须传递 |
| 热加载 | updateConfig() 方法 | 运行时修改间隔/活跃时间无需重启 |

---

## 四、Cron 定时任务机制详解

### 4.1 配置格式（openclaw.json）

```json
{
  "agents": {
    "defaults": {
      "heartbeat": {
        "every": "2h",
        "activeHours": {
          "start": "08:00",
          "end": "23:00",
          "timezone": "America/Chicago"
        }
      }
    }
  }
}
```

Cron 任务配置：

```json
{
  "schedule": {
    "kind": "cron",
    "expr": "0 8 * * *",
    "tz": "America/Chicago"
  },
  "sessionTarget": "main",
  "payload": {
    "kind": "systemEvent",
    "text": "..."
  },
  "delivery": {
    "mode": "none"
  }
}
```

### 4.2 两种 payload 类型

| 类型 | 用途 | 会话 |
|------|------|------|
| `systemEvent` | 系统级提示，注入主会话 | 共享主会话上下文 |
| `agentTurn` | 独立任务，Agent 自主执行 | 通常用 isolated session |

### 4.3 ClawPulse 独立调度器架构

```
┌──────────────────────────────────────┐
│           ClawPulse Daemon            │
│  ┌─────────────┐  ┌───────────────┐  │
│  │ Scheduler    │  │ Webhook Server│  │
│  │ (Croner lib) │  │ (HTTP :8080)  │  │
│  └──────┬───────┘  └───────┬───────┘  │
│  ┌──────┴───────────────────┴───────┐ │
│  │     SQLite Database (WAL mode)    │ │
│  │  jobs | job_runs | webhooks       │ │
│  └───────────────────────────────────┘ │
└──────────────────┬───────────────────┘
                   │ openclaw agent --message "..."
                   ▼
            OpenClaw CLI → Agent
```

Job 数据模型：

```typescript
interface Job {
  id: string;
  name: string;
  cron: string;           // Cron 表达式
  message: string;        // 发送给 Agent 的消息
  enabled: boolean;
  lastRunAt?: number;
  nextRunAt?: number;
  runCount: number;
  failCount: number;
}

interface JobRun {
  id: string;
  jobId: string;
  startedAt: number;
  finishedAt?: number;
  status: 'success' | 'failed' | 'running';
  error?: string;
}
```

---

## 五、与 Agent 主循环的集成

### 5.1 Heartbeat 作为 Agent 五大子系统之一

```typescript
class Agent {
  private heartbeat: HeartbeatManager;

  startHeartbeat(callback?) {
    this.heartbeat.onHeartbeat(async (opts) => {
      callback(opts.content, opts.reason);
      return null;
    });
    this.heartbeat.start();
  }
}
```

Heartbeat 独立于主循环：
- 主循环处理用户请求（被动）
- Heartbeat 定时触发 LLM 检查（主动）
- 两者通过 Agent 类协调

### 5.2 心跳时的后台工作

AGENTS.md 定义了心跳时可做的后台工作（无需询问用户）：
- 整理/阅读记忆文件
- 检查项目状态（如 git）
- 更新文档
- 维护 MEMORY.md

### 5.3 并发控制（Command Queue）

两层 Lane 设计：

```
Session Lane (maxConcurrent=1)  ← 同一会话串行
    └─ Global Lane (maxConcurrent=4)  ← 跨会话并行度控制
```

确保 Heartbeat 触发的 LLM 调用不会与用户请求产生竞争条件。

---

## 六、错误处理与重试

### HeartbeatWake 层
- 执行出错 → 自动重试（1s 延迟）
- requests-in-flight 跳过 → 自动重试（1s 延迟）

### HeartbeatManager 层
- 回调异常 → 捕获，返回 failed
- 不影响下次调度（finally 中始终调用 scheduleNext）

### Agent Loop 层
- 速率限制 → 指数退避重试（3 次，300ms-30s）
- 上下文溢出 → 自动压缩后重试一次

---

## 七、HEARTBEAT.md 使用示例

```markdown
# HEARTBEAT.md — What To Do When You Wake Up

Pick one (rotate naturally):

## Check In — 邮件/日历/通知
## Wonder — 好奇心探索，问用户问题
## Reflect — 回顾对话，审视决策
## Create — 写点什么，记录观察
## Grow — 学习新东西，发现交互模式
## Connect — 有真话要说时联系用户

## Rules
- No HEARTBEAT_OK without trying.
- Don't perform thinking. Actually think.
- Write things down.
- Management tasks stay in cron.
```

---

## 八、关键源码文件索引

| 文件 | 行数 | 职责 |
|------|------|------|
| `src/heartbeat.ts` | 586 | HeartbeatWake + HeartbeatManager 完整实现 |
| `src/agent.ts` | 917 | Agent 核心类，Heartbeat 集成点 |
| `src/agent-loop.ts` | 453 | Agent 主循环（与 Heartbeat 解耦） |
| `src/command-queue.ts` | 129 | 两层 Lane 并发控制 |
| `workspace-templates/AGENTS.md` | 209 | Heartbeat 使用指南 + Heartbeat vs Cron 对比 |

---

*文档版本：v1.0*
*创建日期：2026-02-21*
*关联文档：docs/20_调研/21-OpenClaw记忆系统调研报告.md、docs/10_方案/14-心跳与定时任务方案.md*
