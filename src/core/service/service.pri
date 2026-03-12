# src/core/service/service.pri — 服务层

SOURCES += \
    $$PWD/AgentRuntime.cpp \
    $$PWD/AgentPulse.cpp \
    $$PWD/HealthMonitor.cpp \
    $$PWD/HeartbeatWake.cpp \
    $$PWD/HeartbeatService.cpp \
    $$PWD/HeartbeatReplyUtils.cpp \
    $$PWD/SchedulerService.cpp \
    $$PWD/RuntimeManager.cpp \
    $$PWD/ConfigService.cpp \
    $$PWD/ChatStateRepository.cpp \
    $$PWD/MessageRouter.cpp \
    $$PWD/ChatService.cpp

HEADERS += \
    $$PWD/AgentRuntime.h \
    $$PWD/AgentPulse.h \
    $$PWD/HealthMonitor.h \
    $$PWD/HeartbeatWake.h \
    $$PWD/HeartbeatService.h \
    $$PWD/HeartbeatReplyUtils.h \
    $$PWD/SchedulerService.h \
    $$PWD/RuntimeManager.h \
    $$PWD/ConfigService.h \
    $$PWD/ChatStateRepository.h \
    $$PWD/MessageRouter.h \
    $$PWD/TurnManager.h \
    $$PWD/ChatService.h
