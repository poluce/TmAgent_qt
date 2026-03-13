#include "chat_widget.h"
#include "chat_widget_input.h"
#include "chat_widget_model.h"
#include "chat_widget_view.h"
#include "qss_utils.h"
#include <QDateTime>
#include <QTimer>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <algorithm>

ChatWidget::ChatWidget(QWidget* parent) : QWidget(parent)
{
    m_streamingTimer = new QTimer(this);
    connect(m_streamingTimer, &QTimer::timeout, this, &ChatWidget::onStreamingTimeout);
    setupUi();
}

ChatWidget::~ChatWidget() { }

ChatWidgetView* ChatWidget::view() const
{
    return m_viewWidget;
}

ChatWidgetModel* ChatWidget::model() const
{
    return m_viewWidget ? m_viewWidget->model() : nullptr;
}

void ChatWidget::setupUi()
{
    m_viewWidget = new ChatWidgetView(this);
    m_inputWidget = new ChatWidgetInput(this);
    m_viewWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_inputWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->addWidget(m_viewWidget, 1);
    m_mainLayout->addWidget(m_inputWidget, 0);

    connect(m_viewWidget, &ChatWidgetView::avatarClicked, this, &ChatWidget::avatarClicked);
    connect(m_viewWidget, &ChatWidgetView::selfAvatarClicked, this, &ChatWidget::selfAvatarClicked);
    connect(m_viewWidget, &ChatWidgetView::memberAvatarClicked, this, &ChatWidget::memberAvatarClicked);
    connect(m_viewWidget, &ChatWidgetView::messageSelected, this, &ChatWidget::messageSelected);
    connect(m_viewWidget, &ChatWidgetView::messageContextMenuRequested, this, &ChatWidget::messageContextMenuRequested);
    connect(m_viewWidget, &ChatWidgetView::messageActionRequested, this, &ChatWidget::messageActionRequested);
    connect(m_viewWidget, &ChatWidgetView::imageClicked, this, &ChatWidget::imageClicked);
    connect(m_viewWidget, &ChatWidgetView::voicePlayToggled, this, &ChatWidget::voicePlayToggled);
    connect(m_inputWidget, &ChatWidgetInputBase::messageSent, this, &ChatWidget::onInputMessageSent);
    connect(m_inputWidget, &ChatWidgetInputBase::stopRequested, this, [this]() {
        setSendingState(false);
        emit stopRequested();
    });

    // 命令和图片信号转发
    if (auto* input = qobject_cast<ChatWidgetInput*>(m_inputWidget)) {
        connect(input, &ChatWidgetInput::commandExecuted, this, &ChatWidget::commandExecuted);
        connect(input, &ChatWidgetInput::imageSelected, this, &ChatWidget::imageSelected);
    }
}

void ChatWidget::addMessageToModel(const ChatWidgetMessage& msg)
{
    if (auto* dataModel = model()) {
        dataModel->addMessage(msg);
    }
    if (m_viewWidget) {
        m_viewWidget->scrollToBottom();
    }
}

void ChatWidget::addMessage(const MessageParams& params)
{
    ChatWidgetMessage msg;
    msg.messageId = params.messageId;
    msg.content = params.content;
    msg.timestamp = QDateTime::currentDateTime();

    if (params.senderId.trimmed().isEmpty()) {
        msg.sender = params.displayName.isEmpty() ? QStringLiteral("User") : params.displayName;
        msg.avatarPath = params.avatarPath;
        msg.isMine = params.isMine;
        addMessageToModel(msg);
        return;
    }

    ParticipantInfo info = m_participants.value(params.senderId);
    info.id = params.senderId;
    if (!params.displayName.isEmpty()) {
        info.displayName = params.displayName;
    }
    if (!params.avatarPath.isEmpty()) {
        info.avatarPath = params.avatarPath;
    }
    m_participants.insert(params.senderId, info);

    msg.senderId = params.senderId;
    msg.sender = info.displayName.isEmpty() ? params.senderId : info.displayName;
    msg.avatarPath = info.avatarPath;
    msg.isMine = !m_currentUserId.isEmpty() ? (params.senderId == m_currentUserId) : params.isMine;
    addMessageToModel(msg);
}

void ChatWidget::streamOutput(const QString& content)
{
    // 物理拦截：如果当前不处于发送/接收状态，则拒绝任何字符追加
    if (!m_isSending) {
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    m_pendingStreamBuffer.append(content);

    const bool flushBySize = m_pendingStreamBuffer.size() >= 160;
    const bool flushByElapsed = (m_lastStreamFlushMs <= 0) || (nowMs - m_lastStreamFlushMs >= 120);
    const bool shouldFlushNow = flushBySize || flushByElapsed;
    if (shouldFlushNow) {
        flushPendingStreamBuffer();
    } else if (!m_streamFlushQueued) {
        m_streamFlushQueued = true;
        QTimer::singleShot(16, this, [this]() {
            m_streamFlushQueued = false;
            flushPendingStreamBuffer();
        });
    }
}

void ChatWidget::flushPendingStreamBuffer(bool forceScroll)
{
    if (m_pendingStreamBuffer.isEmpty())
        return;

    const QString batched = m_pendingStreamBuffer;
    m_pendingStreamBuffer.clear();

    if (auto* dataModel = model()) {
        int targetRow = m_streamTargetRow;
        if (targetRow < 0 || targetRow >= dataModel->messageCount())
            targetRow = dataModel->messageCount() - 1;
        dataModel->appendContentToMessageAt(targetRow, batched);
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool shouldScroll = forceScroll
        || (m_lastScrollMs <= 0)
        || (nowMs - m_lastScrollMs >= 350)
        || (batched.size() >= 240);
    if (m_viewWidget && shouldScroll) {
        m_viewWidget->scrollToBottom();
        m_lastScrollMs = QDateTime::currentMSecsSinceEpoch();
    }
    m_lastStreamFlushMs = QDateTime::currentMSecsSinceEpoch();
}

void ChatWidget::setStreamTargetRow(int row)
{
    m_streamTargetRow = row;
}

void ChatWidget::clearStreamTargetRow()
{
    m_streamTargetRow = -1;
}

void ChatWidget::updateMessageContentAtRow(int row, const QString& content)
{
    if (auto* dataModel = model()) {
        dataModel->updateMessageContentAt(row, content);
    }
    if (m_viewWidget) {
        m_viewWidget->refreshLayout();
    }
}

void ChatWidget::removeMessageAt(int row)
{
    if (row < 0)
        return;
    if (auto* dataModel = model()) {
        dataModel->removeMessageAt(row);
        if (m_streamTargetRow == row) {
            m_streamTargetRow = -1;
        } else if (m_streamTargetRow > row) {
            --m_streamTargetRow;
        }
    }
}

bool ChatWidget::applyStyleSheetFile(const QString& fileNameOrPath)
{
    if (fileNameOrPath.trimmed().isEmpty()) {
        return false;
    }
    return QssUtils::applyStyleSheetFromFile(this, fileNameOrPath);
}

void ChatWidget::setDelegateStyle(const ChatWidgetDelegate::Style& style)
{
    m_viewWidget->setDelegateStyle(style);
}

ChatWidgetDelegate::Style ChatWidget::delegateStyle() const
{
    return m_viewWidget->delegateStyle();
}

void ChatWidget::setInputWidget(ChatWidgetInputBase* widget)
{
    if (!widget || widget == m_inputWidget) {
        return;
    }

    if (m_inputWidget) {
        m_mainLayout->removeWidget(m_inputWidget);
        m_inputWidget->deleteLater();
    }

    widget->setParent(this);
    m_inputWidget = widget;
    m_inputWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_mainLayout->addWidget(m_inputWidget, 0);
    connect(m_inputWidget, &ChatWidgetInputBase::messageSent, this, &ChatWidget::onInputMessageSent);
    connect(m_inputWidget, &ChatWidgetInputBase::stopRequested, this, &ChatWidget::stopRequested);
}

ChatWidgetInputBase* ChatWidget::inputWidget() const { return m_inputWidget; }

void ChatWidget::onInputMessageSent(const QString& content)
{
    m_isSending = true; // 用户点击发送后，组件自动进入“发送/等待响应”状态
    MessageParams params;
    params.content = content;
    if (!m_currentUserId.isEmpty()) {
        params.senderId = m_currentUserId;
    } else {
        params.isMine = true;
        params.displayName = QStringLiteral("Me");
    }
    addMessage(params);
    emit messageSent(content);
}

void ChatWidget::removeLastMessage()
{
    if (auto* dataModel = model()) {
        if (m_streamTargetRow == dataModel->messageCount() - 1) {
            m_streamTargetRow = -1;
        }
        dataModel->removeLastMessage();
    }
}

void ChatWidget::clearMessages()
{
    if (auto* dataModel = model()) {
        dataModel->clearMessages();
    }
    m_messageIds.clear();
    m_streamTargetRow = -1;
    m_pendingStreamBuffer.clear();
    m_streamFlushQueued = false;
    m_lastStreamFlushMs = 0;
    m_lastScrollMs = 0;
}

int ChatWidget::messageCount() const
{
    return model() ? model()->messageCount() : 0;
}

void ChatWidget::setSendingState(bool sending)
{
    m_isSending = sending; // 更新组件全局状态锁
    if (auto* input = qobject_cast<ChatWidgetInput*>(m_inputWidget)) {
        input->setSendingState(sending);
    }

    // 核心逻辑：当停止发送时，物理停止内置模拟定时器
    if (!sending && m_streamingTimer->isActive()) {
        m_streamingTimer->stop();
    }
    if (!sending) {
        flushPendingStreamBuffer(true);
        m_streamFlushQueued = false;
    }
}

void ChatWidget::setEmptyStateVisible(bool visible, const QString& message)
{
    Q_UNUSED(message);
    if (m_viewWidget) {
        m_viewWidget->setVisible(!visible);
    }
    if (m_inputWidget) {
        m_inputWidget->setVisible(!visible);
        m_inputWidget->setEnabled(!visible);
    }
    if (visible) {
        setSendingState(false);
    }
}

bool ChatWidget::isEmptyStateVisible() const
{
    return m_viewWidget && !m_viewWidget->isVisible();
}

void ChatWidget::startSimulatedStreaming(const QString& content, int interval)
{
    m_streamingContent = content;
    m_streamingIndex = 0;
    // 先添加一个空消息作为容器
    MessageParams params;
    params.content = QString();
    params.displayName = QStringLiteral("AI");
    params.isMine = false;
    addMessage(params);
    setSendingState(true);
    m_streamingTimer->start(interval);
}

void ChatWidget::updateMessageStatus(const QString& messageId, ChatWidgetMessage::MessageStatus status)
{
    if (auto* dataModel = model()) {
        dataModel->updateMessageStatus(messageId, status);
    }
}

void ChatWidget::updateMessageContent(const QString& messageId, const QString& content)
{
    if (auto* dataModel = model()) {
        dataModel->updateMessageContent(messageId, content);
    }
    if (m_viewWidget) {
        m_viewWidget->refreshLayout();
    }
}

void ChatWidget::updateMessageReactions(const QString& messageId, const QList<ChatWidgetReaction>& reactions)
{
    if (auto* dataModel = model()) {
        dataModel->updateMessageReactions(messageId, reactions);
    }
    if (m_viewWidget) {
        m_viewWidget->refreshLayout();
    }
}

void ChatWidget::updateMessageAttachments(const QString& messageId, const QString& imagePath, const QString& filePath,
                                          const QString& fileName, qint64 fileSize)
{
    if (auto* dataModel = model()) {
        dataModel->updateMessageAttachments(messageId, imagePath, filePath, fileName, fileSize);
    }
    if (m_viewWidget) {
        m_viewWidget->refreshLayout();
    }
}

void ChatWidget::updateMessageReply(const QString& messageId, const QString& replyToMessageId, const QString& replySender,
                                    const QString& replyPreview, bool isForwarded, const QString& forwardedFrom)
{
    if (auto* dataModel = model()) {
        dataModel->updateMessageReply(messageId, replyToMessageId, replySender, replyPreview, isForwarded, forwardedFrom);
    }
    if (m_viewWidget) {
        m_viewWidget->refreshLayout();
    }
}

void ChatWidget::setSearchKeyword(const QString& keyword)
{
    if (auto* dataModel = model()) {
        dataModel->setSearchKeyword(keyword);
    }
}

QString ChatWidget::currentUserId() const
{
    return m_currentUserId;
}

void ChatWidget::setCurrentUser(const QString& userId, const QString& displayName, const QString& avatarPath)
{
    if (m_currentUserId == userId && displayName.isEmpty() && avatarPath.isEmpty()) {
        return;
    }
    if (!userId.trimmed().isEmpty()) {
        upsertParticipant(userId, displayName, avatarPath);
    }
    m_currentUserId = userId;
    if (auto* dataModel = model()) {
        dataModel->updateIsMine(m_currentUserId);
    }
    if (m_viewWidget) {
        m_viewWidget->refreshLayout();
    }
}

void ChatWidget::upsertParticipant(const QString& userId, const QString& displayName, const QString& avatarPath)
{
    ParticipantInfo info;
    info.id = userId;
    info.displayName = displayName;
    info.avatarPath = avatarPath;
    m_participants.insert(userId, info);
    if ((!displayName.isEmpty() || !avatarPath.isEmpty())) {
        const QString finalName = displayName.isEmpty() ? userId : displayName;
        if (auto* dataModel = model()) {
            dataModel->updateParticipantInfo(userId, finalName, avatarPath);
        }
        if (m_viewWidget) {
            m_viewWidget->refreshLayout();
        }
    }
}

bool ChatWidget::removeParticipant(const QString& userId)
{
    return m_participants.remove(userId) > 0;
}

void ChatWidget::clearParticipants()
{
    m_participants.clear();
}

bool ChatWidget::hasParticipant(const QString& userId) const
{
    return m_participants.contains(userId);
}

void ChatWidget::updateParticipantFromHistory(const HistoryMessage& history, QHash<QString, ParticipantInfo>* updatedParticipants)
{
    if (history.senderId.trimmed().isEmpty())
        return;

    ParticipantInfo info = m_participants.value(history.senderId);
    const QString oldName = info.displayName;
    const QString oldAvatar = info.avatarPath;
    info.id = history.senderId;
    if (!history.displayName.isEmpty())
        info.displayName = history.displayName;
    if (!history.avatarPath.isEmpty())
        info.avatarPath = history.avatarPath;
    m_participants.insert(history.senderId, info);

    if (updatedParticipants) {
        if ((!history.displayName.isEmpty() && oldName != info.displayName) ||
            (!history.avatarPath.isEmpty() && oldAvatar != info.avatarPath)) {
            updatedParticipants->insert(history.senderId, info);
        }
    }
}

ChatWidgetMessage ChatWidget::convertHistoryMessage(const HistoryMessage& history)
{
    ChatWidgetMessage msg;
    msg.messageId = history.messageId;
    msg.content = history.content;
    msg.timestamp = history.timestamp;
    msg.messageType = history.messageType;
    msg.status = history.status;
    msg.imagePath = history.imagePath;
    msg.imageThumbnailPath = history.imageThumbnailPath;
    msg.imageWidth = history.imageWidth;
    msg.imageHeight = history.imageHeight;
    msg.filePath = history.filePath;
    msg.fileName = history.fileName;
    msg.fileSize = history.fileSize;
    msg.voicePath = history.voicePath;
    msg.voiceDuration = history.voiceDuration;
    msg.replyToMessageId = history.replyToMessageId;
    msg.replySender = history.replySender;
    msg.replyPreview = history.replyPreview;
    msg.isForwarded = history.isForwarded;
    msg.forwardedFrom = history.forwardedFrom;
    msg.reactions = history.reactions;
    msg.mentions = history.mentions;

    if (history.senderId.trimmed().isEmpty()) {
        msg.senderId = QString();
        msg.sender = history.displayName.isEmpty() ? QStringLiteral("User") : history.displayName;
        msg.avatarPath = history.avatarPath;
        msg.isMine = history.isMine;
    } else {
        const ParticipantInfo info = m_participants.value(history.senderId);
        msg.senderId = history.senderId;
        msg.sender = info.displayName.isEmpty() ? history.senderId : info.displayName;
        msg.avatarPath = info.avatarPath;
        msg.isMine = !m_currentUserId.isEmpty() ? (history.senderId == m_currentUserId) : history.isMine;
    }

    if (!msg.messageId.isEmpty())
        m_messageIds.insert(msg.messageId);

    return msg;
}

void ChatWidget::sortHistoryByTimestamp(QList<HistoryMessage>& messages)
{
    std::sort(messages.begin(), messages.end(), [](const HistoryMessage& a, const HistoryMessage& b) {
        const qint64 at = a.timestamp.isValid() ? a.timestamp.toMSecsSinceEpoch() : 0;
        const qint64 bt = b.timestamp.isValid() ? b.timestamp.toMSecsSinceEpoch() : 0;
        return at < bt;
    });
}

QList<ChatWidgetMessage> ChatWidget::convertHistoryBatch(const QList<HistoryMessage>& sorted, bool dedupe,
                                                          QHash<QString, ParticipantInfo>* updatedParticipants)
{
    QList<ChatWidgetMessage> converted;
    converted.reserve(sorted.size());
    for (const HistoryMessage& history : sorted) {
        if (dedupe && !history.messageId.isEmpty() && m_messageIds.contains(history.messageId))
            continue;
        updateParticipantFromHistory(history, updatedParticipants);
        converted.append(convertHistoryMessage(history));
    }
    return converted;
}

void ChatWidget::applyUpdatedParticipants(const QHash<QString, ParticipantInfo>& updatedParticipants)
{
    if (updatedParticipants.isEmpty())
        return;
    for (auto it = updatedParticipants.constBegin(); it != updatedParticipants.constEnd(); ++it) {
        const ParticipantInfo& info = it.value();
        const QString finalName = info.displayName.isEmpty() ? it.key() : info.displayName;
        if (auto* dataModel = model())
            dataModel->updateParticipantInfo(it.key(), finalName, info.avatarPath);
    }
    if (m_viewWidget)
        m_viewWidget->refreshLayout();
}

void ChatWidget::setHistoryMessages(const QList<HistoryMessage>& messages, bool resetParticipants)
{
    ParticipantInfo currentInfo;
    const bool hasCurrent = !m_currentUserId.isEmpty() && m_participants.contains(m_currentUserId);
    if (hasCurrent) {
        currentInfo = m_participants.value(m_currentUserId);
    }

    if (resetParticipants) {
        m_participants.clear();
        if (hasCurrent) {
            m_participants.insert(m_currentUserId, currentInfo);
        }
        m_messageIds.clear();
    }

    QList<HistoryMessage> sorted = messages;
    sortHistoryByTimestamp(sorted);

    const QList<ChatWidgetMessage> converted = convertHistoryBatch(sorted, true);
    if (m_viewWidget) {
        m_viewWidget->setMessages(converted);
    }
}

void ChatWidget::appendHistoryMessages(const QList<HistoryMessage>& messages, bool sortAndDedupe)
{
    if (messages.isEmpty())
        return;

    QList<HistoryMessage> sorted = messages;
    if (sortAndDedupe) {
        sortHistoryByTimestamp(sorted);
    }

    QHash<QString, ParticipantInfo> updatedParticipants;
    const QList<ChatWidgetMessage> converted = convertHistoryBatch(sorted, sortAndDedupe, &updatedParticipants);

    if (m_viewWidget)
        m_viewWidget->appendMessages(converted);

    applyUpdatedParticipants(updatedParticipants);
}

void ChatWidget::prependHistoryMessages(const QList<HistoryMessage>& messages, bool sortAndDedupe)
{
    if (messages.isEmpty())
        return;

    QList<HistoryMessage> sorted = messages;
    if (sortAndDedupe) {
        sortHistoryByTimestamp(sorted);
    }

    QHash<QString, ParticipantInfo> updatedParticipants;
    const QList<ChatWidgetMessage> converted = convertHistoryBatch(sorted, sortAndDedupe, &updatedParticipants);

    if (m_viewWidget)
        m_viewWidget->prependMessages(converted);

    applyUpdatedParticipants(updatedParticipants);
}

void ChatWidget::onStreamingTimeout()
{
    if (m_streamingIndex < m_streamingContent.length()) {
        int chunk = qMin(3, static_cast<int>(m_streamingContent.length()) - m_streamingIndex);
        streamOutput(m_streamingContent.mid(m_streamingIndex, chunk));
        m_streamingIndex += chunk;
    } else {
        setSendingState(false);
    }
}

ChatWidgetCommandRegistry* ChatWidget::commandRegistry() const
{
    if (auto* input = qobject_cast<ChatWidgetInput*>(m_inputWidget))
        return input->commandRegistry();
    return nullptr;
}

void ChatWidget::registerCommand(const ChatWidgetCommand& command)
{
    if (auto* reg = commandRegistry())
        reg->registerCommand(command);
}

void ChatWidget::unregisterCommand(const QString& name)
{
    if (auto* reg = commandRegistry())
        reg->unregisterCommand(name);
}

void ChatWidget::addImageMessage(const MessageParams& params, const QString& imagePath,
                                 int imageWidth, int imageHeight)
{
    ChatWidgetMessage msg;
    msg.messageId = params.messageId;
    msg.content = params.content;
    msg.timestamp = QDateTime::currentDateTime();
    msg.messageType = ChatWidgetMessage::MessageType::Image;
    msg.imagePath = imagePath;
    msg.imageWidth = imageWidth;
    msg.imageHeight = imageHeight;

    if (params.senderId.trimmed().isEmpty()) {
        msg.sender = params.displayName.isEmpty() ? QStringLiteral("User") : params.displayName;
        msg.avatarPath = params.avatarPath;
        msg.isMine = params.isMine;
    } else {
        ParticipantInfo info = m_participants.value(params.senderId);
        info.id = params.senderId;
        if (!params.displayName.isEmpty()) info.displayName = params.displayName;
        if (!params.avatarPath.isEmpty()) info.avatarPath = params.avatarPath;
        m_participants.insert(params.senderId, info);
        msg.senderId = params.senderId;
        msg.sender = info.displayName.isEmpty() ? params.senderId : info.displayName;
        msg.avatarPath = info.avatarPath;
        msg.isMine = !m_currentUserId.isEmpty() ? (params.senderId == m_currentUserId) : params.isMine;
    }
    addMessageToModel(msg);
}

void ChatWidget::updateImageState(const QString& messageId, const QString& imagePath,
                                  const QString& thumbnailPath, int width, int height,
                                  ChatWidgetMessage::ImageLoadState state)
{
    if (auto* dataModel = model())
        dataModel->updateImageState(messageId, imagePath, thumbnailPath, width, height, state);
}

void ChatWidget::addVoiceMessage(const MessageParams& params, const QString& voicePath, int durationSeconds)
{
    ChatWidgetMessage msg;
    msg.messageId = params.messageId;
    msg.content = params.content;
    msg.timestamp = QDateTime::currentDateTime();
    msg.messageType = ChatWidgetMessage::MessageType::Voice;
    msg.voicePath = voicePath;
    msg.voiceDuration = durationSeconds;

    if (params.senderId.trimmed().isEmpty()) {
        msg.sender = params.displayName.isEmpty() ? QStringLiteral("User") : params.displayName;
        msg.avatarPath = params.avatarPath;
        msg.isMine = params.isMine;
    } else {
        ParticipantInfo info = m_participants.value(params.senderId);
        info.id = params.senderId;
        if (!params.displayName.isEmpty()) info.displayName = params.displayName;
        if (!params.avatarPath.isEmpty()) info.avatarPath = params.avatarPath;
        m_participants.insert(params.senderId, info);
        msg.senderId = params.senderId;
        msg.sender = info.displayName.isEmpty() ? params.senderId : info.displayName;
        msg.avatarPath = info.avatarPath;
        msg.isMine = !m_currentUserId.isEmpty() ? (params.senderId == m_currentUserId) : params.isMine;
    }
    addMessageToModel(msg);
}

void ChatWidget::updateVoicePlayState(const QString& messageId,
                                      ChatWidgetMessage::VoicePlayState state, int progress)
{
    if (auto* dataModel = model())
        dataModel->updateVoicePlayState(messageId, state, progress);
}

void ChatWidget::addFileMessage(const MessageParams& params, const QString& filePath,
                                const QString& fileName, qint64 fileSize)
{
    ChatWidgetMessage msg;
    msg.messageId = params.messageId;
    msg.content = params.content;
    msg.timestamp = QDateTime::currentDateTime();
    msg.messageType = ChatWidgetMessage::MessageType::File;
    msg.filePath = filePath;
    msg.fileName = fileName;
    msg.fileSize = fileSize;

    if (params.senderId.trimmed().isEmpty()) {
        msg.sender = params.displayName.isEmpty() ? QStringLiteral("User") : params.displayName;
        msg.avatarPath = params.avatarPath;
        msg.isMine = params.isMine;
    } else {
        ParticipantInfo info = m_participants.value(params.senderId);
        info.id = params.senderId;
        if (!params.displayName.isEmpty()) info.displayName = params.displayName;
        if (!params.avatarPath.isEmpty()) info.avatarPath = params.avatarPath;
        m_participants.insert(params.senderId, info);
        msg.senderId = params.senderId;
        msg.sender = info.displayName.isEmpty() ? params.senderId : info.displayName;
        msg.avatarPath = info.avatarPath;
        msg.isMine = !m_currentUserId.isEmpty() ? (params.senderId == m_currentUserId) : params.isMine;
    }
    addMessageToModel(msg);
}
