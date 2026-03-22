# src/core/agent/agent.pri — Agent 框架

SOURCES += \
    $$PWD/delegate/CodexDelegateBackend.cpp \
    $$PWD/delegate/DelegateBackendSupport.cpp \
    $$PWD/DelegateTaskScheduler.cpp \
    $$PWD/LLMAgent.cpp \
    $$PWD/McpToolProvider.cpp \
    $$PWD/ToolPluginManager.cpp \
    $$PWD/delegate/TmagentDelegateBackend.cpp \
    $$PWD/ToolDispatcher.cpp

HEADERS += \
    $$PWD/HostedToolProvider.h \
    $$PWD/IToolPlugin.h \
    $$PWD/IToolPluginHost.h \
    $$PWD/IToolProvider.h \
    $$PWD/delegate/CodexDelegateBackend.h \
    $$PWD/delegate/DelegateBackendSupport.h \
    $$PWD/delegate/IDelegateBackend.h \
    $$PWD/DelegateTaskScheduler.h \
    $$PWD/LLMAgent.h \
    $$PWD/McpToolProvider.h \
    $$PWD/ToolPluginManager.h \
    $$PWD/ToolPluginTypes.h \
    $$PWD/delegate/TmagentDelegateBackend.h \
    $$PWD/ToolDispatcher.h \
    $$PWD/AgentEventBus.h \
    $$PWD/ToolTypes.h


