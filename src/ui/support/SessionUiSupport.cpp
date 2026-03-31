#include "SessionUiSupport.h"

#include "core/manager/SessionManager.h"
#include "core/model/Session.h"

namespace SessionUiSupport {

Session* activateSession(IWorkspaceService* sessionCommands, const QString& sessionId, QString* currentSessionId)
{
    const QString trimmedSessionId = sessionId.trimmed();
    if (!sessionCommands || trimmedSessionId.isEmpty())
        return nullptr;

    Session* session = SessionManager::instance()->findById(trimmedSessionId);
    if (!session)
        return nullptr;

    sessionCommands->switchSession(trimmedSessionId);
    if (currentSessionId)
        *currentSessionId = trimmedSessionId;
    return session;
}

Session* activateCreatedSession(IWorkspaceService* sessionCommands, Session* session, QString* currentSessionId)
{
    if (!session)
        return nullptr;
    return activateSession(sessionCommands, session->id(), currentSessionId);
}

bool renameSessionAndRuntime(IConversationService* viewCommands, const QString& sessionId, const QString& name)
{
    return viewCommands && viewCommands->renameSessionAndRuntime(sessionId, name);
}

RemoveSessionResult removeSession(IWorkspaceService* sessionCommands,
                                  const QString& sessionId,
                                  const QString& currentSessionId,
                                  const QString& actorIdentityId)
{
    const QString trimmedSessionId = sessionId.trimmed();
    if (!sessionCommands || trimmedSessionId.isEmpty())
        return RemoveSessionResult::Failed;

    if (actorIdentityId.trimmed().isEmpty())
        sessionCommands->removeSession(trimmedSessionId);
    else if (!sessionCommands->removeSessionAs(actorIdentityId.trimmed(), trimmedSessionId))
        return RemoveSessionResult::Failed;

    return (currentSessionId == trimmedSessionId)
        ? RemoveSessionResult::RemovedCurrent
        : RemoveSessionResult::RemovedInactive;
}

} // namespace SessionUiSupport
