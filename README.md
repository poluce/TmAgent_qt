# TmAgent

**Team of Agents** - 多智能体协作框架的 Qt 客户端

## 简介

TmAgent 是一个基于 Qt 的 AI Agent 客户端，支持：

- 🤖 **LLM 对话**：与大语言模型进行多轮对话
- 🔧 **工具调用**：自动执行文件操作和 Shell 命令
- 🛡️ **安全策略**：读写权限分离，写操作限定在工作目录内
- 📝 **调试模式**：可切换详细/简洁的工具执行反馈

## 环境要求

- **Qt**: 5.14.2+ (推荐使用 Qt 5.14.2)
- **编译器**: MinGW 7.3+ / MSVC 2017+
- **C++ 标准**: C++17

## 构建

```bash
# 使用 qmake 构建
mkdir build && cd build
qmake ../TmAgent.pro
make -j4
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
