# ChatWidget

一个可移植的 Qt 对话窗口组件,支持 Markdown 渲染与流式输出。**本项目仅提供源码**,通过 `.pri` 文件集成到你的项目中。

## 目录说明
- `src/chatwidget/`：聊天窗口组件源码
- `src/chatlist/`：聊天列表组件源码
- `test/we_chat_style/WeChatStyle.pro`：聊天窗口示例应用
- `test/we_chat_list_demo/WeChatListDemo.pro`：聊天列表示例应用
- `.agent/Agent.md`：项目架构文档

## 聊天列表示例（带搜索）
`ChatListWidget` 是一个可选复合控件（搜索栏 + 列表），默认不强制使用。  
示例里通过 `enableSearchFiltering(true)` 开启过滤，并设置搜索角色（如昵称 + 最后消息）。
样式表需显式调用（如 `chatListWidget->applyStyleSheetFile("chat_list.qss")`）。

## 使用说明

> [!IMPORTANT]
> **本项目仅提供源码,不支持预编译库。** 请通过 `.pri` 文件直接引入源码到你的项目中。

### 源码集成方式

在你的 `.pro` 中加入:
```
INCLUDEPATH += /path/to/QChatWidget/src
include(/path/to/QChatWidget/src/chatwidget/chat_widget.pri)
```

示例代码:
```
ChatWidget *chat = new ChatWidget(this);
chat->applyStyleSheetFile("chat_widget.qss");
layout->addWidget(chat);
connect(chat, &ChatWidget::messageSent, this, [](const QString &text){
    // 处理用户输入
});
```

## 示例应用
`test/we_chat_style/WeChatStyle.pro` 是完整示例,可直接运行查看效果。

## 更多信息
详细的架构说明、开发工作流和最佳实践请参考 [.agent/Agent.md](file:///.agent/Agent.md)。
