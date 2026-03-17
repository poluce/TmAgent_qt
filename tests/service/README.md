# Service Tests

当前包含：

- `MessageRouterTest`：验证 `@Agent` / `@all` / 默认路由 / unresolved mention 等基础路由规则。
- `MessageRoutingIntegrationTest`：验证 `ChatService` 群聊场景中的 `message_routed` 事件、用户消息 `mentions` 写入，以及 `delegate_task` 工具链路中的父/子追踪字段在事件、消息、任务状态与 SQLite 日志查询中的贯通。
- `MessagePersistenceConcurrencyTest`：验证 SQLite-first 消息主链在同一 session 并发写入下不丢消息、`seq` 顺序可恢复且 payload 完整。
- `ConversationEnqueueCoordinatorTest`：验证主链入口的权限校验、入队、merge、soft backpressure 与 hard overflow。
- `ConversationDispatchCoordinatorTest`：验证主链中段的 active-session 约束、history/config/ioContext 注入与 dispatch 事件发射。
- `ConversationFinishCoordinatorTest`：验证普通完成、blocked 同 turn、heartbeat 静默与手动 heartbeat 完成路径。
- `ConversationToolEventCoordinatorTest`：验证 delegate started/completed/failed 路径中的任务状态、心跳抑制与事件统计。
- `DelegateSettlementCoordinatorTest`：验证子代理完成通知链中的系统消息注入、task state 回写、heartbeat 触发与 settled 事件。
- `PrimarySessionResolverTest`：验证主会话选择策略中的最近活跃会话命中、缺失时创建与 isolated 隔离会话创建。
- `AgentPulseRegistryTest`：验证 pulse 注册表中的单实例复用、hard-timeout 事件与进度恢复回调。
- `HeartbeatPromptBuilderTest`：验证 heartbeat prompt 的默认模板回退、reason 注入与 legacy 指令文件修正。
- `HeartbeatStateStoreTest`：验证 heartbeat 运行时状态的 app-state 加载、文件回退与持久化条件。
- `HeartbeatDispatchCoordinatorTest`：验证 heartbeat dispatch 的 pipeline busy、正常 enqueue 与 enqueue 失败路径。
- `HeartbeatSnapshotCoordinatorTest`：验证 heartbeat 快照/变化判断/静默与通知决策。
- `SchedulerTriggerCoordinatorTest`：验证 scheduler 触发链的 job 校验、session 解析、enqueue 与 fired/completed/failed 事件发射。
- `SchedulerServiceTest`：验证定时任务 CRUD、启停、持久化恢复与到点触发基础行为。

运行方式（Windows + Qt）：

```powershell
cd tests/service
mkdir build-router; cd build-router
qmake ..\MessageRouterTest.pro
mingw32-make -j4
.\release\MessageRouterTest.exe

cd ..
mkdir build-routing; cd build-routing
qmake ..\MessageRoutingIntegrationTest.pro
mingw32-make -j4
Copy-Item -Recurse -Force ..\..\..\resources .\release\resources
.\release\MessageRoutingIntegrationTest.exe

cd ..
mkdir build-finish; cd build-finish
qmake ..\ConversationFinishCoordinatorTest.pro
mingw32-make -j4
.\release\ConversationFinishCoordinatorTest.exe

cd ..
mkdir build-dispatch; cd build-dispatch
qmake ..\ConversationDispatchCoordinatorTest.pro
mingw32-make -j4
.\release\ConversationDispatchCoordinatorTest.exe

cd ..
mkdir build-delegate-settlement; cd build-delegate-settlement
qmake ..\DelegateSettlementCoordinatorTest.pro
mingw32-make -j4
.\release\DelegateSettlementCoordinatorTest.exe

cd ..
mkdir build-primary-session; cd build-primary-session
qmake ..\PrimarySessionResolverTest.pro
mingw32-make -j4
.\release\PrimarySessionResolverTest.exe

cd ..
mkdir build-agent-pulse; cd build-agent-pulse
qmake ..\AgentPulseRegistryTest.pro
mingw32-make -j4
.\release\AgentPulseRegistryTest.exe

cd ..
mkdir build-heartbeat-prompt; cd build-heartbeat-prompt
qmake ..\HeartbeatPromptBuilderTest.pro
mingw32-make -j4
.\release\HeartbeatPromptBuilderTest.exe

cd ..
mkdir build-heartbeat-state; cd build-heartbeat-state
qmake ..\HeartbeatStateStoreTest.pro
mingw32-make -j4
.\release\HeartbeatStateStoreTest.exe

cd ..
mkdir build-heartbeat-dispatch; cd build-heartbeat-dispatch
qmake ..\HeartbeatDispatchCoordinatorTest.pro
mingw32-make -j4
.\release\HeartbeatDispatchCoordinatorTest.exe

cd ..
mkdir build-tool-event; cd build-tool-event
qmake ..\ConversationToolEventCoordinatorTest.pro
mingw32-make -j4
.\release\ConversationToolEventCoordinatorTest.exe

cd ..
mkdir build-enqueue; cd build-enqueue
qmake ..\ConversationEnqueueCoordinatorTest.pro
mingw32-make -j4
.\release\ConversationEnqueueCoordinatorTest.exe

cd ..
mkdir build-heartbeat-snapshot; cd build-heartbeat-snapshot
qmake ..\HeartbeatSnapshotCoordinatorTest.pro
mingw32-make -j4
.\release\HeartbeatSnapshotCoordinatorTest.exe

cd ..
mkdir build-scheduler-trigger; cd build-scheduler-trigger
qmake ..\SchedulerTriggerCoordinatorTest.pro
mingw32-make -j4
.\release\SchedulerTriggerCoordinatorTest.exe

cd ..
mkdir build-message-persist; cd build-message-persist
qmake ..\MessagePersistenceConcurrencyTest.pro
mingw32-make -j4
.\release\MessagePersistenceConcurrencyTest.exe

cd ..
mkdir build-scheduler; cd build-scheduler
qmake ..\SchedulerServiceTest.pro
mingw32-make -j4
.\release\SchedulerServiceTest.exe
```
