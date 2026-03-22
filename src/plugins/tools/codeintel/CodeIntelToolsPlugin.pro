QT += core
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = CodeIntelToolsPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$clean_path($$PWD/../../../..)
INCLUDEPATH += $$REPO_ROOT/src

SOURCES += \
    $$PWD/CodeIntelToolsPlugin.cpp

HEADERS += \
    $$PWD/CodeIntelToolsPlugin.h \
    $$REPO_ROOT/src/core/agent/HostedToolProvider.h \
    $$REPO_ROOT/src/core/agent/IToolPlugin.h \
    $$REPO_ROOT/src/core/agent/IToolPluginHost.h \
    $$REPO_ROOT/src/core/agent/IToolProvider.h \
    $$REPO_ROOT/src/core/agent/ToolPluginTypes.h \
    $$REPO_ROOT/src/core/agent/ToolTypes.h

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
