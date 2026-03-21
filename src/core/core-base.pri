# src/core/core-base.pri — CLI 与 App 共享的核心模块
# 包含 agent、tools、utils、parser、lsp

include($$PWD/agent/agent.pri)
include($$PWD/tools/tools.pri)

# backend plugins
SOURCES += \
    $$PWD/backend/BackendPluginManager.cpp
HEADERS += \
    $$PWD/backend/IBackendPlugin.h \
    $$PWD/backend/BackendPluginManager.h

# logging（EventLogTool 依赖）
include($$PWD/logging/logging.pri)

# utils
SOURCES += \
    $$PWD/utils/ModelConfigLoader.cpp \
    $$PWD/utils/KeychainHelper.cpp \
    $$PWD/utils/OpenSslRuntimeLoader.cpp \
    $$PWD/utils/ToolSchemaLoader.cpp
HEADERS += \
    $$PWD/utils/ModelConfigLoader.h \
    $$PWD/utils/KeychainHelper.h \
    $$PWD/utils/OpenSslRuntimeLoader.h \
    $$PWD/utils/DefaultPrompts.h \
    $$PWD/utils/ToolSchemaLoader.h

# parser
SOURCES += \
    $$PWD/parser/TreeSitterParser.cpp
HEADERS += \
    $$PWD/parser/TreeSitterParser.h

# lsp
SOURCES += \
    $$PWD/lsp/JsonRpcTransport.cpp \
    $$PWD/lsp/LspClient.cpp \
    $$PWD/lsp/LspServerManager.cpp \
    $$PWD/lsp/LspDownloader.cpp \
    $$PWD/lsp/BuildSystemAdapter.cpp
HEADERS += \
    $$PWD/lsp/JsonRpcTransport.h \
    $$PWD/lsp/LspClient.h \
    $$PWD/lsp/LspProtocol.h \
    $$PWD/lsp/LspServerManager.h \
    $$PWD/lsp/LspDownloader.h \
    $$PWD/lsp/BuildSystemAdapter.h


