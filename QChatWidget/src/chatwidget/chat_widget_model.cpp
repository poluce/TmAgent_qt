#include "chat_widget_model.h"
#include <algorithm>

namespace {
bool isSystemMessage(ChatWidgetMessage::MessageType type)
{
    return type == ChatWidgetMessage::MessageType::System ||
           type == ChatWidgetMessage::MessageType::DateSeparator;
}

qint64 messageTimestampKey(const ChatWidgetMessage& message)
{
    return message.timestamp.isValid() ? message.timestamp.toMSecsSinceEpoch() : 0;
}
} // namespace

ChatWidgetModel::ChatWidgetModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ChatWidgetModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_messages.count();
}

QVariant ChatWidgetModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.count())
        return QVariant();

    const ChatWidgetMessage& msg = m_messages[index.row()];

    switch (role) {
    case ChatWidgetSenderRole:
        return msg.sender;
    case ChatWidgetContentRole:
        return msg.content;
    case ChatWidgetAvatarRole:
        return msg.avatarPath;
    case ChatWidgetTimestampRole:
        return msg.timestamp;
    case ChatWidgetIsMineRole:
        return msg.isMine;
    case ChatWidgetSenderIdRole:
        return msg.senderId;
    case ChatWidgetMessageIdRole:
        return msg.messageId;
    case ChatWidgetMessageTypeRole:
        return static_cast<int>(msg.messageType);
    case ChatWidgetMessageStatusRole:
        return static_cast<int>(msg.status);
    case ChatWidgetImagePathRole:
        return msg.imagePath;
    case ChatWidgetFilePathRole:
        return msg.filePath;
    case ChatWidgetFileNameRole:
        return msg.fileName;
    case ChatWidgetFileSizeRole:
        return msg.fileSize;
    case ChatWidgetReplyToMessageIdRole:
        return msg.replyToMessageId;
    case ChatWidgetReplySenderRole:
        return msg.replySender;
    case ChatWidgetReplyPreviewRole:
        return msg.replyPreview;
    case ChatWidgetIsForwardedRole:
        return msg.isForwarded;
    case ChatWidgetForwardedFromRole:
        return msg.forwardedFrom;
    case ChatWidgetReactionsRole: {
        QVariantList reactionList;
        reactionList.reserve(msg.reactions.size());
        for (const ChatWidgetReaction& reaction : msg.reactions) {
            QVariantMap map;
            map.insert("emoji", reaction.emoji);
            map.insert("count", reaction.count);
            reactionList.append(map);
        }
        return reactionList;
    }
    case ChatWidgetMentionsRole:
        return msg.mentions;
    case ChatWidgetIsSystemRole:
        return isSystemMessage(msg.messageType);
    case ChatWidgetSearchKeywordRole:
        return m_searchKeyword;
    case ChatWidgetImageThumbnailRole:
        return msg.imageThumbnailPath;
    case ChatWidgetImageWidthRole:
        return msg.imageWidth;
    case ChatWidgetImageHeightRole:
        return msg.imageHeight;
    case ChatWidgetImageLoadStateRole:
        return static_cast<int>(msg.imageLoadState);
    case ChatWidgetVoicePathRole:
        return msg.voicePath;
    case ChatWidgetVoiceDurationRole:
        return msg.voiceDuration;
    case ChatWidgetVoicePlayStateRole:
        return static_cast<int>(msg.voicePlayState);
    case ChatWidgetVoicePlayProgressRole:
        return msg.voicePlayProgress;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ChatWidgetModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[ChatWidgetSenderRole] = "sender";
    roles[ChatWidgetContentRole] = "content";
    roles[ChatWidgetAvatarRole] = "avatar";
    roles[ChatWidgetTimestampRole] = "timestamp";
    roles[ChatWidgetIsMineRole] = "isMine";
    roles[ChatWidgetSenderIdRole] = "senderId";
    roles[ChatWidgetMessageIdRole] = "messageId";
    roles[ChatWidgetMessageTypeRole] = "messageType";
    roles[ChatWidgetMessageStatusRole] = "messageStatus";
    roles[ChatWidgetImagePathRole] = "imagePath";
    roles[ChatWidgetFilePathRole] = "filePath";
    roles[ChatWidgetFileNameRole] = "fileName";
    roles[ChatWidgetFileSizeRole] = "fileSize";
    roles[ChatWidgetReplyToMessageIdRole] = "replyToMessageId";
    roles[ChatWidgetReplySenderRole] = "replySender";
    roles[ChatWidgetReplyPreviewRole] = "replyPreview";
    roles[ChatWidgetIsForwardedRole] = "isForwarded";
    roles[ChatWidgetForwardedFromRole] = "forwardedFrom";
    roles[ChatWidgetReactionsRole] = "reactions";
    roles[ChatWidgetMentionsRole] = "mentions";
    roles[ChatWidgetIsSystemRole] = "isSystem";
    roles[ChatWidgetSearchKeywordRole] = "searchKeyword";
    roles[ChatWidgetImageThumbnailRole] = "imageThumbnail";
    roles[ChatWidgetImageWidthRole] = "imageWidth";
    roles[ChatWidgetImageHeightRole] = "imageHeight";
    roles[ChatWidgetImageLoadStateRole] = "imageLoadState";
    roles[ChatWidgetVoicePathRole] = "voicePath";
    roles[ChatWidgetVoiceDurationRole] = "voiceDuration";
    roles[ChatWidgetVoicePlayStateRole] = "voicePlayState";
    roles[ChatWidgetVoicePlayProgressRole] = "voicePlayProgress";
    return roles;
}

void ChatWidgetModel::addMessage(const ChatWidgetMessage& message)
{
    beginInsertRows(QModelIndex(), m_messages.count(), m_messages.count());
    m_messages.append(message);
    if (!message.messageId.isEmpty()) {
        m_messageIds.insert(message.messageId);
    }
    endInsertRows();
}

void ChatWidgetModel::setMessages(const QList<ChatWidgetMessage>& messages)
{
    beginResetModel();
    m_messages.clear();
    m_messageIds.clear();

    QList<ChatWidgetMessage> sorted = messages;
    std::sort(sorted.begin(), sorted.end(), [](const ChatWidgetMessage& a, const ChatWidgetMessage& b) {
        return messageTimestampKey(a) < messageTimestampKey(b);
    });

    for (const ChatWidgetMessage& message : sorted) {
        if (!message.messageId.isEmpty() && m_messageIds.contains(message.messageId))
            continue;
        if (!message.messageId.isEmpty())
            m_messageIds.insert(message.messageId);
        m_messages.append(message);
    }
    endResetModel();
}

QList<ChatWidgetMessage> ChatWidgetModel::filterNewMessages(const QList<ChatWidgetMessage>& messages)
{
    QList<ChatWidgetMessage> filtered;
    if (messages.isEmpty())
        return filtered;

    filtered.reserve(messages.size());
    for (const ChatWidgetMessage& message : messages) {
        if (!message.messageId.isEmpty() && m_messageIds.contains(message.messageId))
            continue;
        if (!message.messageId.isEmpty())
            m_messageIds.insert(message.messageId);
        filtered.append(message);
    }
    return filtered;
}

void ChatWidgetModel::appendMessages(const QList<ChatWidgetMessage>& messages)
{
    const QList<ChatWidgetMessage> filtered = filterNewMessages(messages);
    if (filtered.isEmpty())
        return;

    const int start = m_messages.count();
    beginInsertRows(QModelIndex(), start, start + filtered.count() - 1);
    m_messages.append(filtered);
    endInsertRows();
}

void ChatWidgetModel::prependMessages(const QList<ChatWidgetMessage>& messages)
{
    const QList<ChatWidgetMessage> filtered = filterNewMessages(messages);
    if (filtered.isEmpty())
        return;

    beginInsertRows(QModelIndex(), 0, filtered.count() - 1);
    QList<ChatWidgetMessage> combined = filtered;
    combined.append(m_messages);
    m_messages.swap(combined);
    endInsertRows();
}

void ChatWidgetModel::updateIsMine(const QString& currentUserId)
{
    if (currentUserId.trimmed().isEmpty() || m_messages.isEmpty()) {
        return;
    }
    bool anyChanged = false;
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].senderId.isEmpty()) {
            continue;
        }
        const bool newIsMine = m_messages[i].senderId == currentUserId;
        if (m_messages[i].isMine != newIsMine) {
            m_messages[i].isMine = newIsMine;
            anyChanged = true;
        }
    }
    if (anyChanged) {
        QModelIndex first = index(0, 0);
        QModelIndex last = index(m_messages.size() - 1, 0);
        emit dataChanged(first, last, { ChatWidgetIsMineRole });
    }
}

void ChatWidgetModel::updateParticipantInfo(const QString& senderId, const QString& displayName, const QString& avatarPath)
{
    if (senderId.trimmed().isEmpty() || m_messages.isEmpty()) {
        return;
    }
    bool anyChanged = false;
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].senderId != senderId) {
            continue;
        }
        if (!displayName.isEmpty() && m_messages[i].sender != displayName) {
            m_messages[i].sender = displayName;
            anyChanged = true;
        }
        if (!avatarPath.isEmpty() && m_messages[i].avatarPath != avatarPath) {
            m_messages[i].avatarPath = avatarPath;
            anyChanged = true;
        }
    }
    if (anyChanged) {
        QModelIndex first = index(0, 0);
        QModelIndex last = index(m_messages.size() - 1, 0);
        emit dataChanged(first, last, { ChatWidgetSenderRole, ChatWidgetAvatarRole });
    }
}

void ChatWidgetModel::appendContentToLastMessage(const QString& content)
{
    if (m_messages.isEmpty())
        return;

    m_messages.last().content.append(content);
    QModelIndex idx = index(m_messages.count() - 1, 0);
    emit dataChanged(idx, idx, { ChatWidgetContentRole });
}

void ChatWidgetModel::appendContentToMessageAt(int row, const QString& content)
{
    if (row < 0 || row >= m_messages.size())
        return;

    m_messages[row].content.append(content);
    QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, { ChatWidgetContentRole });
}

void ChatWidgetModel::updateMessageContentAt(int row, const QString& content)
{
    if (row < 0 || row >= m_messages.size())
        return;
    if (m_messages[row].content == content)
        return;

    m_messages[row].content = content;
    QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, { ChatWidgetContentRole });
}

void ChatWidgetModel::updateMessageStatus(const QString& messageId, ChatWidgetMessage::MessageStatus status)
{
    if (messageId.trimmed().isEmpty()) {
        return;
    }
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].messageId != messageId) {
            continue;
        }
        if (m_messages[i].status == status) {
            return;
        }
        m_messages[i].status = status;
        QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, { ChatWidgetMessageStatusRole });
        return;
    }
}

void ChatWidgetModel::updateMessageContent(const QString& messageId, const QString& content)
{
    if (messageId.trimmed().isEmpty()) {
        return;
    }
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].messageId != messageId) {
            continue;
        }
        if (m_messages[i].content == content) {
            return;
        }
        m_messages[i].content = content;
        QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, { ChatWidgetContentRole });
        return;
    }
}

void ChatWidgetModel::updateMessageReactions(const QString& messageId, const QList<ChatWidgetReaction>& reactions)
{
    if (messageId.trimmed().isEmpty()) {
        return;
    }
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].messageId != messageId) {
            continue;
        }
        if (m_messages[i].reactions == reactions) {
            return;
        }
        m_messages[i].reactions = reactions;
        QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, { ChatWidgetReactionsRole });
        return;
    }
}

void ChatWidgetModel::updateMessageAttachments(const QString& messageId, const QString& imagePath, const QString& filePath,
                                               const QString& fileName, qint64 fileSize)
{
    if (messageId.trimmed().isEmpty()) {
        return;
    }
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].messageId != messageId) {
            continue;
        }
        bool changed = false;
        if (m_messages[i].imagePath != imagePath) {
            m_messages[i].imagePath = imagePath;
            changed = true;
        }
        if (m_messages[i].filePath != filePath) {
            m_messages[i].filePath = filePath;
            changed = true;
        }
        if (m_messages[i].fileName != fileName) {
            m_messages[i].fileName = fileName;
            changed = true;
        }
        if (m_messages[i].fileSize != fileSize) {
            m_messages[i].fileSize = fileSize;
            changed = true;
        }
        if (!changed) {
            return;
        }
        QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, { ChatWidgetImagePathRole, ChatWidgetFilePathRole, ChatWidgetFileNameRole, ChatWidgetFileSizeRole });
        return;
    }
}

void ChatWidgetModel::updateMessageReply(const QString& messageId, const QString& replyToMessageId, const QString& replySender,
                                         const QString& replyPreview, bool isForwarded, const QString& forwardedFrom)
{
    if (messageId.trimmed().isEmpty()) {
        return;
    }
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].messageId != messageId) {
            continue;
        }
        bool changed = false;
        if (m_messages[i].replyToMessageId != replyToMessageId) {
            m_messages[i].replyToMessageId = replyToMessageId;
            changed = true;
        }
        if (m_messages[i].replySender != replySender) {
            m_messages[i].replySender = replySender;
            changed = true;
        }
        if (m_messages[i].replyPreview != replyPreview) {
            m_messages[i].replyPreview = replyPreview;
            changed = true;
        }
        if (m_messages[i].isForwarded != isForwarded) {
            m_messages[i].isForwarded = isForwarded;
            changed = true;
        }
        if (m_messages[i].forwardedFrom != forwardedFrom) {
            m_messages[i].forwardedFrom = forwardedFrom;
            changed = true;
        }
        if (!changed) {
            return;
        }
        QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, { ChatWidgetReplyToMessageIdRole, ChatWidgetReplySenderRole, ChatWidgetReplyPreviewRole,
                                     ChatWidgetIsForwardedRole, ChatWidgetForwardedFromRole });
        return;
    }
}

void ChatWidgetModel::setSearchKeyword(const QString& keyword)
{
    if (m_searchKeyword == keyword) {
        return;
    }
    m_searchKeyword = keyword;
    if (m_messages.isEmpty()) {
        return;
    }
    QModelIndex first = index(0, 0);
    QModelIndex last = index(m_messages.size() - 1, 0);
    emit dataChanged(first, last, { ChatWidgetSearchKeywordRole });
}

void ChatWidgetModel::removeMessageAt(int row)
{
    if (row < 0 || row >= m_messages.size()) {
        return;
    }
    beginRemoveRows(QModelIndex(), row, row);
    const QString messageId = m_messages.at(row).messageId;
    m_messages.removeAt(row);
    if (!messageId.isEmpty()) {
        m_messageIds.remove(messageId);
    }
    endRemoveRows();
}

void ChatWidgetModel::removeLastMessage()
{
    removeMessageAt(m_messages.size() - 1);
}

void ChatWidgetModel::clearMessages()
{
    if (m_messages.isEmpty())
        return;
    beginResetModel();
    m_messages.clear();
    m_messageIds.clear();
    endResetModel();
}

int ChatWidgetModel::messageCount() const
{
    return m_messages.size();
}

void ChatWidgetModel::updateImageState(const QString& messageId, const QString& imagePath,
                                       const QString& thumbnailPath, int width, int height,
                                       ChatWidgetMessage::ImageLoadState state)
{
    if (messageId.trimmed().isEmpty())
        return;
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].messageId != messageId)
            continue;
        bool changed = false;
        if (m_messages[i].imagePath != imagePath) {
            m_messages[i].imagePath = imagePath;
            changed = true;
        }
        if (m_messages[i].imageThumbnailPath != thumbnailPath) {
            m_messages[i].imageThumbnailPath = thumbnailPath;
            changed = true;
        }
        if (m_messages[i].imageWidth != width) {
            m_messages[i].imageWidth = width;
            changed = true;
        }
        if (m_messages[i].imageHeight != height) {
            m_messages[i].imageHeight = height;
            changed = true;
        }
        if (m_messages[i].imageLoadState != state) {
            m_messages[i].imageLoadState = state;
            changed = true;
        }
        if (!changed)
            return;
        QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, { ChatWidgetImagePathRole, ChatWidgetImageThumbnailRole,
                                     ChatWidgetImageWidthRole, ChatWidgetImageHeightRole,
                                     ChatWidgetImageLoadStateRole });
        return;
    }
}

void ChatWidgetModel::updateVoicePlayState(const QString& messageId,
                                           ChatWidgetMessage::VoicePlayState state, int progress)
{
    if (messageId.trimmed().isEmpty())
        return;
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].messageId != messageId)
            continue;
        bool changed = false;
        if (m_messages[i].voicePlayState != state) {
            m_messages[i].voicePlayState = state;
            changed = true;
        }
        if (m_messages[i].voicePlayProgress != progress) {
            m_messages[i].voicePlayProgress = progress;
            changed = true;
        }
        if (!changed)
            return;
        QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, { ChatWidgetVoicePlayStateRole, ChatWidgetVoicePlayProgressRole });
        return;
    }
}
