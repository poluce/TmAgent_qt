# Service Tests

当前包含：

- `MessageRouterTest`：验证 `@Agent` / `@all` / 默认路由 / unresolved mention 等基础路由规则。
- `MessageRoutingIntegrationTest`：验证 `ApplicationServices` 群聊场景中的 `message_routed` 事件、用户消息 `mentions` 写入，以及 `delegate_task` 工具链路中的父/子追踪字段在事件、消息、任务状态与 SQLite 日志查询中的贯通。
- `MessagePersistenceConcurrencyTest`：验证 SQLite-first 消息主链在同一 session 并发写入下不丢消息、`seq` 顺序可恢复且 payload 完整。
- `ConversationContextServiceTest`：验证完成回合后的 snapshot/checkpoint/resume packet 持久化与事件发射。
- `PrimarySessionResolverTest`：验证主会话选择策略中的最近活跃会话命中、缺失时创建与 isolated 隔离会话创建。
- `AgentPulseRegistryTest`：验证 pulse 注册表中的单实例复用、hard-timeout 事件与进度恢复回调。
- `HeartbeatStateStoreTest`：验证新的 `heartbeat_runtime:*` 状态键加载与保存。
- `HeartbeatDecisionEngineTest`：验证关键变化判定、手动心跳升级与无变化维护策略。
- 说明：旧心跳服务级、回复抑制、端到端与旧提示词构造测试已随心跳架构替换移除。
- `TaskStateServiceTest`：验证任务状态机状态变更与持久化语义。
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
mkdir build-context-service; cd build-context-service
qmake ..\ConversationContextServiceTest.pro
mingw32-make -j4
.\release\ConversationContextServiceTest.exe

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
mkdir build-heartbeat-state; cd build-heartbeat-state
qmake ..\HeartbeatStateStoreTest.pro
mingw32-make -j4
.\release\HeartbeatStateStoreTest.exe

cd ..
mkdir build-heartbeat-decision; cd build-heartbeat-decision
qmake ..\HeartbeatDecisionEngineTest.pro
mingw32-make -j4
.\release\HeartbeatDecisionEngineTest.exe

cd ..
mkdir build-task-state; cd build-task-state
qmake ..\TaskStateServiceTest.pro
mingw32-make -j4
.\release\TaskStateServiceTest.exe

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
