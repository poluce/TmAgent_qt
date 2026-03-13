# ChatWidget 代码规范

本文档定义本仓库的命名、结构、API 设计与信号/槽约定。

## 1) 命名规范

- **文件名**：统一 `lower_snake`（如 `chat_list_view.h`、`chat_widget_input.cpp`）
- **.pro 文件名**：首字母大写的驼峰（如 `WeChatStyle.pro`、`ChatWidgetLib.pro`）
- **类名**：`PascalCase`，带模块前缀  
  - 聊天列表模块：`ChatList*`  
  - 聊天窗口模块：`ChatWidget*`  
  - 组件主入口不使用 `Q` 前缀（如 `ChatWidget`）
- **方法/函数**：`camelCase`
- **成员变量**：`m_` + `camelCase`
- **枚举/角色**：枚举名和枚举值都带模块前缀（如 `ChatListRoles::ChatListNameRole`）
- **头文件保护**：`MODULE_FILE_H`（如 `CHAT_LIST_VIEW_H`）

## 2) 目录结构

- `src/chatlist/`：聊天列表组件
- `src/chatwidget/`：聊天窗口组件
- `resources/styles/`：样式表目录（每个模块一个 .qss）
- `lib/`：库工程 .pro
- `test/`：示例工程（不进入库）

## 3) 头文件格式

顺序：
1) include guard  
2) Qt/标准库头  
3) 项目内头  
4) 前置声明  
5) 类定义  

规则：
- 能前置声明就不 include
- 只 include 必要依赖
- 公共 API 保持最小化

## 4) 类内结构顺序

建议顺序：
1) `public`
2) `signals`
3) `public slots`
4) `protected / protected slots`
5) `private slots`
6) `private`

## 5) 信号/槽规范

- 信号名使用过去式或 `Requested` 结尾  
  - 例：`messageSent`、`stopRequested`
- 容器组件可**显式**转发核心信号  
- 不建议默认“全部转发”，除非需求明确

## 6) 对外 API 设计

原则：
- 常用操作应当“一行调用”
- 模型类应支持“部分更新”
- 不强制 UI 组合；复合控件可选

对外组件要求：
- 必须提供一个主入口组件（如 `ChatWidget`、`ChatListWidget`）
- 提供最小化配置接口
- 允许访问内部子组件（如 `listView()` / `searchBar()`）
- 样式可通过 API 调整（例如头像大小/形状/圆角）
- 样式表不自动加载，调用方需显式应用（如 `applyStyleSheetFile(...)` 或自行 `setStyleSheet`）

## 7) 代码风格

- 4 空格缩进
- `{` 与控制语句同一行
- 单行 `if` 可省略 `{}`，多行必须加
- Qt 信号/槽使用函数指针形式

## 8) 窗口与尺寸规范

- 避免在窗口/控件上使用**固定尺寸**（`setFixedWidth/Height/Size`），优先使用 `setMinimum*` / `setMaximum*`。
- 尽量依赖布局（`QVBoxLayout` / `QHBoxLayout` 等）完成自适应。
- 需要用户可调整时，优先使用可拖拽分割（如 `QSplitter`）。

## 8) 文档与示例

- 示例工程只放 `test/`
- README 必须包含：
  - 最简用法
  - 模块结构
  - 可选功能说明（搜索/过滤/流式）

## 9) 构建约定

- `chat_list.pri` 与 `chat_widget.pri` 是唯一对外入口
- `lib/ChatListLib.pro` / `lib/ChatWidgetLib.pro` 默认构建静态库
