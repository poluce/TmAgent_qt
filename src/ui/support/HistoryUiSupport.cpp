#include "HistoryUiSupport.h"

#include "core/model/Identity.h"
#include <QDateTime>
#include <QHash>

namespace {

QString teammateReplyDisplayText(const Message& msg)
{
    const QString teammateName =
        msg.content.payload.value(QStringLiteral("teammate_name")).toString().trimmed();
    const QString status =
        msg.content.payload.value(QStringLiteral("status")).toString().trimmed();
    QString raw =
        msg.content.payload.value(QStringLiteral("raw_content")).toString();
    if (raw.trimmed().isEmpty())
        raw = msg.content.text;

    QStringList lines;
    lines << QObject::tr("队友回复%1%2")
                 .arg(teammateName.isEmpty() ? QString() : QStringLiteral(" · "))
                 .arg(teammateName);
    if (!status.isEmpty())
        lines << QObject::tr("状态：%1").arg(status);
    if (!raw.trimmed().isEmpty())
        lines << raw;
    return lines.join(QStringLiteral("\n"));
}

QList<Message> filterVisibleSessionMessages(const QList<Message>& allMessages, bool filterHeartbeatMessages, int* filteredHeartbeatCount)
{
    if (filteredHeartbeatCount)
        *filteredHeartbeatCount = 0;
    Q_UNUSED(filterHeartbeatMessages);

    QList<Message> visibleMessages;
    visibleMessages.reserve(allMessages.size());
    for (const Message& msg : allMessages) {
        if (!msg.visibleInChat)
            continue;
        visibleMessages.append(msg);
    }
    return visibleMessages;
}

} // namespace

namespace HistoryUiSupport {

QList<ChatWidget::HistoryMessage> buildSessionHistoryMessages(const QList<Message>& allMessages,
                                                              const SessionRestoreOptions& options)
{
    int filteredHeartbeatCount = 0;
    const QList<Message> visibleMessages =
        filterVisibleSessionMessages(allMessages, options.filterHeartbeatMessages, &filteredHeartbeatCount);

    QList<Message> messages = visibleMessages;
    if (options.maxRestoreMessages > 0 && visibleMessages.size() > options.maxRestoreMessages)
        messages = visibleMessages.mid(visibleMessages.size() - options.maxRestoreMessages);

    QList<ChatWidget::HistoryMessage> historyMessages;
    historyMessages.reserve(messages.size() + 1);

    const bool shouldShowRestoreNotice =
        options.maxRestoreMessages > 0
        && (visibleMessages.size() > options.maxRestoreMessages || filteredHeartbeatCount > 0);
    if (shouldShowRestoreNotice) {
        ChatWidget::HistoryMessage notice;
        notice.messageType = ChatWidgetMessage::MessageType::System;
        notice.senderId = QStringLiteral("system");
        notice.displayName = QStringLiteral("System");
        notice.timestamp = QDateTime::currentDateTime();
        if (filteredHeartbeatCount > 0) {
            notice.content = QObject::tr("（仅显示最近 %1 条可见消息，共 %2 条；已过滤心跳消息 %3 条）")
                                 .arg(options.maxRestoreMessages)
                                 .arg(allMessages.size())
                                 .arg(filteredHeartbeatCount);
        } else {
            notice.content = QObject::tr("（仅显示最近 %1 条消息，共 %2 条）")
                                 .arg(options.maxRestoreMessages)
                                 .arg(visibleMessages.size());
        }
        historyMessages.append(notice);
    }

    QHash<QString, Identity*> senderIdentityCache;
    senderIdentityCache.reserve(qMax(8, messages.size()));

    for (const Message& msg : messages) {
        if (msg.content.type == MessageContent::Type::ToolCall
            || msg.content.type == MessageContent::Type::ToolResult) {
            continue;
        }
        if (msg.content.type != MessageContent::Type::File && msg.content.text.trimmed().isEmpty())
            continue;

        ChatWidget::HistoryMessage historyMsg;
        historyMsg.messageId = msg.id;
        historyMsg.timestamp = msg.timestamp.isValid() ? msg.timestamp : QDateTime::currentDateTime();

        if (msg.content.type == MessageContent::Type::TeammateReply) {
            historyMsg.messageType = ChatWidgetMessage::MessageType::System;
            historyMsg.senderId = QStringLiteral("system");
            historyMsg.displayName = QStringLiteral("System");
            historyMsg.content = teammateReplyDisplayText(msg);
            historyMessages.append(historyMsg);
            continue;
        }

        if (msg.content.type == MessageContent::Type::System || msg.senderId == QLatin1String("system")) {
            historyMsg.messageType = ChatWidgetMessage::MessageType::System;
            historyMsg.senderId = QStringLiteral("system");
            historyMsg.displayName = QStringLiteral("System");
        } else {
            Identity* senderIdentity = senderIdentityCache.value(msg.senderId, nullptr);
            if (!senderIdentityCache.contains(msg.senderId)) {
                senderIdentity = options.identityResolver ? options.identityResolver(msg.senderId) : nullptr;
                senderIdentityCache.insert(msg.senderId, senderIdentity);
            }

            const bool isSelf = (!options.viewerIdentityId.isEmpty() && msg.senderId == options.viewerIdentityId);
            if (!senderIdentity || senderIdentity->isUser()) {
                historyMsg.senderId = options.userSenderId;
                historyMsg.displayName = options.userDisplayName;
                historyMsg.avatarPath = options.userAvatarPath;
                historyMsg.isMine = options.userMessagesAreMine;
            } else if (isSelf) {
                historyMsg.senderId = options.viewerIdentityId;
                historyMsg.displayName = senderIdentity->name().trimmed().isEmpty()
                    ? options.selfDisplayName
                    : senderIdentity->name();
                historyMsg.avatarPath = senderIdentity->avatar().trimmed();
                historyMsg.isMine = true;
            } else {
                historyMsg.senderId = senderIdentity->id();
                historyMsg.displayName = senderIdentity->name().trimmed().isEmpty()
                    ? options.defaultAssistantDisplayName
                    : senderIdentity->name().trimmed();
                historyMsg.avatarPath = senderIdentity->avatar().trimmed();
            }
        }

        if (msg.content.type == MessageContent::Type::File) {
            const QString filePath = msg.content.payload.value(QStringLiteral("file_path")).toString();
            const QString fileName = msg.content.payload.value(QStringLiteral("file_name")).toString();
            if (options.requireCompleteFilePayload && (filePath.isEmpty() || fileName.isEmpty()))
                continue;

            historyMsg.messageType = ChatWidgetMessage::MessageType::File;
            historyMsg.filePath = filePath;
            historyMsg.fileName = fileName;
            historyMsg.fileSize = static_cast<qint64>(msg.content.payload.value(QStringLiteral("file_size")).toDouble());
            historyMsg.content = msg.content.text.isEmpty() ? fileName : msg.content.text;
        } else {
            historyMsg.messageType = ChatWidgetMessage::MessageType::Text;
            historyMsg.content = msg.content.text;
        }

        historyMessages.append(historyMsg);
    }

    return historyMessages;
}

QList<ChatWidget::HistoryMessage> buildRawHistoryMessages(const QJsonArray& history,
                                                          const RawHistoryRestoreOptions& options)
{
    QList<ChatWidget::HistoryMessage> historyMessages;
    historyMessages.reserve(history.size());

    for (const QJsonValue& value : history) {
        const QJsonObject obj = value.toObject();
        const QString role = obj.value(QStringLiteral("role")).toString();
        const QString content = obj.value(QStringLiteral("content")).toString();
        if (content.isEmpty() || role == QLatin1String("tool"))
            continue;

        const bool isUser = (role == QLatin1String("user"));
        const bool isSystem = (role == QLatin1String("system"));
        ChatWidget::HistoryMessage msg;
        msg.messageId = obj.value(QStringLiteral("message_id")).toString().trimmed();
        msg.content = content;
        msg.timestamp = QDateTime::currentDateTime();
        msg.senderId = isUser ? options.userSenderId
                              : (isSystem ? QStringLiteral("system") : options.fallbackAssistantSenderId);
        msg.displayName = isUser ? options.userDisplayName
                                 : (isSystem ? QStringLiteral("System") : options.assistantDisplayName);
        msg.avatarPath = isUser ? options.userAvatarPath
                                : (isSystem ? QString() : options.assistantAvatarPath);
        msg.isMine = isUser && options.userMessagesAreMine;
        msg.messageType = isSystem ? ChatWidgetMessage::MessageType::System
                                   : ChatWidgetMessage::MessageType::Text;
        historyMessages.append(msg);
    }

    return historyMessages;
}

} // namespace HistoryUiSupport
