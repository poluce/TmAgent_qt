# src/core/service/service.pri — 服务层

SOURCES += \
    $$PWD/AgentRuntime.cpp \
    $$PWD/RuntimeManager.cpp \
    $$PWD/ConfigService.cpp \
    $$PWD/ChatStateRepository.cpp \
    $$PWD/MessageRouter.cpp \
    $$PWD/ChatService.cpp

HEADERS += \
    $$PWD/AgentRuntime.h \
    $$PWD/RuntimeManager.h \
    $$PWD/ConfigService.h \
    $$PWD/ChatStateRepository.h \
    $$PWD/MessageRouter.h \
    $$PWD/TurnManager.h \
    $$PWD/ChatService.h
