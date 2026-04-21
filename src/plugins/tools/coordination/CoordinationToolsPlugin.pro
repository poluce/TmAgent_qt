QT += core sql
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = CoordinationToolsPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$clean_path($$PWD/../../../..)

# 引入 SDK
SDK_PATH = $$REPO_ROOT/tmagent-plugin-sdk
include($$SDK_PATH/tmagent-plugin-sdk.pri)

# 仅包含必要的内部依赖（TeammateManager 等服务）
INCLUDEPATH += $$REPO_ROOT/src/core/service/include \
               $$REPO_ROOT/src/core/tools \
               $$REPO_ROOT/src/core/model \
               $$REPO_ROOT/src/core/persistence \
               $$REPO_ROOT/src/core/backend \
               $$REPO_ROOT/src

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
