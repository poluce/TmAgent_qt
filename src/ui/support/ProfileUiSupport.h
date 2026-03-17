#ifndef PROFILEUISUPPORT_H
#define PROFILEUISUPPORT_H

#include "ChatCapabilityInterfaces.h"
#include <QString>

class ProfileWidget;
class QWidget;
class Identity;

namespace ProfileUiSupport {

struct AgentProfileInfo {
    QString roleName;
    QString modelInfo;
};

ProfileWidget* createProfilePopup(QWidget* parent);
void setProfileAvatarIfAvailable(ProfileWidget* profile, const QString& avatarPath);
QString resolveModelInfo(const IConversationViewQueries* viewQueries, const LLMConfig& cfg);
AgentProfileInfo resolveAgentProfileInfo(const IConversationViewQueries* viewQueries,
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

