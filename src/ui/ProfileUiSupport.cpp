#include "ProfileUiSupport.h"

#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/service/AgentRuntime.h"
#include "core/service/ChatService.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include "profile_widget.h"
#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QPixmap>
#include <QToolTip>

namespace ProfileUiSupport {

ProfileWidget* createProfilePopup(QWidget* parent)
{
    ProfileWidget* profile = new ProfileWidget(parent);
    profile->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    profile->setAttribute(Qt::WA_DeleteOnClose);
    profile->applyDefaultStyle();
    return profile;
}

void setProfileAvatarIfAvailable(ProfileWidget* profile, const QString& avatarPath)
{
    if (!profile || avatarPath.trimmed().isEmpty())
        return;
    QPixmap avatar(avatarPath);
    if (!avatar.isNull())
        profile->setAvatar(avatar);
}

QString resolveModelInfo(ChatService* chatService, const LLMConfig& cfg)
{
    if (!cfg.isValid())
        return QStringLiteral("默认模型");
    if (ModelFactory* factory = chatService ? chatService->modelFactory() : nullptr) {
        const QString modelInfo = factory->resolveModelId(cfg).trimmed();
        return modelInfo.isEmpty() ? QStringLiteral("未指定模型") : modelInfo;
    }
    const QString modelInfo = cfg.selectedModelId.trimmed();
    return modelInfo.isEmpty() ? QStringLiteral("未指定模型") : modelInfo;
}

AgentProfileInfo resolveAgentProfileInfo(ChatService* chatService,
                                         Identity* agentIdentity,
                                         const QString& sessionId)
{
    AgentProfileInfo info;
    info.roleName = QStringLiteral("智能对话");
    info.modelInfo = QStringLiteral("默认模型");

    LLMConfig cfg;
    if (agentIdentity && agentIdentity->profile()) {
        IdentityProfile* profile = agentIdentity->profile();
        const QString desc = profile->description().trimmed();
        if (!desc.isEmpty())
            info.roleName = desc;
        cfg = profile->llmConfig();
    } else if (chatService) {
        if (AgentRuntime* runtime = chatService->runtimeForSession(sessionId)) {
            cfg = runtime->config();
            Identity* runtimeIdentity = runtime->identity();
            if (runtimeIdentity && runtimeIdentity->profile()) {
                const QString desc = runtimeIdentity->profile()->description().trimmed();
                if (!desc.isEmpty())
                    info.roleName = desc;
            }
        }
    }

    info.modelInfo = resolveModelInfo(chatService, cfg);
    return info;
}

void populateUserProfile(ProfileWidget* profile,
                         const QString& userName,
                         const QString& tmId,
                         const QString& avatarPath)
{
    if (!profile)
        return;
    profile->setUserName(userName.isEmpty() ? QStringLiteral("我") : userName);
    profile->setTmId(tmId.isEmpty() ? QStringLiteral("user") : tmId);
    setProfileAvatarIfAvailable(profile, avatarPath);
    profile->addDetailItem(QStringLiteral("角色"), QStringLiteral("用户"));
}

void populateAgentProfile(ProfileWidget* profile,
                          const QString& userName,
                          const QString& tmId,
                          const QString& roleName,
                          const QString& modelInfo,
                          const QString& avatarPath)
{
    if (!profile)
        return;
    profile->setUserName(userName.isEmpty() ? QStringLiteral("Agent") : userName);
    profile->setTmId(tmId.isEmpty() ? QStringLiteral("agent") : tmId);
    setProfileAvatarIfAvailable(profile, avatarPath);
    profile->addDetailItem(QStringLiteral("角色"), QStringLiteral("AI 助手"));
    profile->addSeparator();
    profile->addDetailItem(QStringLiteral("岗位"), roleName.isEmpty() ? QStringLiteral("智能对话") : roleName);
    profile->addSeparator();
    profile->addDetailItem(QStringLiteral("模型"), modelInfo.isEmpty() ? QStringLiteral("默认模型") : modelInfo);
}

void attachSessionCopyAction(ProfileWidget* profile, const QString& sessionId)
{
    if (!profile || sessionId.trimmed().isEmpty())
        return;

    profile->addSeparator();
    profile->addDetailItem(QStringLiteral("会话ID"), sessionId);
    profile->addDetailItem(QStringLiteral("复制"), QStringLiteral("点击复制会话ID"), true);
    QObject::connect(profile, &ProfileWidget::detailItemClicked, profile, [sessionId](const QString& title) {
        if (title != QStringLiteral("复制"))
            return;
        if (QClipboard* clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(sessionId, QClipboard::Clipboard);
            if (clipboard->supportsSelection())
                clipboard->setText(sessionId, QClipboard::Selection);
        }
        QToolTip::showText(QCursor::pos(), QStringLiteral("会话ID已复制"));
    });
}

void showProfilePopup(ProfileWidget* profile)
{
    if (!profile)
        return;
    const QPoint pos = QCursor::pos();
    profile->move(pos.x() - profile->width() / 2, pos.y() - 20);
    profile->show();
}

} // namespace ProfileUiSupport
