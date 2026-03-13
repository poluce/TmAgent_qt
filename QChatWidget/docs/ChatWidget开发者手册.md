# ChatWidget 开发者手册

## 1. 简介与适用范围
ChatWidget 是一个可移植的 Qt 聊天窗口组件，面向 Qt 应用开发者提供**纯源码集成**方式。它基于 Qt 5.14.2 与 C++17，适用于 Qt Widgets 项目，可在不引入其他模块的情况下独立使用。

主要能力包括：
- Markdown 渲染（基于 md4c）
- 流式输出（打字机效果）

本手册仅覆盖 `chatwidget` 模块，不包含聊天列表（chatlist）与模型配置（modelconfig）相关内容。

## 2. 功能概览
ChatWidget 支持的主要功能包括：

- 消息展示：文本消息渲染、Markdown 渲染（基于 md4c）、流式输出/打字机效果
- 消息类型与状态：Text/Image/File/System/DateSeparator，支持 Sending/Sent/Failed/Read 等状态
- 消息管理：添加消息、更新消息内容/状态、移除最后一条、清空、统计数量
- 历史消息：批量设置/追加/前插历史消息，支持去重与排序
- 附加信息：图片/文件附件信息、回复信息、转发信息、表情反应、@提及
- 用户与参与者：设置当前用户、参与者信息维护（增改删/存在性判断）
- 交互事件：头像点击、消息选中、上下文菜单、消息动作请求、发送/停止信号
- 视图与输入：可替换输入组件、设置空状态提示、发送状态切换
- 搜索支持：设置搜索关键词（由视图/模型内部处理）
- 样式：加载样式表、设置 delegate 风格
- 内置模拟：模拟 AI 流式回复（定时器驱动）

## 3. 集成前提
在集成 ChatWidget 之前，请确保满足以下前提条件：

- 使用 Qt 5.14.2 及以上版本的 Qt Widgets 项目
- 编译标准为 C++17
- 以源码形式引入（通过 `.pri` 文件）

## 4. 集成步骤
ChatWidget 采用**源码集成**，通过 `.pri` 文件引入模块，不提供预编译库。

1. 在你的 `.pro` 文件中启用 Qt Widgets 与 C++17。
2. 添加 QChatWidget 源码路径到 `INCLUDEPATH`。
3. 只引入 `chat_widget.pri`（无需引入其他模块）。

```qmake
QT += core gui widgets
CONFIG += c++17

INCLUDEPATH += /path/to/QChatWidget/src
include(/path/to/QChatWidget/src/chatwidget/chat_widget.pri)
```

补充说明：
- `chat_widget.pri` 会自动引入 `resources/styles.qrc`，无需手动添加资源文件。
- 如果只使用 ChatWidget，请不要引入 `chat_list.pri` 或 `modelconfig.pri`。

## 5. 最小使用示例
下面是最小可运行示例，展示如何创建并显示 ChatWidget：

```cpp
// main.cpp
#include <QApplication>
#include "chatwidget/chat_widget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    ChatWidget *chat = new ChatWidget();
    chat->applyStyleSheetFile(":/styles/chat_widget.qss");
    chat->show();

    return app.exec();
}
```

## 6. 资源与样式
ChatWidget 的样式表不会自动加载，需要显式指定。`chat_widget.pri` 已自动引入资源文件 `resources/styles.qrc`，因此你可以直接使用 `:/styles/` 前缀加载内置样式。

### 6.1 使用内置样式
```cpp
chat->applyStyleSheetFile(":/styles/chat_widget.qss");
```

### 6.2 使用代码内样式
```cpp
chat->setStyleSheet("QListView { background: #1e1e1e; }");
```

### 6.3 使用外部样式文件
```cpp
chat->applyStyleSheetFile("/path/to/custom.qss");
```

## 7. API 速览（ChatWidget）
以下为 `ChatWidget` 的常用公开 API，便于快速集成时查阅。更底层的视图/模型细节请直接查看头文件。

### 7.1 消息相关
- `addMessage(const MessageParams& params)`：统一消息入口
- `streamOutput(const QString& content)`：流式追加到最后一条消息
- `removeLastMessage()` / `clearMessages()` / `messageCount()`

### 7.2 历史消息与状态更新
- `setHistoryMessages(const QList<HistoryMessage>& messages, bool resetParticipants = true)`
- `appendHistoryMessages(...)` / `prependHistoryMessages(...)`
- `updateMessageStatus(...)` / `updateMessageContent(...)`
- `updateMessageReactions(...)` / `updateMessageAttachments(...)` / `updateMessageReply(...)`

### 7.3 用户与参与者
- `setCurrentUser(const QString& userId, const QString& displayName = QString(), const QString& avatarPath = QString())`
- `currentUserId()`
- `upsertParticipant(...)` / `removeParticipant(...)`
- `clearParticipants()` / `hasParticipant(...)`

### 7.4 视图与输入
- `applyStyleSheetFile(...)`
- `setDelegateStyle(...)` / `delegateStyle()`
- `setInputWidget(ChatWidgetInputBase* widget)` / `inputWidget()`
- `setSendingState(bool sending)`
- `setEmptyStateVisible(bool visible, const QString& message = QString())` / `isEmptyStateVisible()`
- `setSearchKeyword(const QString& keyword)`

### 7.5 组件内置模拟
- `startSimulatedStreaming(const QString& content, int interval = 30)`：内部定时器模拟流式回复

### 7.6 主要信号
- `messageSent(const QString& content)`：输入框发送消息
- `avatarClicked(...)` / `selfAvatarClicked(...)` / `memberAvatarClicked(...)`
- `stopRequested()`
- `messageSelected(...)`
- `messageContextMenuRequested(...)`
- `messageActionRequested(...)`

### 7.7 关键数据结构（简要）
`ChatWidget::HistoryMessage` 常见字段包括：
`senderId`、`displayName`、`avatarPath`、`content`、`timestamp`、`isMine`、`messageId`，
以及 `messageType`、`status`、`imagePath`、`filePath`、`fileName`、`fileSize`、`reply*`、`reactions`、`mentions` 等。

### 7.8 行为说明与约定
- **Model 行为**：`setMessages/appendMessages/prependMessages` 会按 `timestamp` 排序，并基于 `messageId` 去重；空 `messageId` 不参与去重。
- **View 职责**：`ChatWidgetView` 仅负责展示与交互。数据更新请通过 `ChatWidget` 或直接操作 `ChatWidgetModel`。

## 8. 迁移提示（破坏性变更）
- `setCurrentUserId(...)` 已移除，请使用 `setCurrentUser(...)`。
- `updateParticipant(...)` 已移除，请使用 `upsertParticipant(...)`。
- `ChatWidgetView` 不再提供数据更新类接口（如 `updateMessage*`），请通过 `ChatWidget` 或 `ChatWidgetModel` 更新数据。
