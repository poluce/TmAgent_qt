QT += core network
TEMPLATE = lib
CONFIG += plugin c++17
TARGET = CodeIntelToolsPlugin

DEFINES += QT_DEPRECATED_WARNINGS

REPO_ROOT = $$clean_path($$PWD/../../../..)
INCLUDEPATH += $$REPO_ROOT/src \
               $$REPO_ROOT/src/core/tools

include($$REPO_ROOT/3rdparty/tree-sitter.pri)

SOURCES += \
    $$PWD/CodeIntelToolsPlugin.cpp \
    $$PWD/CodeIntelToolProvider.cpp \
    $$PWD/CodeParserTool.cpp \
    $$PWD/LspTool.cpp \
    $$PWD/LspInstallTool.cpp \
    $$REPO_ROOT/src/core/parser/TreeSitterParser.cpp \
    $$REPO_ROOT/src/core/lsp/JsonRpcTransport.cpp \
    $$REPO_ROOT/src/core/lsp/LspClient.cpp \
    $$REPO_ROOT/src/core/lsp/LspServerManager.cpp \
    $$REPO_ROOT/src/core/lsp/LspDownloader.cpp \
    $$REPO_ROOT/src/core/lsp/BuildSystemAdapter.cpp

HEADERS += \
    $$PWD/CodeIntelToolsPlugin.h \
    $$PWD/CodeIntelToolProvider.h \
    $$PWD/CodeParserTool.h \
    $$PWD/LspTool.h \
    $$PWD/LspInstallTool.h \
    $$REPO_ROOT/src/core/agent/AgentEventBus.h \
    $$REPO_ROOT/src/core/parser/TreeSitterParser.h \
    $$REPO_ROOT/src/core/lsp/JsonRpcTransport.h \
    $$REPO_ROOT/src/core/lsp/LspClient.h \
    $$REPO_ROOT/src/core/lsp/LspProtocol.h \
    $$REPO_ROOT/src/core/lsp/LspServerManager.h \
    $$REPO_ROOT/src/core/lsp/LspDownloader.h \
    $$REPO_ROOT/src/core/lsp/BuildSystemAdapter.h \
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
