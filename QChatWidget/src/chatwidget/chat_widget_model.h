#ifndef CHAT_WIDGET_MODEL_H
#define CHAT_WIDGET_MODEL_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QString>
#include <QtGlobal>
#include <QVariant>
#include <QSet>

struct ChatWidgetReaction {
    QString emoji;
    int count = 0;

    ChatWidgetReaction() = default;
    ChatWidgetReaction(const QString& emojiValue, int countValue)
        : emoji(emojiValue)
        , count(countValue)
    {
    }
};

inline bool operator==(const ChatWidgetReaction& lhs, const ChatWidgetReaction& rhs)
{
    return lhs.emoji == rhs.emoji && lhs.count == rhs.count;
}

inline bool operator!=(const ChatWidgetReaction& lhs, const ChatWidgetReaction& rhs)
{
    return !(lhs == rhs);
}

struct ChatWidgetMessage {
    enum class MessageType {
        Text,
        Image,
        File,
        Voice,
        System,
        DateSeparator
    };

    enum class MessageStatus {
        Sending,
        Sent,
        Failed,
        Read
    };

    QString senderId;    // 发送者ID（多人聊天用）
    QString sender;      // 发送者姓名
    QString content;     // 消息内容
    QString avatarPath;  // 头像路径
    QDateTime timestamp; // 时间戳
    bool isMine = false; // 是否是我发的消息
    QString messageId;   // 消息唯一ID

    MessageType messageType = MessageType::Text;
    MessageStatus status = MessageStatus::Sent;

    QString imagePath;
    QString imageThumbnailPath;
    int imageWidth = 0;
    int imageHeight = 0;
    enum class ImageLoadState { None, Loading, Loaded, Failed };
    ImageLoadState imageLoadState = ImageLoadState::None;

    QString filePath;
    QString fileName;
    qint64 fileSize = 0;

    QString voicePath;
    int voiceDuration = 0;
    enum class VoicePlayState { Stopped, Playing, Paused };
    VoicePlayState voicePlayState = VoicePlayState::Stopped;
    int voicePlayProgress = 0;

    QString replyToMessageId;
    QString replySender;
    QString replyPreview;
    bool isForwarded = false;
    QString forwardedFrom;

    QList<ChatWidgetReaction> reactions;
    QStringList mentions;
};

class ChatWidgetModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum ChatWidgetRoles {
        ChatWidgetSenderRole = Qt::UserRole + 1,
        ChatWidgetContentRole,
        ChatWidgetAvatarRole,
        ChatWidgetTimestampRole,
        ChatWidgetIsMineRole,
        ChatWidgetSenderIdRole,
        ChatWidgetMessageIdRole,
        ChatWidgetMessageTypeRole,
        ChatWidgetMessageStatusRole,
        ChatWidgetImagePathRole,
        ChatWidgetFilePathRole,
        ChatWidgetFileNameRole,
        ChatWidgetFileSizeRole,
        ChatWidgetReplyToMessageIdRole,
        ChatWidgetReplySenderRole,
        ChatWidgetReplyPreviewRole,
        ChatWidgetIsForwardedRole,
        ChatWidgetForwardedFromRole,
        ChatWidgetReactionsRole,
        ChatWidgetMentionsRole,
        ChatWidgetIsSystemRole,
        ChatWidgetSearchKeywordRole,
        ChatWidgetImageThumbnailRole,
        ChatWidgetImageWidthRole,
        ChatWidgetImageHeightRole,
        ChatWidgetImageLoadStateRole,
        ChatWidgetVoicePathRole,
        ChatWidgetVoiceDurationRole,
        ChatWidgetVoicePlayStateRole,
        ChatWidgetVoicePlayProgressRole
    };

    explicit ChatWidgetModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addMessage(const ChatWidgetMessage& message);
    void setMessages(const QList<ChatWidgetMessage>& messages);
    void appendMessages(const QList<ChatWidgetMessage>& messages);
    void prependMessages(const QList<ChatWidgetMessage>& messages);
    void updateIsMine(const QString& currentUserId);
    void updateParticipantInfo(const QString& senderId, const QString& displayName, const QString& avatarPath);
    void appendContentToLastMessage(const QString& content);
    void appendContentToMessageAt(int row, const QString& content);
    void updateMessageContentAt(int row, const QString& content);
    void updateMessageStatus(const QString& messageId, ChatWidgetMessage::MessageStatus status);
    void updateMessageContent(const QString& messageId, const QString& content);
    void updateMessageReactions(const QString& messageId, const QList<ChatWidgetReaction>& reactions);
    void updateMessageAttachments(const QString& messageId, const QString& imagePath, const QString& filePath,
                                  const QString& fileName, qint64 fileSize);
    void updateMessageReply(const QString& messageId, const QString& replyToMessageId, const QString& replySender,
                            const QString& replyPreview, bool isForwarded, const QString& forwardedFrom);
    void setSearchKeyword(const QString& keyword);
    void updateImageState(const QString& messageId, const QString& imagePath,
                          const QString& thumbnailPath, int width, int height,
                          ChatWidgetMessage::ImageLoadState state);
    void updateVoicePlayState(const QString& messageId,
                              ChatWidgetMessage::VoicePlayState state, int progress);
    void removeMessageAt(int row);
    void removeLastMessage();
    void clearMessages();
    int messageCount() const;

private:
    QList<ChatWidgetMessage> filterNewMessages(const QList<ChatWidgetMessage>& messages);

    QList<ChatWidgetMessage> m_messages;
    QString m_searchKeyword;
    QSet<QString> m_messageIds;
};

#endif // CHAT_WIDGET_MODEL_H
