# TmAgent 测试套件

## 目录结构

```
tests/
├── parser/                           # 解析器测试模块
│   ├── TreeSitterParserTest.pro
│   ├── TreeSitterParserTest.cpp
│   ├── README.md
│   └── TEST_REPORT.md
├── memory/                           # 记忆系统测试模块
│   ├── MemoryReflectionTest.pro
│   └── MemoryReflectionTest.cpp
├── service/                          # 服务层测试模块
│   ├── MessageRouterTest.pro
│   ├── MessageRouterTest.cpp
│   ├── HeartbeatServiceTest.pro
│   ├── HeartbeatServiceTest.cpp
│   ├── HeartbeatReplyUtilsTest.pro
│   ├── HeartbeatReplyUtilsTest.cpp
│   ├── HeartbeatEndToEndTest.pro
│   └── HeartbeatEndToEndTest.cpp
├── agent/                            # Agent 测试 (待添加)
├── tools/                            # 工具测试模块
│   ├── MemoryToolTest.pro
│   └── MemoryToolTest.cpp
└── README.md                         # 本文件
```

## 测试模块

| 模块              | 状态     | 描述                      |
| ----------------- | -------- | ------------------------- |
| [parser](parser/) | ✅ 14/14 | TreeSitterParser 封装测试 |
| [memory](memory/) | ✅ 新增 | 反思任务/质量评分（M4）无头集成测试 |
| [service](service/) | ✅ 补齐 | MessageRouter 路由规则 + HeartbeatService / HeartbeatReplyUtils 回归测试 + Heartbeat 端到端验收 |
| agent             | 🔜       | LLMAgent、ToolDispatcher  |
| tools             | ✅ 补齐 | FileTool、ShellTool、WebTool、MemoryTool |

## 运行测试

```powershell
# Parser 模块
cd tests/parser
mkdir build; cd build; qmake ..; mingw32-make -j4
.\release\TreeSitterParserTest.exe

# Service 模块
cd tests/service
mkdir build; cd build; qmake ..; mingw32-make -j4
.\release\MessageRouterTest.exe

cd ..
mkdir build-heartbeat; cd build-heartbeat; qmake ..\HeartbeatServiceTest.pro; mingw32-make -j4
.\release\HeartbeatServiceTest.exe

cd ..
mkdir build-heartbeat-utils; cd build-heartbeat-utils; qmake ..\HeartbeatReplyUtilsTest.pro; mingw32-make -j4
.\release\HeartbeatReplyUtilsTest.exe

cd ..
mkdir build-heartbeat-e2e; cd build-heartbeat-e2e; qmake ..\HeartbeatEndToEndTest.pro; mingw32-make -j4
Copy-Item -Recurse -Force ..\..\..\resources .\release\resources
.\release\HeartbeatEndToEndTest.exe

# Memory 模块
cd tests/memory
mkdir build; cd build; qmake ..; mingw32-make -j4
.\release\MemoryReflectionTest.exe

# Tools 模块（MemoryTool）
cd tests/tools
mkdir build-memory; cd build-memory; qmake ..\MemoryToolTest.pro; mingw32-make -j4
.\release\MemoryToolTest.exe
```

## 添加新模块

1. 创建 `tests/<module>/` 目录
2. 添加 `*.pro`、`*Test.cpp`、`README.md`
3. 更新本文件的模块表格
