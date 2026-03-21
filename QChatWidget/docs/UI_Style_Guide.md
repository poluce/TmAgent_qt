# QChatWidget UI 样式指南

> 版本：v1.1  
> 更新时间：2026-03-22  
> 适用范围：`QChatWidget` 当前仍保留的运行时组件与样式资源

本文档是 `QChatWidget` 当前唯一的样式规范。`QChatWidget` 已按主仓库内嵌运行组件收口，不再维护内部 demo/test 或历史设计方案目录；样式维护应直接围绕主工程当前在用的组件展开，并配合 [`design_tokens.md`](./design_tokens.md) 一起阅读。

## 1. 当前样式边界

当前保留并在主工程中实际使用的样式文件只有：

```text
resources/styles/
├── global.qss
├── chat_list.qss
├── chat_widget.qss
├── model_config_manager_page.qss
└── profile_widget.qss
```

对应职责如下：

| 文件 | 职责 | 当前作用对象 |
| --- | --- | --- |
| `global.qss` | 全局基础样式 | 通用按钮、输入框、菜单、基础视觉 token |
| `chat_list.qss` | 会话列表样式 | `ChatListWidget` 及其搜索栏、列表、菜单 |
| `chat_widget.qss` | 聊天区样式 | `ChatWidget`、输入区、消息区、相关菜单 |
| `model_config_manager_page.qss` | 模型配置页样式 | `ModelConfigManagerPage` |
| `profile_widget.qss` | 资料卡样式 | `ProfileWidget` |

## 2. 基本原则

### 2.1 单一职责

每个 QSS 文件只描述一个组件或一个稳定的样式层级：

- `chat_list.qss` 只放会话列表相关规则
- `chat_widget.qss` 只放聊天区相关规则
- 公共规则统一收敛到 `global.qss`

### 2.2 先公共、后局部

共性的按钮、输入框、菜单样式优先落在 `global.qss`，局部文件只补充特有结构或状态，避免多处重复维护同一组颜色、圆角和间距。

### 2.3 设计令牌优先

颜色、圆角、间距和按钮尺寸优先复用 [`design_tokens.md`](./design_tokens.md) 中已经定义的值；如果新增视觉变量，应先补充设计令牌，再落具体样式。

### 2.4 显式加载

`QChatWidget` 不自动注入样式。调用方需要通过 `applyStyleSheetFile(...)` 或 `QssUtils::applyStyleSheetFromFile(...)` 显式加载对应资源。

## 3. 当前加载约定

运行时样式由 `.pri` 自动引入 `resources/styles.qrc`，组件侧按需显式加载：

```cpp
chatListWidget->applyStyleSheetFile("chat_list.qss");
chatWidget->applyStyleSheetFile("chat_widget.qss");
profileWidget->applyStyleSheetFile("profile_widget.qss");
page->applyStyleSheet(); // ModelConfigManagerPage 内部组合 global + page-specific
```

推荐顺序是：

1. 先通过 `global.qss` 提供基础控件规则。
2. 再叠加组件自己的局部样式文件。

## 4. 修改工作流

整理样式时按下面顺序走：

1. 先定位问题属于哪个运行时组件。
2. 判断是公共规则还是组件特有规则。
3. 检查 `design_tokens.md` 中是否已有可复用 token。
4. 修改对应 QSS。
5. 在主工程实际运行界面中验证：
   - 会话列表
   - 聊天区
   - 资料卡
   - 模型配置页
6. 如视觉语义变化明显，同步更新本文件和 `design_tokens.md`。

## 5. 常见注意点

- 使用对象名选择器前，先确认对应控件在 C++ 里稳定设置了 `objectName`。
- 修改 `global.qss` 时，要回归所有当前在用组件，而不是只看单个页面。
- 不要再为已删除的 demo/test 添加专用样式文件；新增样式必须服务当前主工程运行面。
- 如果发现样式规范与主工程现状不一致，以当前主工程运行行为为准，并回写本文件。

## 6. 参考文档

- [设计令牌文档](./design_tokens.md)
- [ChatWidget 开发者手册](./ChatWidget开发者手册.md)
- [主仓库中的 QChatWidget 组件说明](../../docs/10_方案/12-QChatWidget组件说明.md)
