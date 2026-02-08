# TmAgent Qt 技术约束与前提条件

> 本文档是《项目愿景与产品规划》的技术补充，显式列出愿景中所有隐含的 Qt 框架层面的技术约束、前提条件和实现假设。
> 所有实现工作在动手前应先对照本文档检查约束条件。

---

## 一、线程模型

### 约束 1：GUI 主线程不可阻塞

Qt Widgets 的所有 UI 操作（包括 QWidget 创建、布局更新、消息渲染）必须在 GUI 主线程执行。任何阻塞主线程的操作都会导致界面冻结。

这意味着：
- 岗位 Agent 的决策循环不能在主线程同步执行
- 子 Agent 的工具调用（文件读写、网络请求、Shell 命令）不能阻塞主线程
- 流式输出的 UI 更新必须通过主线程的事件循环驱动

### 约束 2：多 Agent 并行需要工作线程

愿景中"岗位 Agent 同时创建多个子 Agent 并行执行"（第三章 3.4 节），在 Qt 中的实现方案：

| 阶段 | 方案 | 说明 |
|------|------|------|
| 阶段 1-2 | 单线程 + 异步信号槽 | 所有 Agent 在主线程，通过事件循环交错执行（伪并行） |
| 阶段 3+ | QThread 工作线程池 | 每个子 Agent 在独立工作线程运行，真正并行 |

工作线程方案的关键约束：
- QObject 的线程亲和性：QObject 只能在创建它的线程中使用
- 跨线程信号槽必须使用 `Qt::QueuedConnection`（自动序列化到目标线程的事件循环）
- 子 Agent 在工作线程创建时，其 parent 必须为 `nullptr`（不能跨线程设置 parent）
- QNetworkAccessManager 必须在子 Agent 所在的工作线程中创建（不能跨线程共享）

### 约束 3：QEventLoop 嵌套的风险

当前 `AgentTool` 使用 `QEventLoop::exec()` 将异步子 Agent 转为同步调用。递归深度为 3 时会产生 3 层嵌套事件循环，存在以下风险：
- 外层 Agent 的信号可能在内层事件循环中被意外处理（重入）
- 用户中断只能停止最内层 Agent，外层仍在等待
- 超时定时器可能在错误的事件循环层级触发

缓解措施：
- 限制递归深度 <= 2
- 为每个 QEventLoop 设置超时保护
- 长期方案：改用异步状态机代替嵌套事件循环

---

## 二、QObject 生命周期

### 约束 4：子 Agent 的创建与销毁

愿景中"子 Agent 完成后销毁"（第三章 3.2 节），在 Qt 中必须遵循：
- 使用 `deleteLater()` 而非直接 `delete`（确保所有排队信号处理完毕后再销毁）
- 不能在 QObject 的信号槽回调中直接 delete 发送者（会导致崩溃）
- 如果子 Agent 持有 QNetworkReply，必须先 `abort()` 再 `deleteLater()`
- 多个子 Agent 并行时，使用 `QList<LLMAgent*>` 管理，所有子 Agent 完成后统一清理

### 约束 5：Provider 热切换

愿景中"智能路由：根据任务类型自动选择最合适的模型"（阶段 5），涉及运行时切换 LLMProvider：
- 切换前必须先 `abort()` 当前请求，再 `deleteLater()` 旧 Provider
- 新 Provider 的 parent 应设为 LLMAgent（利用 Qt parent-child 自动销毁机制）
- 每个 LLMAgent 持有独立的 LLMProvider 实例，不共享

### 约束 6：AgentEventBus 信号连接管理

AgentEventBus 是全局单例，所有 Agent 都会连接其信号。Agent 销毁时：
- 如果接收者是 QObject 且使用成员函数作为槽，Qt 会自动断开连接
- 如果使用 lambda 作为槽，必须指定 context 对象（如 `connect(bus, &Bus::signal, this, [this](){...})`），否则 Agent 销毁后 lambda 仍会被调用导致崩溃
- 不能使用裸函数指针或 `std::function` 作为槽（无法自动断开）

---

## 三、网络模型

### 约束 7：QNetworkAccessManager 的线程亲和性

每个 LLMProvider 持有独立的 QNetworkAccessManager。并发场景下：
- 10 个 Agent 并行 = 10 个 QNetworkAccessManager = 最多 60 个并发 HTTP 连接
- 单个 API 服务器可能限制每 IP 连接数（如 Anthropic 限制 50）
- 不能跨线程使用 QNetworkAccessManager（会崩溃）
- 推荐：主线程 Agent 共享全局实例，工作线程 Agent 每线程一个实例

### 约束 8：SSE 流式解析的缓冲区管理

流式输出依赖 SSE（Server-Sent Events）解析，`QNetworkReply::readyRead` 信号存在分包和粘包问题：
- 一个 SSE 事件可能分多次 `readyRead` 到达（需要累积缓冲区）
- 一次 `readyRead` 可能包含多个完整事件（需要循环解析）
- 中文等多字节 UTF-8 字符可能在字节边界被截断（需要有状态解码器，不能直接 `QString::fromUtf8(chunk)`）
- 缓冲区需要设置上限（如 10MB），防止异常数据导致内存耗尽

### 约束 9：WebSocket 长连接（外部通讯渠道）

外部通讯渠道插件可能需要 WebSocket：
- 需要在 TmAgent.pro 中添加 `QT += websockets`
- QWebSocket 必须在有事件循环的线程中运行
- 需要心跳保活机制（每 30 秒 ping）
- 断线重连必须创建新的 QWebSocket 实例（不能复用已断开的实例）
- 重连策略：指数退避（1s → 2s → 4s → 8s → 最大 60s）

### 约束 10：abort() 的竞态条件

多个 Agent 并行时，用户中断请求可能与网络请求完成同时发生：
- `abort()` 前必须先 `disconnect` 信号，防止 `finished` 槽被调用
- 对已完成的 QNetworkReply 调用 `abort()` 是安全的（无操作）
- 超时处理使用 QTimer（Qt 5.14.2 没有 `QNetworkRequest::setTransferTimeout`，该 API 在 5.15+ 才有）

---

## 四、插件系统

### 约束 11：QPluginLoader 的 ABI 兼容性

Qt 插件系统对编译环境有严格要求，插件与主程序必须完全一致：
- Qt 版本（精确到 patch 版本，如都是 5.14.2）
- 编译器及版本（如都是 MinGW 7.3 或 MSVC 2019）
- C++ 标准（如都是 C++17）
- 构建类型（Debug 插件不能加载到 Release 主程序）

插件接口定义必须遵循：
```cpp
// 接口声明（主程序和插件共享的头文件）
class IChannelPlugin {
public:
    virtual ~IChannelPlugin() = default;
    virtual void initialize(QJsonObject context) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};
Q_DECLARE_INTERFACE(IChannelPlugin, "com.tmagent.IChannelPlugin/1.0")

// 插件实现
class WeChatChannel : public QObject, public IChannelPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.tmagent.IChannelPlugin/1.0" FILE "wechat.json")
    Q_INTERFACES(IChannelPlugin)
};
```

### 约束 12：插件接口不能使用 STL 容器

插件接口的参数和返回值只能使用 Qt 容器（QString、QList、QMap、QJsonObject 等），不能使用 `std::string`、`std::vector` 等 STL 容器。原因：STL 容器的 ABI 在不同编译器版本间不兼容。

### 约束 13：插件中的 QWidget 生命周期

插件创建的 UI 组件（如配置面板）：
- 不能将主程序窗口设为 parent（插件卸载时会导致双重释放）
- 推荐 parent=nullptr，由主程序手动管理布局
- 插件卸载前必须由插件自己销毁所有 UI 组件
- 插件加载的资源文件（.qrc）必须在卸载前调用 `QResource::unregisterResource()` 释放

### 约束 14：插件热加载的限制

Qt 的 `QPluginLoader::unload()` 只有在插件创建的所有 QObject 都销毁后才能成功。实际上在 Windows 上 DLL 卸载后重新加载可能失败（DLL 被缓存）。推荐方案：
- 支持"停用"（stop + 隐藏 UI），但不真正卸载 DLL
- 真正的插件更新需要重启应用

---

## 五、数据持久化

### 约束 15：存储方案选择

愿景中五大资源库的存储方案：

| 资源库 | 推荐方案 | 原因 |
|--------|---------|------|
| 知识库 | SQLite + FTS5 | 需要全文检索，JSON 文件无法高效搜索 |
| 文件库 | JSON 索引 + 文件系统 | 元数据少，实际文件在项目目录中 |
| Skill 库 | YAML 文件 | 与现有 tools.yaml 格式一致，人类可读可编辑 |
| MCP 服务器配置 | JSON 配置文件 | 配置项少，结构简单 |
| 经验库 | SQLite | 需要按标签/时间/相关性检索，数据量可能很大 |

Qt 5.14.2 内置 SQLite 驱动（`QT += sql`），支持 FTS5 全文搜索扩展。

### 约束 16：向量检索（语义搜索）需要第三方库

知识库的语义检索需要向量相似度计算，Qt 没有内置支持。可选方案：
- **hnswlib**（推荐）：纯 C++ 头文件库，无外部依赖，易集成
- **faiss**：Facebook 开源，功能强大但依赖重
- **annoy**：Spotify 开源，轻量但精度略低
- 向量生成需要调用 Embedding API（如 OpenAI text-embedding-3-small）

初期可以只实现关键词检索（SQLite FTS5），语义检索作为后续优化。

### 约束 17：QFileSystemWatcher 的限制

文件库的"变更通知"功能依赖 QFileSystemWatcher：
- Windows 上最多监控约 4096 个文件/目录
- 不能递归监控子目录（需要手动遍历添加）
- 文件重命名会触发 `fileChanged` 而非 `directoryChanged`
- 推荐：只监控项目根目录和关键子目录，不监控每个文件

---

## 六、UI 架构

### 约束 18：Tab 切换的实现方案

愿景中"顶部 Tab 切换 Identity 视角"（第四章 4.2 节），推荐实现：

```
QTabBar（顶部标签栏，只负责标签显示和切换信号）
    +
QStackedWidget（堆叠容器，每个页面是一个 IdentityView）
    ├── IdentityView[用户]
    │   ├── ChatListWidget（会话列表，数据源：用户的 SessionList）
    │   └── ChatWidget（聊天区，数据源：当前选中的 Session）
    ├── IdentityView[Agent-A]
    │   ├── ChatListWidget（会话列表，数据源：Agent-A 的 SessionList）
    │   └── ChatWidget（聊天区）
    └── ...
```

不推荐 QTabWidget（它把 TabBar 和内容区绑定在一起，灵活性不够）。

### 约束 19：QChatWidget 子模块的多实例支持

愿景中每个 Tab 都有自己的 ChatListWidget 和 ChatWidget。当前 QChatWidget 子模块的情况：
- ChatWidget 支持多实例（无全局状态）
- ChatWidgetModel 是独立的数据模型，每个 ChatWidget 持有自己的实例
- ChatListWidget 支持多实例
- QSS 样式文件是全局加载的（通过 `applyStyleSheetFile`），多实例共享同一套样式——这是正确的行为

需要扩展的部分：
- ChatWidgetModel 需要支持绑定到 Session 数据源（当前是内部管理消息列表）
- ChatListWidget 需要支持按 Identity 过滤会话列表
- 群聊消息中的 @mention 需要在 ChatWidgetDelegate 中实现高亮渲染

### 约束 20：多 Tab 同时流式输出的性能

多个 Agent 同时进行流式输出时：
- 每个 ChatWidget 的 ChatWidgetDelegate 都在主线程重绘
- Markdown 渲染（md4c）是 CPU 密集型操作
- 10 个 Tab 同时流式输出可能导致 UI 卡顿

缓解措施：
- 只有当前可见的 Tab 实时渲染，后台 Tab 只更新数据模型
- Markdown 渲染结果缓存（只在内容变化时重新渲染）
- 使用 `QListView::setUniformItemSizes(false)` + 延迟布局计算

---

## 七、进程间通信

### 约束 21：单实例应用保证

TmAgent 作为桌面应用，必须防止多开（多个实例会导致数据文件冲突）：
- 推荐方案：`QLocalServer` + `QLocalSocket`
- 启动时尝试连接已有实例，如果连接成功说明已有实例在运行，将参数转发给已有实例
- 如果连接失败，创建 QLocalServer 监听

### 约束 22：外部通讯渠道的进程隔离

某些通讯渠道插件可能需要独立进程（如微信 Hook 需要注入目标进程）：
- 推荐方案：`QProcess` 启动子进程 + `QLocalSocket` 通信
- 子进程崩溃不影响主程序
- 需要定义主程序与子进程之间的 IPC 协议（推荐 JSON over QLocalSocket）

---

## 八、环境与版本约束

### 约束 23：Qt 5.14.2 的版本限制

项目锁定 Qt 5.14.2，以下较新的 Qt API 不可用：
- `QNetworkRequest::setTransferTimeout()`（5.15+）
- `QPromise` / `QFuture` 改进（6.0+）
- `QStringView` 的完整 API（5.15+）
- `Q_NAMESPACE_EXPORT`（5.14+，刚好可用）
- `QCalendar`（5.14+，刚好可用）

如果未来需要升级 Qt 版本，需要评估 QChatWidget 子模块和 qtkeychain 的兼容性。

### 约束 24：C++17 特性的编译器支持

项目使用 C++17（TmAgent.pro 中 `CONFIG += c++17`），MinGW 7.3 对 C++17 的支持：
- `std::optional`、`std::variant`、`std::any`：完全支持
- `std::filesystem`：需要链接 `-lstdc++fs`
- 结构化绑定（`auto [a, b] = ...`）：支持
- `if constexpr`：支持
- `std::string_view`：支持但建议优先使用 `QStringView`

### 约束 25：OpenSSL 依赖

项目目录中有 `openssl/` 目录，说明 HTTPS 请求依赖 OpenSSL 动态库：
- 所有 LLM API 调用都是 HTTPS，OpenSSL 是硬依赖
- 插件如果需要 HTTPS（如 Telegram Bot API），也依赖同一套 OpenSSL
- 部署时必须携带 `libssl-1_1-x64.dll` 和 `libcrypto-1_1-x64.dll`（或对应版本）
- Qt 5.14.2 默认支持 OpenSSL 1.1.x，不支持 3.x

---

## 九、运行与质量补充约束

### 约束 26：并发预算与限流

多 Agent 并行必须受预算控制，避免线程、连接和 UI 同时被打满：
- 全局子 Agent 并发上限：`max(4, CPU核心数)`，默认 8
- 单岗位 Agent 并发上限：2（避免某一岗位独占资源）
- 单 LLM Provider 并发上限：按供应商限额配置，默认 4
- 超出预算进入队列（FIFO + 优先级），队列长度超限时拒绝并返回可重试错误
- 限流命中必须产生日志和指标（命中次数、排队时长、拒绝率）

### 约束 27：统一取消语义

“用户点击停止”必须在全链路表现一致：
- 取消信号从 UI 进入后，先更新任务状态为 `Canceled`
- 然后向子 Agent 广播取消，再取消网络请求（`abort()`）和定时器
- 所有取消路径都必须幂等（重复取消不导致崩溃或重复回调）
- 取消完成后写入统一结束事件（含 `reason=user_cancel`）

### 约束 28：SQLite 一致性策略

知识库和经验库采用 SQLite 时必须定义一致性边界：
- 启用 WAL 模式，读写并发时减少锁冲突
- 每个任务写入使用显式事务（BEGIN/COMMIT），失败自动回滚
- FTS5 索引更新与主表写入在同一事务中完成
- 启动时执行轻量一致性检查（关键表存在、索引可查询）

### 约束 29：可观测性 Trace 贯穿

任务链路中必须有统一 `trace_id`：
- 每条用户消息生成 `trace_id`，向子 Agent/工具调用/网络请求透传
- 日志、审计记录、错误事件必须携带 `trace_id`
- 跨插件与跨进程 IPC 消息中必须保留 `trace_id`
- UI 调试面板按 `trace_id` 聚合展示，支持一键回放

### 约束 30：性能基线与退化阈值

必须定义可验证的性能基线，避免“功能可用但体验不可用”：
- 场景 A（5 个并行任务）：UI 主线程帧率 >= 30 FPS
- 场景 B（10 个并行流式输出）：输入响应延迟 P95 <= 150 ms
- 常驻内存基线：2 小时运行内存增长不超过 20%
- 触发退化阈值时自动启用保护策略（暂停后台渲染、降采样日志）

### 约束 31：测试矩阵与发布门禁

每个演进阶段都需要最小可发布测试集：
- 单元测试：状态机、路由规则、权限校验、存储读写
- 集成测试：多 Agent 协作链路（含子 Agent 创建/销毁）
- 压力测试：并发任务 + 流式输出 + 插件消息同时进行
- 故障注入：网络超时、插件崩溃、数据库锁冲突、进程重启恢复
- 发布门禁：关键用例通过率 100%，非关键用例通过率 >= 95%

---

## 十、约束与愿景章节对照表

| 约束编号 | 约束名称 | 对应愿景章节 |
|---------|---------|-------------|
| 1 | GUI 主线程不可阻塞 | 三 3.3（子Agent工作流程）、四 4.1（界面结构） |
| 2 | 多 Agent 并行需要工作线程 | 三 3.4（为什么需要双层） |
| 3 | QEventLoop 嵌套风险 | 三 3.2（子Agent定义） |
| 4 | 子 Agent 创建与销毁 | 三 3.2（子Agent完成后销毁） |
| 5 | Provider 热切换 | 十二 阶段5（智能路由） |
| 6 | AgentEventBus 信号连接 | 八 原则4（独立运行单元） |
| 7 | QNetworkAccessManager 线程亲和性 | 三 3.4（多任务并行） |
| 8 | SSE 流式解析缓冲区 | 四 4.1（聊天区流式输出） |
| 9 | WebSocket 长连接 | 十一（外部通讯渠道） |
| 10 | abort() 竞态条件 | 三 3.4（多任务并行） |
| 11 | QPluginLoader ABI 兼容性 | 十 10.2（插件类型） |
| 12 | 插件接口不能用 STL 容器 | 十 10.2（插件类型） |
| 13 | 插件 QWidget 生命周期 | 十 10.2（扩展插件） |
| 14 | 插件热加载限制 | 十 10.3（插件生命周期） |
| 15 | 存储方案选择 | 九（资源库体系） |
| 16 | 向量检索需第三方库 | 九 9.1（知识库） |
| 17 | QFileSystemWatcher 限制 | 九 9.1（文件库） |
| 18 | Tab 切换实现方案 | 四 4.2（Tab切换机制） |
| 19 | QChatWidget 多实例支持 | 四 4.2（Tab切换机制） |
| 20 | 多 Tab 流式输出性能 | 三 3.4（多任务并行） |
| 21 | 单实例应用保证 | 十三（不做什么：本地部署） |
| 22 | 外部通讯渠道进程隔离 | 十一 11.2（架构设计） |
| 23 | Qt 5.14.2 版本限制 | 全局 |
| 24 | C++17 编译器支持 | 全局 |
| 25 | OpenSSL 依赖 | 十一（外部通讯渠道）、九 9.1（MCP服务器） |
| 26 | 并发预算与限流 | 十二 阶段3（群聊与协作）、十二 12.8（阶段边界） |
| 27 | 统一取消语义 | 十五（任务状态机与失败补偿） |
| 28 | SQLite 一致性策略 | 九 9.3（资源库存储方式） |
| 29 | Trace 贯穿 | 十二 阶段4（可观测性） |
| 30 | 性能基线与退化阈值 | 十二 阶段4（可观测性）、十四（成功标准） |
| 31 | 测试矩阵与发布门禁 | 十二 12.8（阶段范围边界） |

---

*文档版本：v1.2*
*创建日期：2026-02-08*
*关联文档：项目愿景与产品规划.md*
