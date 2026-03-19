#ifndef CHATCOORDINATORFACTORY_H
#define CHATCOORDINATORFACTORY_H

#include "ChatService.h"
#include "CoordinatorContext.h"
#include "PrimarySessionResolver.h"
#include "HeartbeatPromptBuilder.h"
#include "HeartbeatStateStore.h"
#include "ConversationDispatchCoordinator.h"
#include "ConversationEnqueueCoordinator.h"
#include "ConversationStreamCoordinator.h"
#include "TurnCompletionCoordinator.h"
#include "ToolEventCoordinator.h"
#include "BackgroundTaskCoordinator.h"
#include "HeartbeatDispatchCoordinator.h"

class ChatCoordinatorFactory {
public:
    explicit ChatCoordinatorFactory(ChatService& service);

    CoordinatorContext makeSharedContext();
    PrimarySessionResolver makePrimarySessionResolver() const;
    HeartbeatPromptBuilder makeHeartbeatPromptBuilder() const;
    HeartbeatStateStore makeHeartbeatStateStore() const;

    ConversationEnqueueCoordinator::Dependencies makeEnqueueDependencies();
    ConversationEnqueueCoordinator::Limits makeEnqueueLimits() const;

    ConversationDispatchCoordinator::Dependencies makeDispatchDependencies();
    ConversationDispatchCoordinator::Limits makeDispatchLimits() const;

    ConversationStreamCoordinator::Dependencies makeStreamDependencies();
    TurnCompletionCoordinator::Dependencies makeTurnCompletionDependencies();
    ToolEventCoordinator::Dependencies makeToolEventDependencies();
    BackgroundTaskCoordinator::Dependencies makeBackgroundTaskDependencies();

    HeartbeatDispatchCoordinator::Dependencies makeHeartbeatDispatchDependencies(
        HeartbeatRuntimeState& runtimeState,
        bool& shouldPersistState,
        const QDateTime& nowUtc);

private:
    ChatService& m_service;
};

#endif // CHATCOORDINATORFACTORY_H
