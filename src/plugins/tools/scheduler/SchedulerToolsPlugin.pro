QT += core
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = SchedulerToolsPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $clean_path($PWD/../../../..)

# 引入 SDK
SDK_PATH = $REPO_ROOT/tmagent-plugin-sdk
include($SDK_PATH/tmagent-plugin-sdk.pri)

# ToolSchemaSupport is now in SDK - no need for src/core/tools
# Note: Still need src/core/tools for ToolFailureSupport (will be addressed separately)
INCLUDEPATH += $REPO_ROOT/src \
               $REPO_ROOT/src/core/tools

SOURCES += \
    $PWD/SchedulerToolsPlugin.cpp \
    $PWD/SchedulerToolProvider.cpp \
    $PWD/SchedulerTool.cpp

HEADERS += \
    $PWD/SchedulerToolsPlugin.h \
    $PWD/SchedulerToolProvider.h \
    $PWD/SchedulerTool.h

OBJECTS_DIR = $OUT_PWD/.obj
MOC_DIR = $OUT_PWD/.moc
RCC_DIR = $OUT_PWD/.rcc
UI_DIR = $OUT_PWD/.ui

win32 {
    QMAKE_CXXFLAGS += -Wa,-mbig-obj
}

CONFIG(debug, debug|release) {
    DESTDIR = $REPO_ROOT/build-plugins/debug/plugins/tools
} else {
    DESTDIR = $REPO_ROOT/build-plugins/release/plugins/tools
}
