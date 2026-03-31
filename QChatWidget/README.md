# QChatWidget

`QChatWidget` 是 `TmAgent` 主仓库中的内嵌运行组件，负责聊天区、会话列表、资料卡和模型配置页的基础 UI 能力。它不再按“独立组件库 + 内部 demo/test”方式维护；当前目录只保留主工程实际依赖的运行时代码、样式、必要第三方源码和维护文档。

## 当前保留内容

- `src/chatwidget/`：聊天区组件
- `src/chatlist/`：会话列表组件
- `src/modelconfig/`：模型配置页面与共享类型
- `src/profile/`：资料卡弹层组件
- `src/common/qss_utils.*`：样式加载辅助
- `resources/styles/` 与 `resources/styles.qrc`：运行时样式资源
- `3rdparty/md4c/`：Markdown 渲染依赖
- `docs/ChatWidget开发者手册.md`：`ChatWidget` 使用与 API 说明
- `docs/UI_Style_Guide.md`：当前唯一的样式规范
- `docs/design_tokens.md`：样式设计令牌

## 主工程集成入口

主工程通过 `app.pro` 直接引入以下 `.pri`：

```qmake
INCLUDEPATH += $$PWD/QChatWidget/src
include($$PWD/QChatWidget/src/chatwidget/chat_widget.pri)
include($$PWD/QChatWidget/src/chatlist/chat_list.pri)
include($$PWD/QChatWidget/src/modelconfig/modelconfig.pri)
include($$PWD/QChatWidget/src/profile/profile_widget.pri)
```

这意味着：

- `QChatWidget` 以源码形式参与主程序编译
- 目录结构和头文件路径的变动会直接影响 `TmAgent`
- 当前维护目标是“服务主工程运行”，而不是对外发布独立库

## 维护提示

- 优先修改仍在主工程中被实际引用的模块；不要把已删除的 demo/test 维护方式带回来。
- 样式问题优先查看 `docs/UI_Style_Guide.md` 与 `docs/design_tokens.md`。
- `ChatWidget` 的具体 API 和行为说明优先查看 `docs/ChatWidget开发者手册.md`。
- 若调整 `.pri`、目录结构或资源路径，请同步检查主仓库中的 `docs/10_方案/12-QChatWidget组件说明.md`。
