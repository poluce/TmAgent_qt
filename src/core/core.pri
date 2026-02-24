# src/core/core.pri — Core 聚合模块
# 聚合 core 下所有子模块

include($$PWD/agent/agent.pri)
include($$PWD/model/model.pri)
include($$PWD/service/service.pri)
include($$PWD/tools/tools.pri)

# memory
SOURCES += \
    $$PWD/memory/MemoryDocument.cpp \
    $$PWD/memory/MemoryManager.cpp
HEADERS += \
    $$PWD/memory/MemoryDocument.h \
    $$PWD/memory/MemoryManager.h

# manager
SOURCES += \
    $$PWD/manager/IdentityManager.cpp \
    $$PWD/manager/SessionManager.cpp
HEADERS += \
    $$PWD/manager/IdentityManager.h \
    $$PWD/manager/SessionManager.h

# persistence
SOURCES += \
    $$PWD/persistence/ChatPersistenceService.cpp
HEADERS += \
    $$PWD/persistence/ChatPersistenceService.h

# logging
SOURCES += \
    $$PWD/logging/LogQueryEngine.cpp
HEADERS += \
    $$PWD/logging/LogQueryEngine.h

# utils
SOURCES += \
    $$PWD/utils/ModelConfigLoader.cpp \
    $$PWD/utils/KeychainHelper.cpp \
    $$PWD/utils/ToolSchemaLoader.cpp
HEADERS += \
    $$PWD/utils/ModelConfigLoader.h \
    $$PWD/utils/KeychainHelper.h \
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
