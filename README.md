# TmAgent

**Team of Agents** - 多智能体协作框架的 Qt 客户端

## 简介

TmAgent 是一个基于 Qt 的 AI Agent 客户端，支持：

- 🤖 **LLM 对话**：与大语言模型进行多轮对话
- 🧠 **深度思考可视化**：支持 DeepSeek 专属 Provider 与思考/工具调用状态指示
- 🔧 **工具调用**：自动执行文件操作和 Shell 命令
- 💻 **交互式 CLI**：支持 `TmAgentCli --interactive` 进入 REPL 多轮对话
- 💾 **SQLite 迁移进行中**：当前为 JSONL + SQLite 双写过渡态，已支持跨进程同步
- 🛡️ **安全策略**：读写权限分离，写操作限定在工作目录内
- 📝 **调试模式**：可切换详细/简洁的工具执行反馈

## 子模块

本仓库通过子模块引用 **QChatWidget**。克隆后需初始化并更新子模块：

```powershell
git submodule update --init --recursive --remote
```

或使用脚本：`.\scripts\update-submodule.ps1`。适配说明见 [docs/10_方案/12-子模块更新与适配.md](docs/10_方案/12-子模块更新与适配.md)。

## 文档

- 文档导航与分层说明：`docs/README.md`
- 架构执行主方案：`docs/10_方案/10-架构升级设计方案.md`

## 环境要求

- **Qt**: 5.14.2+ (推荐使用 Qt 5.14.2)
- **编译器**: MinGW 7.3+ / MSVC 2017+
- **C++ 标准**: C++17

## 构建

```bash
# 顶层工程一次构建，同时生成 TmAgent、TmAgentCli 与 tmagent-log 三个可执行文件
mkdir build && cd build
qmake ../TmAgent.pro
make -j4
```

### TmAgentCli 使用示例

```bash
# 交互式多轮对话（REPL）
./TmAgentCli --interactive

# 单次任务执行
./TmAgentCli "请总结今天的改动并给出待办"
```

### tmagent-log 使用示例

常用示例：

```bash
# 按会话 ID 查最近 100 条（表格格式）
./tmagent-log --session-id 77c76ca0-ab84-4c11-a495-56f24ae62743 --limit 100 --format table

# 按 request_id 精确定位（事件源）
./tmagent-log --source events --request-id 1deee4eb-5211-4f05-b549-974f5fc2cb79 --format report

# 按关键词筛错误
./tmagent-log --keyword "Bad Gateway" --limit 50 --format report
```

### 心跳与定时任务（文件配置）

当前已接入后端骨架（`HealthMonitor` / `HeartbeatService` / `AgentPulse` / `SchedulerService`），
可先通过配置文件直接验证：

```bash
# Agent 级心跳配置
~/.tmagent/identities/agents/<agent_id>/heartbeat_config.json

# Agent 心跳指令
~/.tmagent/identities/agents/<agent_id>/HEARTBEAT.md

# 全局定时任务
~/.tmagent/config/scheduled_jobs.json
```

`heartbeat_config.json` 示例：

```json
{
  "enabled": true,
  "intervalMs": 1800000,
  "coalesceMs": 250,
  "duplicateWindowMs": 86400000,
  "heartbeatPath": "/absolute/path/to/HEARTBEAT.md",
  "activeHours": {
    "start": "08:00",
    "end": "23:00",
    "timezone": "Asia/Shanghai"
  }
}
```

`scheduled_jobs.json` 示例（Cron 5 段）：

```json
{
  "schemaVersion": 1,
  "jobs": [
    {
      "jobId": "daily-brief",
      "name": "每日简报",
      "agentId": "<agent_id>",
      "prompt": "请整理今天的关键事项并输出简报",
      "cronExpr": "0 9 * * *",
      "timezone": "Asia/Shanghai",
      "sessionTarget": "main",
      "enabled": true
    }
  ]
}
```

### 持久化与多进程同步

当前主线已进入 SQLite 迁移阶段，现状为 `JSONL + SQLite` 双写过渡态：

```bash
# SQLite 数据库（会话 / 事件 / 同步查询）
~/.tmagent/tmagent.db
```

其中 Markdown / JSONL 仍保留为当前主要可读落盘形式；SQLite 已用于跨进程同步、快速检索与日志查询加速，但尚未完成对现有存储链路的全面替代。

## 使用

```bash
# 从项目目录启动（工作目录 = 启动位置）
cd /path/to/your/project
TmAgent.exe
```

## 安全机制

| 操作类型 | 权限                |
| -------- | ------------------- |
| 读取操作 | 🔓 可访问任意路径   |
| 写入操作 | 🔒 限定在工作目录内 |

## 许可证

MIT License
