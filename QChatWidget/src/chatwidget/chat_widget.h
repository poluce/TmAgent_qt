#ifndef CHAT_WIDGET_H
#define CHAT_WIDGET_H

#include "chat_widget_delegate.h"
#include "chat_widget_model.h"
#include "chat_widget_command.h"
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QWidget>

class ChatWidgetView;
class ChatWidgetInputBase;
class QTimer;

class ChatWidget : public QWidget {
    Q_OBJECT

public:
    struct MessageParams {
        QString messageId;
        QString content;
        QString senderId;
        QString displayName;
        QString avatarPath;
        bool isMine = false;
    };

    struct ParticipantInfo {
        QString id;
        QString displayName;
        QString avatarPath;
    };
    struct HistoryMessage {
        QString senderId;
        QString displayName;
        QString avatarPath;
        QString content;
        QDateTime timestamp;
        bool isMine = false;
        QString messageId;

        ChatWidgetMessage::MessageType messageType = ChatWidgetMessage::MessageType::Text;
        ChatWidgetMessage::MessageStatus status = ChatWidgetMessage::MessageStatus::Sent;

        QString imagePath;
        QString imageThumbnailPath;
        int imageWidth = 0;
        int imageHeight = 0;

        QString filePath;
        QString fileName;
        qint64 fileSize = 0;

        QString voicePath;
        int voiceDuration = 0;

        QString replyToMessageId;
        QString replySender;
        QString replyPreview;
        bool isForwarded = false;
        QString forwardedFrom;

        QList<ChatWidgetReaction> reactions;
        QStringList mentions;
    };

    explicit ChatWidget(QWidget* parent = nullptr);
    ~ChatWidget();

    ChatWidgetView* view() const;
    ChatWidgetModel* model() const;

    // API: 添加消息
    void addMessage(const MessageParams& params);

    // API: 流式输出（追加内容到最后一条消息）
    void streamOutput(const QString& content);
    void setStreamTargetRow(int row);
    void clearStreamTargetRow();
    void updateMessageContentAtRow(int row, const QString& content);
    void removeMessageAt(int row);

    // API: 移除最后一条消息
    void removeLastMessage();
    void clearMessages();
    int messageCount() const;
    bool applyStyleSheetFile(const QString& fileNameOrPath);
    void setDelegateStyle(const ChatWidgetDelegate::Style& style);
    ChatWidgetDelegate::Style delegateStyle() const;
    void setInputWidget(class ChatWidgetInputBase* widget);
    class ChatWidgetInputBase* inputWidget() const;
    void setSendingState(bool sending);
    void setEmptyStateVisible(bool visible, const QString& message = QString());
    bool isEmptyStateVisible() const;
    QString currentUserId() const;
    void setCurrentUser(const QString& userId, const QString& displayName = QString(), const QString& avatarPath = QString());
    void upsertParticipant(const QString& userId, const QString& displayName, const QString& avatarPath = QString());
    bool removeParticipant(const QString& userId);
    void clearParticipants();
    bool hasParticipant(const QString& userId) const;
    void setHistoryMessages(const QList<HistoryMessage>& messages, bool resetParticipants = true);
    void appendHistoryMessages(const QList<HistoryMessage>& messages, bool sortAndDedupe = true);
    void prependHistoryMessages(const QList<HistoryMessage>& messages, bool sortAndDedupe = true);

    void updateMessageStatus(const QString& messageId, ChatWidgetMessage::MessageStatus status);
    void updateMessageContent(const QString& messageId, const QString& content);
    void updateMessageReactions(const QString& messageId, const QList<ChatWidgetReaction>& reactions);
    void updateMessageAttachments(const QString& messageId, const QString& imagePath, const QString& filePath,
                                  const QString& fileName, qint64 fileSize);
    void updateMessageReply(const QString& messageId, const QString& replyToMessageId, const QString& replySender,
                            const QString& replyPreview, bool isForwarded, const QString& forwardedFrom);
    void setSearchKeyword(const QString& keyword);

    // API: 命令系统
    ChatWidgetCommandRegistry* commandRegistry() const;
    void registerCommand(const ChatWidgetCommand& command);
    void unregisterCommand(const QString& name);

    // API: 图片/文件/语音消息
    void addFileMessage(const MessageParams& params, const QString& filePath,
                        const QString& fileName, qint64 fileSize);
    void addImageMessage(const MessageParams& params, const QString& imagePath,
                         int imageWidth = 0, int imageHeight = 0);
    void updateImageState(const QString& messageId, const QString& imagePath,
                          const QString& thumbnailPath, int width, int height,
                          ChatWidgetMessage::ImageLoadState state);

    // API: 语音消息
    void addVoiceMessage(const MessageParams& params, const QString& voicePath, int durationSeconds);
    void updateVoicePlayState(const QString& messageId,
                              ChatWidgetMessage::VoicePlayState state, int progress);

    // API: 模拟 AI 自动流式回复（组件内部管理定时器）
    void startSimulatedStreaming(const QString& content, int interval = 30);

signals:
    // API: 消息发送信号
    void messageSent(const QString& content);
    void avatarClicked(const QString& sender, bool isMine, int row);
    void selfAvatarClicked(const QString& senderId, int row);
    void memberAvatarClicked(const QString& senderId, const QString& displayName, int row);
    void stopRequested();
    void messageSelected(const QString& messageId);
    void messageContextMenuRequested(const QString& messageId, const QPoint& globalPos);
    void messageActionRequested(const QString& action, const QString& messageId, const QString& content);
    void commandExecuted(const QString& command, const QStringList& arguments, const QString& rawText);
    void imageSelected(const QStringList& paths);
    void imageClicked(const QString& messageId, const QString& imagePath);
    void voicePlayToggled(const QString& messageId, const QString& voicePath, bool play);

private slots:
    void onInputMessageSent(const QString& content);
    void onStreamingTimeout();

private:
    void setupUi();
    void addMessageToModel(const ChatWidgetMessage& msg);
    void flushPendingStreamBuffer(bool forceScroll = false);
    static void sortHistoryByTimestamp(QList<HistoryMessage>& messages);
    QList<ChatWidgetMessage> convertHistoryBatch(const QList<HistoryMessage>& sorted, bool dedupe,
                                                  QHash<QString, ParticipantInfo>* updatedParticipants = nullptr);
    void applyUpdatedParticipants(const QHash<QString, ParticipantInfo>& updatedParticipants);
    ChatWidgetMessage convertHistoryMessage(const HistoryMessage& history);
    void updateParticipantFromHistory(const HistoryMessage& history, QHash<QString, ParticipantInfo>* updatedParticipants = nullptr);

    class QVBoxLayout* m_mainLayout;
    class ChatWidgetView* m_viewWidget;
    class ChatWidgetInputBase* m_inputWidget;
    bool m_isSending = false;
    QHash<QString, ParticipantInfo> m_participants;
    QString m_currentUserId;
    QSet<QString> m_messageIds;

    QTimer* m_streamingTimer = nullptr;
    QString m_streamingContent;
    int m_streamingIndex = 0;
    int m_streamTargetRow = -1;
    QString m_pendingStreamBuffer;
    bool m_streamFlushQueued = false;
    qint64 m_lastStreamFlushMs = 0;
    qint64 m_lastScrollMs = 0;
};

#endif // CHAT_WIDGET_H
