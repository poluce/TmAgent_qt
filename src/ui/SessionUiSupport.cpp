#include "SessionUiSupport.h"

#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/Session.h"
#include "core/service/AgentRuntime.h"
#include "core/service/ChatService.h"
#include "llm/LLMTypes.h"

namespace SessionUiSupport {

Session* activateSession(ChatService* chatService, const QString& sessionId, QString* currentSessionId)
{
    const QString trimmedSessionId = sessionId.trimmed();
    if (!chatService || trimmedSessionId.isEmpty())
        return nullptr;

    Session* session = SessionManager::instance()->findById(trimmedSessionId);
    if (!session)
        return nullptr;

    chatService->switchSession(trimmedSessionId);
    if (currentSessionId)
        *currentSessionId = trimmedSessionId;
    return session;
}

Session* activateCreatedSession(ChatService* chatService, Session* session, QString* currentSessionId)
{
    if (!session)
        return nullptr;
    return activateSession(chatService, session->id(), currentSessionId);
}

bool renameSessionAndRuntime(ChatService* chatService, const QString& sessionId, const QString& name)
{
    const QString trimmedSessionId = sessionId.trimmed();
    const QString trimmedName = name.trimmed();
    if (!chatService || trimmedSessionId.isEmpty())
        return false;

    Session* session = SessionManager::instance()->findById(trimmedSessionId);
    if (session)
        session->setTitle(trimmedName);

    AgentRuntime* runtime = chatService->runtimeForSession(trimmedSessionId);
    if (runtime && runtime->identity()) {
        runtime->identity()->setName(trimmedName);
        LLMConfig cfg = runtime->config();
        cfg.userName = trimmedName;
        runtime->setConfig(cfg);
    }

    return true;
}

RemoveSessionResult removeSession(ChatService* chatService,
                                  const QString& sessionId,
                                  const QString& currentSessionId,
                                  const QString& actorIdentityId)
{
    const QString trimmedSessionId = sessionId.trimmed();
    if (!chatService || trimmedSessionId.isEmpty())
        return RemoveSessionResult::Failed;

    if (actorIdentityId.trimmed().isEmpty())
        chatService->removeSession(trimmedSessionId);
    else if (!chatService->removeSessionAs(actorIdentityId.trimmed(), trimmedSessionId))
        return RemoveSessionResult::Failed;

    return (currentSessionId == trimmedSessionId)
        ? RemoveSessionResult::RemovedCurrent
        : RemoveSessionResult::RemovedInactive;
}

} // namespace SessionUiSupport
