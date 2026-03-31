#ifndef HISTORYUISUPPORT_H
#define HISTORYUISUPPORT_H

#include "core/model/Message.h"
#include "chat_widget.h"
#include <QJsonArray>
#include <QList>
#include <functional>

class Identity;

namespace HistoryUiSupport {

struct SessionRestoreOptions {
    QString viewerIdentityId;
    QString userSenderId = QStringLiteral("user");
    QString userDisplayName = QStringLiteral("用户");
    QString userAvatarPath;
    QString selfDisplayName = QStringLiteral("Me");
    QString defaultAssistantDisplayName = QStringLiteral("Agent");
    bool userMessagesAreMine = true;
    bool filterHeartbeatMessages = false;
    bool requireCompleteFilePayload = false;
    int maxRestoreMessages = 0;
    std::function<Identity*(const QString&)> identityResolver;
};

struct RawHistoryRestoreOptions {
    QString fallbackAssistantSenderId = QStringLiteral("assistant");
    QString assistantDisplayName = QStringLiteral("Assistant");
    QString assistantAvatarPath;
    QString userSenderId = QStringLiteral("user");
    QString userDisplayName = QStringLiteral("用户");
    QString userAvatarPath;
    bool userMessagesAreMine = true;
};

QList<ChatWidget::HistoryMessage> buildSessionHistoryMessages(const QList<Message>& allMessages,
                                                              const SessionRestoreOptions& options);
QList<ChatWidget::HistoryMessage> buildRawHistoryMessages(const QJsonArray& history,
                                                          const RawHistoryRestoreOptions& options);

} // namespace HistoryUiSupport

#endif // HISTORYUISUPPORT_H

