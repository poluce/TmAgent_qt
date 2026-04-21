# src/core/agent/agent.pri — Agent 框架

SOURCES += \
    $PWD/delegate/DelegateBackendSupport.cpp \
    $PWD/DelegateTaskScheduler.cpp \
    $PWD/LLMAgent.cpp \
    $PWD/McpToolProvider.cpp \
    $PWD/delegate/TmagentDelegateBackend.cpp \
    $PWD/ToolPluginManager.cpp \
    $PWD/ToolDispatcher.cpp \
    $PWD/JsonSchemaValidator.cpp

HEADERS += \
    $PWD/IToolPlugin.h \
    $PWD/IToolPluginHost.h \
    $PWD/IToolProvider.h \
    $PWD/delegate/DelegateBackendSupport.h \
    $PWD/delegate/IDelegateBackend.h \
    $PWD/DelegateTaskScheduler.h \
    $PWD/LLMAgent.h \
    $PWD/McpToolProvider.h \
    $PWD/delegate/TmagentDelegateBackend.h \
    $PWD/ToolPluginManager.h \
    $PWD/ToolPluginTypes.h \
    $PWD/ToolDispatcher.h \
    $PWD/AgentEventBus.h \
    $PWD/ToolTypes.h \
    $PWD/JsonSchemaValidator.h



# ToolPluginHostImpl - SDK IToolPluginHost implementation
SOURCES += $PWD/ToolPluginHostImpl.cpp
HEADERS += $PWD/ToolPluginHostImpl.h

# Adapter classes - Bridge SDK interfaces to main application
SOURCES += \
    $PWD/ToolExecutorAdapter.cpp \
    $PWD/ConfigAdapter.cpp \
    $PWD/ModelFactoryAdapter.cpp \
    $PWD/LegacyPluginAdapter.cpp

HEADERS += \
    $PWD/ToolExecutorAdapter.h \
    $PWD/ConfigAdapter.h \
    $PWD/ModelFactoryAdapter.h \
    $PWD/LegacyPluginAdapter.h

