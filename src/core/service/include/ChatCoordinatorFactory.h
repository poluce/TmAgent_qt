#ifndef CHATCOORDINATORFACTORY_H
#define CHATCOORDINATORFACTORY_H

#include "ChatService.h"
#include "PrimarySessionResolver.h"
#include "HeartbeatPromptBuilder.h"
#include "HeartbeatStateStore.h"
#include "ConversationDispatchCoordinator.h"
#include "ConversationEnqueueCoordinator.h"
#include "ConversationErrorCoordinator.h"
#include "ConversationFinishCoordinator.h"
#include "ConversationFinalizeCoordinator.h"
#include "ConversationMemoryFinishCoordinator.h"
#include "ConversationStreamCoordinator.h"
#include "ConversationToolEventCoordinator.h"
#include "ConversationToolPersistenceCoordinator.h"
#include "DelegateSettlementCoordinator.h"
#include "HeartbeatDispatchCoordinator.h"
#include "SchedulerTriggerCoordinator.h"

class ChatCoordinatorFactory {
public:
    explicit ChatCoordinatorFactory(ChatService& service);

    PrimarySessionResolver makePrimarySessionResolver() const;
    HeartbeatPromptBuilder makeHeartbeatPromptBuilder() const;
    HeartbeatStateStore makeHeartbeatStateStore() const;

    ConversationEnqueueCoordinator::Dependencies makeEnqueueDependencies();
    ConversationEnqueueCoordinator::Limits makeEnqueueLimits() const;

    ConversationDispatchCoordinator::Dependencies makeDispatchDependencies();
    ConversationDispatchCoordinator::Limits makeDispatchLimits() const;

    ConversationFinalizeCoordinator::Dependencies makeFinalizeDependencies();
    ConversationStreamCoordinator::Dependencies makeStreamDependencies();
    ConversationFinishCoordinator::Dependencies makeFinishDependencies();
    ConversationMemoryFinishCoordinator::Dependencies makeMemoryFinishDependencies();
    ConversationErrorCoordinator::Dependencies makeErrorDependencies();
    ConversationToolEventCoordinator::Dependencies makeToolEventDependencies();
    ConversationToolPersistenceCoordinator::Dependencies makeToolPersistenceDependencies();
    DelegateSettlementCoordinator::Dependencies makeDelegateSettlementDependencies();

    HeartbeatDispatchCoordinator::Dependencies makeHeartbeatDispatchDependencies(
        HeartbeatRuntimeState& runtimeState,
        bool& shouldPersistState,
        const QDateTime& nowUtc);
    SchedulerTriggerCoordinator::Dependencies makeSchedulerTriggerDependencies();

private:
    ChatService& m_service;
};

#endif // CHATCOORDINATORFACTORY_H

