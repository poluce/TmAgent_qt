#ifndef SESSIONUISUPPORT_H
#define SESSIONUISUPPORT_H

#include "AppFacade.h"
#include <QString>

class Session;

namespace SessionUiSupport {

enum class RemoveSessionResult {
    Failed,
    RemovedInactive,
    RemovedCurrent
};

Session* activateSession(IWorkspaceService* sessionCommands, const QString& sessionId, QString* currentSessionId = nullptr);
Session* activateCreatedSession(IWorkspaceService* sessionCommands, Session* session, QString* currentSessionId = nullptr);
bool renameSessionAndRuntime(IConversationService* viewCommands, const QString& sessionId, const QString& name);
RemoveSessionResult removeSession(IWorkspaceService* sessionCommands,
                                  const QString& sessionId,
                                  const QString& currentSessionId = QString(),
                                  const QString& actorIdentityId = QString());

} // namespace SessionUiSupport

#endif // SESSIONUISUPPORT_H

