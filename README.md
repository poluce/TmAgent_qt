# TmAgent

**Team of Agents** - 多智能体协作框架的 Qt 客户端

## 简介

TmAgent 是一个基于 Qt 的 AI Agent 客户端，支持：

- 🤖 **LLM 对话**：与大语言模型进行多轮对话
- 🔧 **工具调用**：自动执行文件操作和 Shell 命令
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
# 顶层工程一次构建，同时生成 TmAgent 与 tmagent-log 两个可执行文件
mkdir build && cd build
qmake ../TmAgent.pro
make -j4
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
