# src/core/tools/tools.pri — 工具集

INCLUDEPATH += \
    $$PWD \
    $$PWD/../service/include

SOURCES += \
    $$PWD/../../plugins/tools/workspace/FileTool.cpp \
    $$PWD/../../plugins/tools/shell/ShellTool.cpp \
    $$PWD/../../plugins/tools/memory/MemoryTool.cpp \
    $$PWD/../../plugins/tools/scheduler/SchedulerTool.cpp \
    $$PWD/../../plugins/tools/codeintel/CodeParserTool.cpp \
    $$PWD/../../plugins/tools/memory/SessionSearchTool.cpp \
    $$PWD/../../plugins/tools/codeintel/LspTool.cpp \
    $$PWD/../../plugins/tools/codeintel/LspInstallTool.cpp \
    $$PWD/../../plugins/tools/web/ExternalSearchTool.cpp \
    $$PWD/../../plugins/tools/web/WebTool.cpp \
    $$PWD/../../plugins/tools/workspace/PatchTool.cpp \
    $$PWD/../../plugins/tools/coordination/AgentTool.cpp

HEADERS += \
    $$PWD/FileTool.h \
    $$PWD/ShellTool.h \
    $$PWD/MemoryTool.h \
    $$PWD/SchedulerTool.h \
    $$PWD/CodeParserTool.h \
    $$PWD/SessionSearchTool.h \
    $$PWD/LspTool.h \
    $$PWD/LspInstallTool.h \
    $$PWD/ExternalSearchTool.h \
    $$PWD/WebTool.h \
    $$PWD/PatchTool.h \
    $$PWD/AgentTool.h \
    $$PWD/EventLogTool.h \
    $$PWD/ToolSchemaSupport.h \
    $$PWD/AgentToolNames.h


