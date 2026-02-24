# src/core/agent/agent.pri — Agent 框架

SOURCES += \
    $$PWD/DelegateTaskScheduler.cpp \
    $$PWD/LLMAgent.cpp \
    $$PWD/LocalToolProvider.cpp \
    $$PWD/McpToolProvider.cpp \
    $$PWD/ToolDispatcher.cpp \
    $$PWD/ToolRegistry.cpp

HEADERS += \
    $$PWD/IToolProvider.h \
    $$PWD/DelegateTaskScheduler.h \
    $$PWD/LLMAgent.h \
    $$PWD/LocalToolProvider.h \
    $$PWD/McpToolProvider.h \
    $$PWD/ToolDispatcher.h \
    $$PWD/ToolRegistry.h \
    $$PWD/AgentEventBus.h \
    $$PWD/ToolTypes.h
