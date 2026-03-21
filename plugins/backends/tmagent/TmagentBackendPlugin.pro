QT += core network sql
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = TmagentBackendPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$clean_path($$PWD/../../..)
INCLUDEPATH += $$REPO_ROOT/src \
               $$REPO_ROOT/src/core/service/include

include($$REPO_ROOT/3rdparty/yaml-cpp.pri)
include($$REPO_ROOT/3rdparty/tree-sitter.pri)
include($$REPO_ROOT/3rdparty/qtkeychain/qtkeychain.pri)
include($$REPO_ROOT/src/llm/llm.pri)
include($$REPO_ROOT/src/core/core.pri)

SOURCES += \
    $$PWD/TmagentBackendPlugin.cpp

HEADERS += \
    $$PWD/TmagentBackendPlugin.h \
    $$REPO_ROOT/src/core/backend/IBackendPlugin.h

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
