# src/core/agent/agent.pri — Agent 框架

SOURCES += \
    $$PWD/delegate/CodexDelegateBackend.cpp \
    $$PWD/delegate/DelegateBackendSupport.cpp \
    $$PWD/DelegateTaskScheduler.cpp \
    $$PWD/LLMAgent.cpp \
    $$PWD/LocalToolProvider.cpp \
    $$PWD/McpToolProvider.cpp \
    $$PWD/delegate/TmagentDelegateBackend.cpp \
    $$PWD/ToolDispatcher.cpp \
    $$PWD/ToolRegistry.cpp

HEADERS += \
    $$PWD/IToolProvider.h \
    $$PWD/delegate/CodexDelegateBackend.h \
    $$PWD/delegate/DelegateBackendSupport.h \
    $$PWD/delegate/IDelegateBackend.h \
    $$PWD/DelegateTaskScheduler.h \
    $$PWD/LLMAgent.h \
    $$PWD/LocalToolProvider.h \
    $$PWD/McpToolProvider.h \
    $$PWD/delegate/TmagentDelegateBackend.h \
    $$PWD/ToolDispatcher.h \
    $$PWD/ToolRegistry.h \
    $$PWD/AgentEventBus.h \
    $$PWD/ToolTypes.h


