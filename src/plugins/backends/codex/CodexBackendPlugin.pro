QT += core network
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = CodexBackendPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$clean_path($$PWD/../../../..)
SDK_PATH = $$REPO_ROOT/tmagent-plugin-sdk

# 引入 SDK 配置
include($$SDK_PATH/tmagent-plugin-sdk.pri)

SOURCES += \
    $$PWD/CodexBackendPlugin.cpp \
    $$PWD/CodexDelegateBackend.cpp \
    $$PWD/CodexTeammateBackend.cpp \
    $$PWD/CodexAppServerClient.cpp

HEADERS += \
    $$PWD/CodexBackendPlugin.h \
    $$PWD/CodexDelegateBackend.h \
    $$PWD/CodexTeammateBackend.h \
    $$PWD/CodexAppServerClient.h

OBJECTS_DIR = $$OUT_PWD/.obj
MOC_DIR = $$OUT_PWD/.moc
RCC_DIR = $$OUT_PWD/.rcc
UI_DIR = $$OUT_PWD/.ui

win32 {
    QMAKE_CXXFLAGS += -Wa,-mbig-obj
}

CONFIG(debug, debug|release) {
    DESTDIR = $$REPO_ROOT/build-plugins/debug/plugins/backends
} else {
    DESTDIR = $$REPO_ROOT/build-plugins/release/plugins/backends
}
