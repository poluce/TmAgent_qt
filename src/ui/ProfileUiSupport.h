#ifndef PROFILEUISUPPORT_H
#define PROFILEUISUPPORT_H

#include <QString>

class ChatService;
class ProfileWidget;
class QWidget;
class Identity;
struct LLMConfig;

namespace ProfileUiSupport {

struct AgentProfileInfo {
    QString roleName;
    QString modelInfo;
};

ProfileWidget* createProfilePopup(QWidget* parent);
void setProfileAvatarIfAvailable(ProfileWidget* profile, const QString& avatarPath);
QString resolveModelInfo(ChatService* chatService, const LLMConfig& cfg);
AgentProfileInfo resolveAgentProfileInfo(ChatService* chatService,
                                         Identity* agentIdentity,
                                         const QString& sessionId);
void populateUserProfile(ProfileWidget* profile,
                         const QString& userName,
                         const QString& tmId,
                         const QString& avatarPath = QString());
void populateAgentProfile(ProfileWidget* profile,
                          const QString& userName,
                          const QString& tmId,
                          const QString& roleName,
                          const QString& modelInfo,
                          const QString& avatarPath = QString());
void attachSessionCopyAction(ProfileWidget* profile, const QString& sessionId);
void showProfilePopup(ProfileWidget* profile);

} // namespace ProfileUiSupport

#endif // PROFILEUISUPPORT_H
