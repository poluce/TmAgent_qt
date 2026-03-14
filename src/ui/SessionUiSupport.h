#ifndef SESSIONUISUPPORT_H
#define SESSIONUISUPPORT_H

#include <QString>

class ChatService;
class Session;

namespace SessionUiSupport {

enum class RemoveSessionResult {
    Failed,
    RemovedInactive,
    RemovedCurrent
};

Session* activateSession(ChatService* chatService, const QString& sessionId, QString* currentSessionId = nullptr);
Session* activateCreatedSession(ChatService* chatService, Session* session, QString* currentSessionId = nullptr);
bool renameSessionAndRuntime(ChatService* chatService, const QString& sessionId, const QString& name);
RemoveSessionResult removeSession(ChatService* chatService,
                                  const QString& sessionId,
                                  const QString& currentSessionId = QString(),
                                  const QString& actorIdentityId = QString());

} // namespace SessionUiSupport

#endif // SESSIONUISUPPORT_H
