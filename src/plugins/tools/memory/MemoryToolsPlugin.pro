QT += core sql
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = MemoryToolsPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$clean_path($$PWD/../../../..)

# 引入 SDK
SDK_ROOT = $$REPO_ROOT/tmagent-plugin-sdk
include($$SDK_ROOT/tmagent-plugin-sdk.pri)

# 添加日志支持
include($$REPO_ROOT/src/core/logging/logging.pri)
INCLUDEPATH += $$REPO_ROOT/src/core/logging
INCLUDEPATH += $$REPO_ROOT/src/core/observability
INCLUDEPATH += $$REPO_ROOT/src/core/persistence

SOURCES += \
    $$PWD/MemoryToolsPlugin.cpp \
    $$PWD/MemoryToolProvider.cpp \
    $$PWD/MemoryTool.cpp \
    $$PWD/SessionSearchTool.cpp \
    $$REPO_ROOT/src/core/persistence/DatabaseManager.cpp

HEADERS += \
    $$PWD/MemoryToolsPlugin.h \
    $$PWD/MemoryToolProvider.h \
    $$PWD/MemoryTool.h \
    $$PWD/SessionSearchTool.h \
    $$PWD/EventLogTool.h \
    $$REPO_ROOT/src/core/persistence/DatabaseManager.h

OBJECTS_DIR = $$OUT_PWD/.obj
MOC_DIR = $$OUT_PWD/.moc
RCC_DIR = $$OUT_PWD/.rcc
UI_DIR = $$OUT_PWD/.ui

win32 {
    QMAKE_CXXFLAGS += -Wa,-mbig-obj
}

CONFIG(debug, debug|release) {
    DESTDIR = $$REPO_ROOT/build-plugins/debug/plugins/tools
} else {
    DESTDIR = $$REPO_ROOT/build-plugins/release/plugins/tools
}
