# src/core/core-base.pri — CLI 与 App 共享的核心模块
# 包含 agent、tools、utils、parser、lsp

include($$PWD/agent/agent.pri)
include($$PWD/tools/tools.pri)

# logging（EventLogTool 依赖）
SOURCES += \
    $$PWD/logging/LogQueryEngine.cpp \
    $$PWD/logging/LogDbUtils.cpp \
    $$PWD/logging/LogDbScanner.cpp \
    $$PWD/logging/LogFieldExtractor.cpp \
    $$PWD/logging/LogSummarizer.cpp \
    $$PWD/logging/LogFormatter.cpp \
    $$PWD/logging/LogScanner.cpp \
    $$PWD/logging/LogIndex.cpp \
    $$PWD/logging/AlertManager.cpp \
    $$PWD/logging/MetricsCollector.cpp \
    $$PWD/logging/LogHealthCheck.cpp \
    $$PWD/logging/LogSessionLister.cpp \
    $$PWD/logging/LogAgentLister.cpp
HEADERS += \
    $$PWD/logging/LogQueryEngine.h \
    $$PWD/logging/LogDbUtils.h \
    $$PWD/logging/LogDbScanner.h \
    $$PWD/logging/LogFieldExtractor.h \
    $$PWD/logging/LogSummarizer.h \
    $$PWD/logging/LogFormatter.h \
    $$PWD/logging/LogScanner.h \
    $$PWD/logging/LogIndex.h \
    $$PWD/logging/AlertManager.h \
    $$PWD/logging/MetricsCollector.h \
    $$PWD/logging/LogHealthCheck.h \
    $$PWD/logging/LogSessionLister.h \
    $$PWD/logging/LogAgentLister.h

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


