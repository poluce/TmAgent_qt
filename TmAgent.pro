QT       += core gui network widgets
INCLUDEPATH += src

# 第三方库
include(3rdparty/yaml-cpp.pri)
include(3rdparty/tree-sitter.pri)

TARGET = TmAgent
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated.
DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++17

SOURCES += \
    src/main.cpp \
    src/core/agent/LLMAgent.cpp \
    src/core/agent/ToolDispatcher.cpp \
    src/core/utils/AppSettings.cpp \
    src/core/utils/ToolSchemaLoader.cpp \
    src/core/parser/TreeSitterParser.cpp \
    src/ui/AgentChatWidget.cpp

HEADERS += \
    src/core/agent/LLMAgent.h \
    src/core/agent/ToolDispatcher.h \
    src/core/utils/AppSettings.h \
    src/core/utils/ToolSchemaLoader.h \
    src/core/parser/TreeSitterParser.h \
    src/ui/AgentChatWidget.h

# FORMS += \
#    src/ui/LLMConfigWidget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target.path

# 自动复制 resources 目录到构建输出目录
win32 {
    RESOURCES_SRC_DIR = $$replace(PWD, /, \\)\\resources
    BUILD_DEST_DIR = $$replace(OUT_PWD, /, \\)

    CONFIG(debug, debug|release) {
        QMAKE_POST_LINK += xcopy /Y /E /I \"$$RESOURCES_SRC_DIR\" \"$$BUILD_DEST_DIR\\debug\\resources\\\"
    } else {
        QMAKE_POST_LINK += xcopy /Y /E /I \"$$RESOURCES_SRC_DIR\" \"$$BUILD_DEST_DIR\\release\\resources\\\"
    }
}



