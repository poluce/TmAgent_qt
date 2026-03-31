QT += core network
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = CodexBackendPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$clean_path($$PWD/../../../..)
INCLUDEPATH += $$REPO_ROOT/src \
               $$REPO_ROOT/src/core/service/include

SOURCES += \
    $$PWD/CodexBackendPlugin.cpp \
    $$PWD/CodexDelegateBackend.cpp \
    $$REPO_ROOT/src/core/agent/delegate/DelegateBackendSupport.cpp \
    $$PWD/CodexTeammateBackend.cpp \
    $$PWD/CodexAppServerClient.cpp \
    $$REPO_ROOT/src/core/model/Teammate.cpp \
    $$REPO_ROOT/src/core/model/TeammateRuntimeAccess.cpp

HEADERS += \
    $$PWD/CodexBackendPlugin.h \
    $$REPO_ROOT/src/core/backend/IBackendPlugin.h \
    $$PWD/CodexDelegateBackend.h \
    $$REPO_ROOT/src/core/agent/delegate/DelegateBackendSupport.h \
    $$REPO_ROOT/src/core/agent/delegate/IDelegateBackend.h \
    $$PWD/CodexTeammateBackend.h \
    $$PWD/CodexAppServerClient.h \
    $$REPO_ROOT/src/core/service/include/ITeammateBackend.h \
    $$REPO_ROOT/src/core/model/Teammate.h \
    $$REPO_ROOT/src/core/model/TeammateRuntimeAccess.h

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
