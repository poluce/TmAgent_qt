QT += core sql
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = CoordinationToolsPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$clean_path($$PWD/../../../..)
INCLUDEPATH += $$REPO_ROOT/src \
               $$REPO_ROOT/src/core/tools \
               $$REPO_ROOT/src/core/service/include

SOURCES += \
    $$PWD/CoordinationToolsPlugin.cpp \
    $$PWD/CoordinationToolProvider.cpp \
    $$PWD/AgentTool.cpp \
    $$REPO_ROOT/src/core/service/runtime/TeammateManager.cpp \
    $$REPO_ROOT/src/core/model/Teammate.cpp \
    $$REPO_ROOT/src/core/model/TeammateRuntimeAccess.cpp \
    $$REPO_ROOT/src/core/persistence/ChatPersistenceService.cpp \
    $$REPO_ROOT/src/core/persistence/DatabaseManager.cpp \
    $$REPO_ROOT/src/core/model/IdentityProfile.cpp \
    $$REPO_ROOT/src/core/backend/BackendPluginManager.cpp

HEADERS += \
    $$PWD/CoordinationToolsPlugin.h \
    $$PWD/CoordinationToolProvider.h \
    $$PWD/AgentTool.h \
    $$REPO_ROOT/src/core/service/include/TeammateManager.h \
    $$REPO_ROOT/src/core/model/Teammate.h \
    $$REPO_ROOT/src/core/model/TeammateRuntimeAccess.h \
    $$REPO_ROOT/src/core/persistence/ChatPersistenceService.h \
    $$REPO_ROOT/src/core/persistence/DatabaseManager.h \
    $$REPO_ROOT/src/core/model/IdentityProfile.h \
    $$REPO_ROOT/src/core/backend/BackendPluginManager.h \
    $$REPO_ROOT/src/core/backend/IBackendPlugin.h \
    $$REPO_ROOT/src/core/agent/IToolPlugin.h \
    $$REPO_ROOT/src/core/agent/IToolPluginHost.h \
    $$REPO_ROOT/src/core/agent/IToolProvider.h \
    $$REPO_ROOT/src/core/agent/ToolPluginTypes.h \
    $$REPO_ROOT/src/core/agent/ToolTypes.h \
    $$REPO_ROOT/src/core/tools/AgentToolNames.h

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
