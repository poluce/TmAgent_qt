#ifndef SESSIONUISUPPORT_H
#define SESSIONUISUPPORT_H

#include "ChatCapabilityInterfaces.h"
#include <QString>

class Session;

namespace SessionUiSupport {

enum class RemoveSessionResult {
    Failed,
    RemovedInactive,
    RemovedCurrent
};

Session* activateSession(ISessionCommands* sessionCommands, const QString& sessionId, QString* currentSessionId = nullptr);
Session* activateCreatedSession(ISessionCommands* sessionCommands, Session* session, QString* currentSessionId = nullptr);
bool renameSessionAndRuntime(IConversationViewCommands* viewCommands, const QString& sessionId, const QString& name);
RemoveSessionResult removeSession(ISessionCommands* sessionCommands,
                                  const QString& sessionId,
                                  const QString& currentSessionId = QString(),
                                  const QString& actorIdentityId = QString());

} // namespace SessionUiSupport

#endif // SESSIONUISUPPORT_H

