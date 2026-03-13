QT       += core gui network widgets sql
INCLUDEPATH += src

# 第三方库
include(3rdparty/yaml-cpp.pri)
include(3rdparty/tree-sitter.pri)
include(3rdparty/qtkeychain/qtkeychain.pri)

# QChatWidget 子模块
INCLUDEPATH += $$PWD/QChatWidget/src
include($$PWD/QChatWidget/src/chatwidget/chat_widget.pri)
include($$PWD/QChatWidget/src/chatlist/chat_list.pri)
include($$PWD/QChatWidget/src/modelconfig/modelconfig.pri)
include($$PWD/QChatWidget/src/profile/profile_widget.pri)

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

# 解决 cc1plus.exe out of memory 问题
QMAKE_CXXFLAGS += -Wa,-mbig-obj
CONFIG(release, debug|release) {
    QMAKE_CXXFLAGS += -O2
} else {
    QMAKE_CXXFLAGS += -O0
}
