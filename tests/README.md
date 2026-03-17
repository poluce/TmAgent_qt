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
│   ├── MessageRoutingIntegrationTest.pro
│   ├── MessageRoutingIntegrationTest.cpp
│   ├── MessagePersistenceConcurrencyTest.pro
│   ├── MessagePersistenceConcurrencyTest.cpp
│   ├── HeartbeatServiceTest.pro
│   ├── HeartbeatServiceTest.cpp
│   ├── HeartbeatReplyUtilsTest.pro
│   ├── HeartbeatReplyUtilsTest.cpp
│   ├── HeartbeatEndToEndTest.pro
│   ├── HeartbeatEndToEndTest.cpp
│   ├── TaskStateServiceTest.pro
│   ├── TaskStateServiceTest.cpp
│   ├── SchedulerServiceTest.pro
│   └── SchedulerServiceTest.cpp
├── agent/                            # Agent 测试 (待添加)
├── tools/                            # 工具测试模块
│   ├── MemoryToolTest.pro
│   └── MemoryToolTest.cpp
├── ui/                               # UI 格式化/展示测试模块
│   ├── HistoryFormattersTest.pro
│   └── HistoryFormattersTest.cpp
├── eval/                             # CLI/Agent 离线评测资产
│   ├── eval_runner.py
│   ├── eval_scorer.py
│   ├── tasks/
│   └── README.md
└── README.md                         # 本文件
```

## 测试模块

| 模块              | 状态     | 描述                      |
| ----------------- | -------- | ------------------------- |
| [parser](parser/) | ✅ 14/14 | TreeSitterParser 封装测试 |
| [memory](memory/) | ✅ 新增 | 反思任务/质量评分（M4）无头集成测试 |
| [service](service/) | ✅ 补齐 | MessageRouter 路由规则 + ChatService 群聊路由/委派链路集成测试（含 SQLite 日志反查） + SQLite 消息主链并发持久化测试 + ConversationEnqueueCoordinator / ConversationDispatchCoordinator / ConversationFinishCoordinator / ConversationToolEventCoordinator / DelegateSettlementCoordinator / PrimarySessionResolver / AgentPulseRegistry / HeartbeatPromptBuilder / HeartbeatStateStore / HeartbeatDispatchCoordinator / HeartbeatService / HeartbeatReplyUtils / HeartbeatSnapshotCoordinator / SchedulerTriggerCoordinator 回归测试 + Heartbeat 端到端验收 + TaskStateService 状态机测试 + SchedulerService 调度测试 |
| agent             | 🔜       | LLMAgent、ToolDispatcher  |
| tools             | ✅ 补齐 | FileTool、ShellTool、WebTool、MemoryTool（BM25 排序 + 本地哈希向量回退） |
| ui                | ✅ 新增 | 执行记录/原文面板文案、固定摘要格式、分层定义与四层原文结构测试 + 会话事件 UI 适配测试 |
| eval              | ✅ 合并 | CLI/Agent 离线任务评测、自动评分与样例工作区 |

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
mkdir build-routing; cd build-routing; qmake ..\MessageRoutingIntegrationTest.pro; mingw32-make -j4
Copy-Item -Recurse -Force ..\..\..\resources .\release\resources
.\release\MessageRoutingIntegrationTest.exe

cd ..
mkdir build-message-persist; cd build-message-persist; qmake ..\MessagePersistenceConcurrencyTest.pro; mingw32-make -j4
.\release\MessagePersistenceConcurrencyTest.exe

cd ..
mkdir build-heartbeat; cd build-heartbeat; qmake ..\HeartbeatServiceTest.pro; mingw32-make -j4
.\release\HeartbeatServiceTest.exe

cd ..
mkdir build-heartbeat-utils; cd build-heartbeat-utils; qmake ..\HeartbeatReplyUtilsTest.pro; mingw32-make -j4
.\release\HeartbeatReplyUtilsTest.exe

cd ..
mkdir build-finish; cd build-finish; qmake ..\ConversationFinishCoordinatorTest.pro; mingw32-make -j4
.\release\ConversationFinishCoordinatorTest.exe

cd ..
mkdir build-dispatch; cd build-dispatch; qmake ..\ConversationDispatchCoordinatorTest.pro; mingw32-make -j4
.\release\ConversationDispatchCoordinatorTest.exe

cd ..
mkdir build-delegate-settlement; cd build-delegate-settlement; qmake ..\DelegateSettlementCoordinatorTest.pro; mingw32-make -j4
.\release\DelegateSettlementCoordinatorTest.exe

cd ..
mkdir build-primary-session; cd build-primary-session; qmake ..\PrimarySessionResolverTest.pro; mingw32-make -j4
.\release\PrimarySessionResolverTest.exe

cd ..
mkdir build-agent-pulse; cd build-agent-pulse; qmake ..\AgentPulseRegistryTest.pro; mingw32-make -j4
.\release\AgentPulseRegistryTest.exe

cd ..
mkdir build-heartbeat-prompt; cd build-heartbeat-prompt; qmake ..\HeartbeatPromptBuilderTest.pro; mingw32-make -j4
.\release\HeartbeatPromptBuilderTest.exe

cd ..
mkdir build-heartbeat-state; cd build-heartbeat-state; qmake ..\HeartbeatStateStoreTest.pro; mingw32-make -j4
.\release\HeartbeatStateStoreTest.exe

cd ..
mkdir build-heartbeat-dispatch; cd build-heartbeat-dispatch; qmake ..\HeartbeatDispatchCoordinatorTest.pro; mingw32-make -j4
.\release\HeartbeatDispatchCoordinatorTest.exe

cd ..
mkdir build-tool-event; cd build-tool-event; qmake ..\ConversationToolEventCoordinatorTest.pro; mingw32-make -j4
.\release\ConversationToolEventCoordinatorTest.exe

cd ..
mkdir build-enqueue; cd build-enqueue; qmake ..\ConversationEnqueueCoordinatorTest.pro; mingw32-make -j4
.\release\ConversationEnqueueCoordinatorTest.exe

cd ..
mkdir build-heartbeat-snapshot; cd build-heartbeat-snapshot; qmake ..\HeartbeatSnapshotCoordinatorTest.pro; mingw32-make -j4
.\release\HeartbeatSnapshotCoordinatorTest.exe

cd ..
mkdir build-scheduler-trigger; cd build-scheduler-trigger; qmake ..\SchedulerTriggerCoordinatorTest.pro; mingw32-make -j4
.\release\SchedulerTriggerCoordinatorTest.exe

cd ..
mkdir build-heartbeat-e2e; cd build-heartbeat-e2e; qmake ..\HeartbeatEndToEndTest.pro; mingw32-make -j4
Copy-Item -Recurse -Force ..\..\..\resources .\release\resources
.\release\HeartbeatEndToEndTest.exe

cd ..
mkdir build-task-state; cd build-task-state; qmake ..\TaskStateServiceTest.pro; mingw32-make -j4
.\release\TaskStateServiceTest.exe

cd ..
mkdir build-scheduler; cd build-scheduler; qmake ..\SchedulerServiceTest.pro; mingw32-make -j4
.\release\SchedulerServiceTest.exe

# Memory 模块
cd tests/memory
mkdir build; cd build; qmake ..; mingw32-make -j4
.\release\MemoryReflectionTest.exe

# Tools 模块（MemoryTool）
cd tests/tools
mkdir build-memory; cd build-memory; qmake ..\MemoryToolTest.pro; mingw32-make -j4
.\release\MemoryToolTest.exe

# UI 模块（HistoryFormatters）
cd ..\ui
mkdir build; cd build; qmake ..\HistoryFormattersTest.pro; mingw32-make -j4
.\release\HistoryFormattersTest.exe

cd ..
mkdir build-event-support; cd build-event-support; qmake ..\ConversationEventUiSupportTest.pro; mingw32-make -j4
.\release\ConversationEventUiSupportTest.exe

# Eval 目录（CLI/Agent 离线评测）
cd ..\eval
python .\eval_runner.py --cli ..\..\build\TmAgentCli.exe --suite .\tasks\task_suite.yaml --output .\report.json
```

## 添加新模块

1. 创建 `tests/<module>/` 目录
2. 添加 `*.pro`、`*Test.cpp`、`README.md`
3. 更新本文件的模块表格
