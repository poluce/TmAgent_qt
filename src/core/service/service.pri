# src/core/service/service.pri — 服务层

INCLUDEPATH += \
    $$PWD \
    $$PWD/include \
    $$PWD/conversation \
    $$PWD/background \
    $$PWD/runtime \
    $$PWD/governance \
    $$PWD/state \
    $$PWD/observability

SOURCES += \
    $$PWD/runtime/AgentRuntime.cpp \
    $$PWD/background/AgentPulse.cpp \
    $$PWD/ChatCoordinatorFactory.cpp \
    $$PWD/conversation/ConversationDispatchCoordinator.cpp \
    $$PWD/conversation/ConversationEnqueueCoordinator.cpp \
    $$PWD/conversation/ConversationErrorCoordinator.cpp \
    $$PWD/conversation/ConversationFinishCoordinator.cpp \
    $$PWD/conversation/ConversationFinalizeCoordinator.cpp \
    $$PWD/conversation/ConversationMemoryFinishCoordinator.cpp \
    $$PWD/conversation/ConversationStreamCoordinator.cpp \
    $$PWD/conversation/ConversationToolEventCoordinator.cpp \
    $$PWD/conversation/ConversationToolPersistenceCoordinator.cpp \
    $$PWD/background/DelegateSettlementCoordinator.cpp \
    $$PWD/background/HeartbeatSnapshotCoordinator.cpp \
    $$PWD/background/HeartbeatDispatchCoordinator.cpp \
    $$PWD/background/SchedulerTriggerCoordinator.cpp \
    $$PWD/background/HealthMonitor.cpp \
    $$PWD/background/HeartbeatWake.cpp \
    $$PWD/background/HeartbeatService.cpp \
    $$PWD/background/HeartbeatReplyUtils.cpp \
    $$PWD/background/SchedulerService.cpp \
    $$PWD/background/TaskStateService.cpp \
    $$PWD/runtime/RuntimeManager.cpp \
    $$PWD/governance/ConfigService.cpp \
    $$PWD/state/ChatStateRepository.cpp \
    $$PWD/observability/ExecutionHistoryModel.cpp \
    $$PWD/conversation/MessageRouter.cpp \
    $$PWD/ConversationContextTypes.cpp \
    $$PWD/ChatService.cpp

HEADERS += \
    $$PWD/include/ChatCapabilityInterfaces.h \
    $$PWD/include/ChatCoordinatorSupport.h \
    $$PWD/include/ChatCoordinatorFactory.h \
    $$PWD/include/AgentPulseRegistry.h \
    $$PWD/include/HeartbeatPromptBuilder.h \
    $$PWD/include/HeartbeatRuntimeState.h \
    $$PWD/include/HeartbeatStateStore.h \
    $$PWD/include/PrimarySessionResolver.h \
    $$PWD/include/AgentRuntime.h \
    $$PWD/include/AgentPulse.h \
    $$PWD/include/ConversationDispatchCoordinator.h \
    $$PWD/include/ConversationEnqueueCoordinator.h \
    $$PWD/include/ConversationErrorCoordinator.h \
    $$PWD/include/ConversationFinishCoordinator.h \
    $$PWD/include/ConversationFinalizeCoordinator.h \
    $$PWD/include/ConversationMemoryFinishCoordinator.h \
    $$PWD/include/ConversationStreamCoordinator.h \
    $$PWD/include/ConversationToolEventCoordinator.h \
    $$PWD/include/ConversationToolPersistenceCoordinator.h \
    $$PWD/include/DelegateSettlementCoordinator.h \
    $$PWD/include/HeartbeatSnapshotCoordinator.h \
    $$PWD/include/HeartbeatDispatchCoordinator.h \
    $$PWD/include/SchedulerTriggerCoordinator.h \
    $$PWD/include/HealthMonitor.h \
    $$PWD/include/HeartbeatWake.h \
    $$PWD/include/HeartbeatService.h \
    $$PWD/include/HeartbeatReplyUtils.h \
    $$PWD/include/SchedulerService.h \
    $$PWD/include/TaskStateService.h \
    $$PWD/include/RuntimeManager.h \
    $$PWD/include/ConfigService.h \
    $$PWD/include/ChatStateRepository.h \
    $$PWD/include/ExecutionHistoryModel.h \
    $$PWD/include/MessageRouter.h \
    $$PWD/include/TurnManager.h \
    $$PWD/include/ConversationContextTypes.h \
    $$PWD/include/ChatService.h


