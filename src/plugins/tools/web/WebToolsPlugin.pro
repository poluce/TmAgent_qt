QT += core network
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = WebToolsPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$PWD/../../../..

# Include SDK
SDK_PATH = $$REPO_ROOT/tmagent-plugin-sdk
include($$SDK_PATH/tmagent-plugin-sdk.pri)

# ToolSchemaSupport is now in SDK - no need for src/core/tools

SOURCES += \
    $$PWD/WebToolsPlugin.cpp \
    $$PWD/WebToolProvider.cpp \
    $$PWD/WebTool.cpp \
    $$PWD/ExternalSearchTool.cpp

HEADERS += \
    $$PWD/WebToolsPlugin.h \
    $$PWD/WebToolProvider.h \
    $$PWD/WebTool.h \
    $$PWD/ExternalSearchTool.h

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
