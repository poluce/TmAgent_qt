# QChatWidget 组件说明

> 状态：active（专项维护方案）

## 当前集成方式

本项目已将 [QChatWidget](https://github.com/poluce/QChatWidget) 源码直接纳入主仓库，路径仍为 `QChatWidget/`，用于聊天 UI（`ChatWidget`、`ChatWidgetInput`、`ChatListWidget` 等）。

这意味着：

- `QChatWidget` 的修改与主仓库代码一起提交
- `QChatWidget` 现在就是本项目聊天 UI 代码的一部分
- `QChatWidget` 已按嵌入式运行组件收口，不再维护内部 demo/test/历史计划目录
- 当前仍保留的子模块主要是 `3rdparty/qtkeychain`

## 依赖初始化

在项目根目录执行：

```powershell
git submodule update --init --recursive
```

或使用脚本（在项目根目录）：

```powershell
.\scripts\update-submodule.ps1
```

若在 Cursor/IDE 终端中遇到 `CreateFileMapping` 等错误，请在系统 **PowerShell** 或 **Git Bash** 中执行上述命令。

## 当前适配关系

- **TmAgent.pro / app.pro**  
  - `INCLUDEPATH += $$PWD/QChatWidget/src`  
  - `include($$PWD/QChatWidget/src/chatwidget/chat_widget.pri)`  
  - `include($$PWD/QChatWidget/src/chatlist/chat_list.pri)`（会话列表 ChatListWidget，作为本项目聊天 UI 结构的一部分继续维护）  
  - `include($$PWD/QChatWidget/src/modelconfig/modelconfig.pri)`（引入 `ModelConfigManagerPage` 与共享类型 `model_config_types.h`）  
  - `include($$PWD/QChatWidget/src/profile/profile_widget.pri)`

- **ModelConfig 适配**  
  - 顶部“导入模型”入口最终由 `MainWindow::onModelConfigImportClicked()` 统一处理，打开 `ModelConfigDialog`。  
  - `ModelConfigDialog` 内嵌 `ModelConfigManagerPage`，提供新建、编辑、启用/禁用、设为默认、连接验证等能力。  
  - 预设厂商：DeepSeek、OpenAI、Claude、Ollama、Gemini。共享字段定义统一来自 `model_config_types.h`。  
  - 若 QChatWidget 中 `modelconfig` 或 `qss_utils` 的路径/API 变更，需同步修改 `app.pro`、`src/ui/dialogs/ModelConfigDialog.cpp` 与相关 include 路径。

- **当前主 UI** 使用的 QChatWidget API：
  - `IdentityView` 承载主聊天区域，核心依赖 `ChatWidget`、`ChatWidgetInput`、`ChatWidgetView`、`ChatWidgetModel`、`ChatListWidget`、`ProfileWidget`。
  - `ChatWidget`：`addMessage`, `streamOutput`, `removeLastMessage`, `applyStyleSheetFile("chat_widget.qss")`, `messageSent`, `messageActionRequested`, `stopRequested`, `inputWidget()`。
  - `ChatWidgetInput`（`inputWidget()` 的 qobject_cast）：`setSendingState(bool)`；输入框仍通过 `findChild<QLineEdit*>("chatWidgetInputEdit")` 获取，与 QChatWidget 内 `setObjectName("chatWidgetInputEdit")` 一致。
  - `ChatListWidget`：`listView()`, `applyStyleSheetFile("chat_list.qss")`, `enableSearchFiltering`, `setSearchPlaceholder`, `setSearchRoles`, `addHeaderAction`, `addChatItem`, `updateChatItem`, `clearChats`, `removeCurrentChat()`；信号 `headerActionTriggered`, `chatItemActivated`, `chatItemRemoved`, `chatItemRenamed`。这些接口当前由 `src/ui/views/IdentityView.cpp` 与 `src/ui/support/ChatListUiSupport.cpp` 适配。
  - `ProfileWidget`：资料卡弹层由 `src/ui/support/ProfileUiSupport.cpp` 创建和填充。

- **样式**：相关 `.pri` 会统一引入 `QChatWidget/resources/styles.qrc`。当前主工程会实际加载 `chat_widget.qss`、`chat_list.qss`、`model_config_manager_page.qss`、`profile_widget.qss`。

## 后续修改时若出现编译/运行问题

1. **找不到头文件**  
   检查 QChatWidget 是否新增顶层目录或移动了 `src/chatwidget`，若有则相应修改 `TmAgent.pro` / `app.pro` 的 `INCLUDEPATH` 与 `include(...chat_widget.pri)` 路径。

2. **API 变更**  
   - ChatWidget/ChatWidgetInput：若修改或删除了 `addMessage`、`streamOutput`、`removeLastMessage`、`applyStyleSheetFile`、`inputWidget()`、`messageSent`、`messageActionRequested`、`stopRequested`，或输入控件的 `objectName`，需在 `src/ui/views/IdentityView.cpp` 与 `src/ui/support/ChatUiFlowSupport.cpp` 中做对应修改。  
   - ChatListWidget：若 `listView()`、`standardModel()`、`addChatItem`、`updateChatItem`、`clearChats`、`removeCurrentChat()` 或信号 `headerActionTriggered`/`chatItemActivated`/`chatItemRemoved`/`chatItemRenamed` 及其参数发生变更，或 `chat_list_roles.h` 中角色名/值变更，需同步修改 `src/ui/views/IdentityView.cpp` 与 `src/ui/support/ChatListUiSupport.cpp`。
   - ModelConfigManagerPage：若 `setProviders()`、`setYamlPath()`、`configSaved`、`configDeleted`、`defaultChanged`、`enabledToggled`、`testConnectionRequested` 或共享类型定义发生变化，需同步修改 `src/ui/dialogs/ModelConfigDialog.cpp`。

3. **样式不生效**  
   确认 QChatWidget 的 `resources/styles.qrc` 中仍包含 `styles/chat_widget.qss`，且前缀为 `/styles`。

---

*文档状态：active（专项维护方案）*
*关联文档：docs/10_方案/10-架构升级设计方案.md、docs/README.md*
