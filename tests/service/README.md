# Service Tests

当前包含：

- `MessageRouterTest`：验证 `@Agent` / `@all` / 默认路由 / unresolved mention 等基础路由规则。
- `MessageRoutingIntegrationTest`：验证 `ChatService` 群聊场景中的 `message_routed` 事件、用户消息 `mentions` 写入，以及 `delegate_task` 工具链路中的父/子追踪字段在事件、消息、任务状态与 SQLite 日志查询中的贯通。
- `MessagePersistenceConcurrencyTest`：验证 SQLite-first 消息主链在同一 session 并发写入下不丢消息、`seq` 顺序可恢复且 payload 完整。
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
