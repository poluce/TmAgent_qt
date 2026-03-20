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
    $$PWD/runtime/CodexAppServerClient.cpp \
    $$PWD/runtime/CodexTeammateBackend.cpp \
    $$PWD/runtime/TeammateManager.cpp \
    $$PWD/background/AgentPulse.cpp \
    $$PWD/MemoryMaintenanceService.cpp \
    $$PWD/MemoryToolWriteService.cpp \
    $$PWD/ChatCoordinatorFactory.cpp \
    $$PWD/conversation/ConversationDispatchCoordinator.cpp \
    $$PWD/conversation/ConversationContextService.cpp \
    $$PWD/conversation/ConversationEnqueueCoordinator.cpp \
    $$PWD/conversation/ConversationStreamCoordinator.cpp \
    $$PWD/conversation/TurnCompletionCoordinator.cpp \
    $$PWD/conversation/ToolEventCoordinator.cpp \
    $$PWD/background/BackgroundTaskCoordinator.cpp \
    $$PWD/background/HeartbeatSnapshotCoordinator.cpp \
    $$PWD/background/HeartbeatDispatchCoordinator.cpp \
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
    $$PWD/WorkspaceService.cpp \
    $$PWD/ConversationService.cpp \
    $$PWD/GovernanceService.cpp \
    $$PWD/MemoryService.cpp \
    $$PWD/ApplicationServices.cpp

HEADERS += \
    $$PWD/include/AppFacade.h \
    $$PWD/include/WorkspaceService.h \
    $$PWD/include/ConversationService.h \
    $$PWD/include/GovernanceService.h \
    $$PWD/include/MemoryService.h \
    $$PWD/include/ChatCoordinatorSupport.h \
    $$PWD/include/ChatCoordinatorFactory.h \
    $$PWD/include/AgentPulseRegistry.h \
    $$PWD/include/MemoryMaintenanceService.h \
    $$PWD/include/MemoryToolWriteService.h \
    $$PWD/include/HeartbeatPromptBuilder.h \
    $$PWD/include/HeartbeatRuntimeState.h \
    $$PWD/include/HeartbeatStateStore.h \
    $$PWD/include/PrimarySessionResolver.h \
    $$PWD/include/AgentRuntime.h \
    $$PWD/include/CodexAppServerClient.h \
    $$PWD/include/ITeammateBackend.h \
    $$PWD/include/CodexTeammateBackend.h \
    $$PWD/include/TeammateManager.h \
    $$PWD/include/AgentPulse.h \
    $$PWD/include/ConversationDispatchCoordinator.h \
    $$PWD/include/ConversationContextService.h \
    $$PWD/include/ConversationEnqueueCoordinator.h \
    $$PWD/include/ConversationStreamCoordinator.h \
    $$PWD/include/TurnCompletionCoordinator.h \
    $$PWD/include/ToolEventCoordinator.h \
    $$PWD/include/CoordinatorContext.h \
    $$PWD/include/BackgroundTaskCoordinator.h \
    $$PWD/include/HeartbeatSnapshotCoordinator.h \
    $$PWD/include/HeartbeatDispatchCoordinator.h \
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
    $$PWD/include/ApplicationServices.h


