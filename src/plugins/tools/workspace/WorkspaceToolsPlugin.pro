QT += core
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = WorkspaceToolsPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$clean_path($$PWD/../../../..)
INCLUDEPATH += $$REPO_ROOT/src \
               $$REPO_ROOT/src/core/tools

SOURCES += \
    $$PWD/WorkspaceToolsPlugin.cpp \
    $$PWD/WorkspaceToolProvider.cpp \
    $$PWD/FileTool.cpp \
    $$PWD/PatchTool.cpp \
    $$PWD/WorkspaceToolSchemas.cpp

HEADERS += \
    $$PWD/WorkspaceToolsPlugin.h \
    $$PWD/WorkspaceToolProvider.h \
    $$PWD/FileTool.h \
    $$PWD/PatchTool.h \
    $$PWD/WorkspaceToolSchemas.h

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
