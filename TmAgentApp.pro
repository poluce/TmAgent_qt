QT       += core gui network widgets sql
INCLUDEPATH += src

# 第三方库
include(3rdparty/yaml-cpp.pri)
include(3rdparty/tree-sitter.pri)
include(3rdparty/qtkeychain/qtkeychain.pri)

# QChatWidget 子模块（源码直引）
# chat_widget.pri 已带入 theme_manager、qss_utils、styles.qrc。只通过手动列 chatlist 源文件集成会话列表，
# 不 include(chat_list.pri)，否则 theme/qss 被编两次会产生 multiple definition 链接错误。
INCLUDEPATH += $$PWD/QChatWidget/src
include($$PWD/QChatWidget/src/chatwidget/chat_widget.pri)
CHATLIST_DIR = $$PWD/QChatWidget/src/chatlist
INCLUDEPATH += $$CHATLIST_DIR
SOURCES += $$CHATLIST_DIR/chat_list_delegate.cpp \
    $$CHATLIST_DIR/chat_list_filter_model.cpp \
    $$CHATLIST_DIR/chat_list_view.cpp \
    $$CHATLIST_DIR/chat_list_widget.cpp
HEADERS += $$CHATLIST_DIR/chat_list_roles.h \
    $$CHATLIST_DIR/chat_list_delegate.h \
    $$CHATLIST_DIR/chat_list_filter_model.h \
    $$CHATLIST_DIR/chat_list_view.h \
    $$CHATLIST_DIR/chat_list_widget.h
MODELCONFIG_DIR = $$PWD/QChatWidget/src/modelconfig
INCLUDEPATH += $$MODELCONFIG_DIR
HEADERS += $$MODELCONFIG_DIR/model_config_import_page.h
HEADERS += $$MODELCONFIG_DIR/model_config_manager_page.h
SOURCES += $$MODELCONFIG_DIR/model_config_import_page.cpp
SOURCES += $$MODELCONFIG_DIR/model_config_manager_page.cpp

# ProfileWidget（点击头像弹出的 Agent 信息卡片）
PROFILE_DIR = $$PWD/QChatWidget/src/profile
INCLUDEPATH += $$PROFILE_DIR
HEADERS += $$PROFILE_DIR/profile_widget.h
SOURCES += $$PROFILE_DIR/profile_widget.cpp

TARGET = TmAgent
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated.
DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++17
# 明确告知 qmake 项目包含 C 源码，md4c 为 C 文件
CONFIG += c

# 应用入口
SOURCES += src/main.cpp

# 模块化 .pri 引入
include(src/llm/llm.pri)
include(src/core/core.pri)
include(src/ui/ui.pri)

# FORMS += \
#    src/ui/LLMConfigWidget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target.path

# 自动复制 resources 目录到构建输出目录
win32 {
    RESOURCES_SRC_DIR = $$replace(PWD, /, \\)\\resources
    OPENSSL_SRC_DIR = $$replace(PWD, /, \\)\\openssl
    BUILD_DEST_DIR = $$replace(OUT_PWD, /, \\)

    CONFIG(debug, debug|release) {
        QMAKE_POST_LINK += xcopy /Y /E /I \"$$RESOURCES_SRC_DIR\" \"$$BUILD_DEST_DIR\\debug\\resources\" &
        QMAKE_POST_LINK += copy /Y \"$$OPENSSL_SRC_DIR\\*.dll\" \"$$BUILD_DEST_DIR\\debug\\\"
    } else {
        QMAKE_POST_LINK += xcopy /Y /E /I \"$$RESOURCES_SRC_DIR\" \"$$BUILD_DEST_DIR\\release\\resources\" &
        QMAKE_POST_LINK += copy /Y \"$$OPENSSL_SRC_DIR\\*.dll\" \"$$BUILD_DEST_DIR\\release\\\"
    }
}
