#include "IdentityView.h"
#include "AvatarUtils.h"
#include "chat_list_roles.h"
#include "chat_list_view.h"
#include "chat_list_widget.h"
#include "chat_widget.h"
#include "chat_widget_input.h"
#include "chat_widget_model.h"
#include "chat_widget_view.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/service/AgentRuntime.h"
#include "core/service/ChatService.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include "profile_widget.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTextEdit>
#include <QTime>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
ChatWidget::MessageParams makeSystemMessage(const QString& content)
{
    ChatWidget::MessageParams params;
    params.content = content;
    params.senderId = QStringLiteral("system");
    params.displayName = QStringLiteral("System");
    return params;
}

Session* findLatestPrivateSessionBetween(const QString& userIdentityId, const QString& agentIdentityId)
{
    Session* latest = nullptr;
    const QList<Session*> sessions = SessionManager::instance()->sessionsForIdentity(userIdentityId);
    for (Session* session : sessions) {
        if (!session)
            continue;
        if (session->type() != Session::SessionType::Private)
            continue;
        const QStringList participants = session->participantIds();
        if (participants.size() != 2)
            continue;
        if (!session->hasParticipant(userIdentityId) || !session->hasParticipant(agentIdentityId))
            continue;
        if (!latest || session->lastActiveAt() > latest->lastActiveAt())
            latest = session;
    }
    return latest;
}
} // namespace

// ==================== 构造函数 ====================

IdentityView::IdentityView(const QString& identityId, ChatService* chatService, QWidget* parent)
    : QWidget(parent)
    , m_identityId(identityId)
    , m_chatService(chatService)
{
    setupUI();
}

bool IdentityView::isUserView() const
{
    Identity* identity = IdentityManager::instance()->findById(m_identityId);
    return identity && identity->isUser();
}

void IdentityView::syncInputAvailability()
{
    if (!m_chatWidget)
        return;
    QWidget* input = m_chatWidget->inputWidget();
    if (!input)
        return;

    const bool hasActiveSession = !m_currentSessionId.isEmpty() && SessionManager::instance()->findById(m_currentSessionId) != nullptr;
    const bool canSendMessage = hasActiveSession && m_chatService && m_chatService->canIdentitySendMessage(m_identityId, m_currentSessionId);
    input->setVisible(canSendMessage);
    input->setEnabled(canSendMessage);
}

// ==================== setupUI ====================

void IdentityView::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    // --- 左侧：会话列表 + 按钮 ---
    QWidget* leftContainer = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_chatListWidget = new ChatListWidget(this);
    m_chatListWidget->applyStyleSheetFile("chat_list.qss");
    m_chatListWidget->enableSearchFiltering(true);
    m_chatListWidget->setSearchPlaceholder(tr("搜索会话"));
    m_chatListWidget->setSearchRoles(QList<int>() << ChatListNameRole << ChatListMessageRole);
    const bool canManageSessions = m_chatService && m_chatService->canIdentityManageSessions(m_identityId);
    if (canManageSessions) {
        m_chatListWidget->addHeaderAction(tr("新会话"), QStringLiteral("new_chat"));
        m_chatListWidget->addHeaderAction(tr("删除"), QStringLiteral("remove_current"));
    }
    leftLayout->addWidget(m_chatListWidget, 1);

    splitter->addWidget(leftContainer);

    // --- 中间：聊天区 ---
    QWidget* centerContainer = new QWidget(this);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerContainer);
    centerLayout->setContentsMargins(0, 2, 0, 0);

    m_chatWidget = new ChatWidget(this);
    m_chatWidget->applyStyleSheetFile("chat_widget.qss");
    centerLayout->addWidget(m_chatWidget, 1);

    splitter->addWidget(centerContainer);

    // --- 右侧：历史面板（详细数据 + 原始数据） ---
    QWidget* historyContainer = new QWidget(this);
    QVBoxLayout* historyLayout = new QVBoxLayout(historyContainer);
    historyLayout->setContentsMargins(0, 0, 0, 0);

    m_historyLabel = new QLabel(tr("请求/响应历史 (共 0 次)"), this);
    QFont labelFont = m_historyLabel->font();
    labelFont.setBold(true);
    m_historyLabel->setFont(labelFont);
    historyLayout->addWidget(m_historyLabel);

    m_turnList = new QListWidget(this);
    m_turnList->setStyleSheet(
        "QListWidget { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; }"
        "QListWidget::item { padding: 8px 10px; border-bottom: 1px solid #f1f5f9; }"
        "QListWidget::item:selected { background: #eff6ff; color: #1d4ed8; }");
    m_turnList->setMinimumHeight(170);
    historyLayout->addWidget(m_turnList, 0);

    m_historyTabs = new QTabWidget(this);
    QWidget* summaryTab = new QWidget(this);
    QVBoxLayout* summaryLayout = new QVBoxLayout(summaryTab);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    m_historySummaryDisplay = new QPlainTextEdit(this);
    m_historySummaryDisplay->setReadOnly(true);
    m_historySummaryDisplay->setStyleSheet(
        "QPlainTextEdit { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; "
        "padding: 8px; color: #111827; }");
    summaryLayout->addWidget(m_historySummaryDisplay, 1);

    QWidget* rawTab = new QWidget(this);
    QVBoxLayout* rawLayout = new QVBoxLayout(rawTab);
    rawLayout->setContentsMargins(0, 0, 0, 0);
    m_historyDisplay = new QTreeWidget(this);
    m_historyDisplay->setColumnCount(2);
    m_historyDisplay->setHeaderLabels(QStringList() << tr("Key") << tr("Value"));
    m_historyDisplay->setRootIsDecorated(true);
    m_historyDisplay->setAlternatingRowColors(true);
    m_historyDisplay->setStyleSheet(
        "QTreeWidget { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; "
        "alternate-background-color: #f8fafc; }"
        "QTreeWidget::item { border-radius: 8px; }"
        "QHeaderView::section { background: #f8fafc; border: none; border-bottom: 1px solid #e5e7eb; "
        "padding: 6px 8px; }");
    m_historyDisplay->header()->setStretchLastSection(true);
    rawLayout->addWidget(m_historyDisplay, 1);

    m_historyTabs->addTab(summaryTab, tr("详细数据"));
    m_historyTabs->addTab(rawTab, tr("原始数据"));
    historyLayout->addWidget(m_historyTabs, 1);

    m_clearHistoryBtn = new QPushButton(tr("清空历史"), this);
    m_clearHistoryBtn->setStyleSheet("border: 1px solid #e5e7eb; border-radius: 10px; padding: 6px 10px; background: #f5f5f5;");
    historyLayout->addWidget(m_clearHistoryBtn);

    splitter->addWidget(historyContainer);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes(QList<int>() << 300 << 580 << 320);

    mainLayout->addWidget(splitter);

    // 连接 UI 信号
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &IdentityView::onClearHistoryClicked);
    connect(m_turnList, &QListWidget::currentRowChanged, this, &IdentityView::onTurnSelectionChanged);
    connect(m_chatWidget, &ChatWidget::messageSent, this, &IdentityView::onUserMessageSent);
    connect(m_chatWidget, &ChatWidget::messageActionRequested, this, &IdentityView::onMessageActionRequested);
    const bool canSendMessage = m_chatService && m_chatService->canIdentitySendMessage(m_identityId);
    if (canSendMessage)
        connect(m_chatWidget, &ChatWidget::stopRequested, this, &IdentityView::onAbortClicked);
    connect(m_chatListWidget, &ChatListWidget::headerActionTriggered, this, [this](QAction* action) {
        QString data = action->data().toString();
        if (data == QLatin1String("new_chat"))
            onNewChatRequested();
        else if (data == QLatin1String("remove_current"))
            onRemoveCurrentChatRequested();
    });
    connect(m_chatListWidget, &ChatListWidget::chatItemActivated, this, &IdentityView::onChatItemActivated);
    connect(m_chatListWidget, &ChatListWidget::chatItemRemoved, this, &IdentityView::onChatItemRemoved);
    connect(m_chatListWidget, &ChatListWidget::chatItemRenamed, this, &IdentityView::onChatItemRenamed);
    connect(m_chatListWidget, &ChatListWidget::currentChanged, this, [this](const QModelIndex& current, const QModelIndex&) {
        if (!m_chatListWidget || !m_chatWidget)
            return;
        if (!current.isValid())
            return;
        int row = -1;
        if (QSortFilterProxyModel* proxy = qobject_cast<QSortFilterProxyModel*>(m_chatListWidget->listView()->model()))
            row = proxy->mapToSource(current).row();
        else
            row = current.row();
        QString sessionId = sessionIdForRow(row);
        if (sessionId.isEmpty() || sessionId == m_currentSessionId)
            return;

        m_chatService->switchSession(sessionId);
        m_currentSessionId = sessionId;

        Session* session = SessionManager::instance()->findById(sessionId);
        m_chatWidget->setEmptyStateVisible(false);
        syncInputAvailability();
        restoreChatFromSession(session);
        updateHistoryDisplay();
        updateSendingState();
        m_chatService->saveSessionsToDisk();
    });

    if (ChatWidgetInput* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget())) {
        connect(input, &ChatWidgetInput::voiceStartRequested, this, &IdentityView::onVoiceStartRequested);
        connect(input, &ChatWidgetInput::voiceStopRequested, this, &IdentityView::onVoiceStopRequested);
    }

    if (ChatWidgetView* chatView = m_chatWidget->view()) {
        connect(chatView, &ChatWidgetView::avatarClicked, this, &IdentityView::onAvatarClicked);
    }

    // 根据视角设置 ChatWidget 的"当前用户"，决定消息左右方向
    if (isUserView()) {
        Identity* userIdentity = IdentityManager::instance()->userIdentity();
        const QString userAvatar = userIdentity ? userIdentity->avatar().trimmed() : QString();
        m_chatWidget->setCurrentUser(QStringLiteral("user"), QStringLiteral("Me"), userAvatar);
    } else {
        Identity* identity = IdentityManager::instance()->findById(m_identityId);
        QString agentName = identity ? identity->name() : QStringLiteral("Agent");
        const QString agentAvatar = identity ? identity->avatar().trimmed() : QString();
        m_chatWidget->setCurrentUser(m_identityId, agentName, agentAvatar);
    }

    if (m_chatWidget->inputWidget()) {
        m_chatWidget->inputWidget()->setEnabled(canSendMessage);
    }
    syncInputAvailability();
}

// ==================== activate / deactivate ====================

void IdentityView::activate()
{
    m_isActive = true;
    resetStreamState();
    if (!m_sessionListLoaded || m_sessionListDirty)
        reloadSessionList();
    if (!m_currentSessionId.isEmpty()) {
        Session* session = SessionManager::instance()->findById(m_currentSessionId);
        if (session) {
            m_chatWidget->setEmptyStateVisible(false);
            syncInputAvailability();
            restoreChatFromSession(session);
        }
        updateHistoryDisplay();

        // 如果当前 Session 正在流式输出，恢复流式渲染状态
        if (session && session->isStreaming()) {
            m_chatWidget->setSendingState(true);
            applyUserSendingOverride();
            const Session::StreamState& state = session->streamState();
            if (!state.buffer.isEmpty()) {
                // 添加一个占位消息并填入已有 buffer
                QString agentName = m_chatService->agentDisplayNameForSession(m_currentSessionId);
                const QString agentId = streamAgentIdentityId(m_currentSessionId);
                ChatWidget::MessageParams params;
                params.content = QString();
                params.senderId = agentId.isEmpty() ? m_identityId : agentId;
                params.displayName = agentName;
                params.avatarPath = identityAvatarPath(params.senderId);
                m_chatWidget->addMessage(params);
                m_pendingStreamMsgRow = m_chatWidget->messageCount() - 1;
                m_chatWidget->setStreamTargetRow(m_pendingStreamMsgRow);
                m_chatWidget->streamOutput(state.buffer);
                m_hasPendingStreamMsg = true;
            }
        }
    }
    updateSendingState();
}

void IdentityView::deactivate()
{
    m_isActive = false;
    resetStreamState();
}

// ==================== 内部辅助 ====================

void IdentityView::resetStreamState()
{
    m_hasPendingStreamMsg = false;
    m_pendingStreamMsgRow = -1;
    if (m_chatWidget)
        m_chatWidget->clearStreamTargetRow();
}

void IdentityView::applyUserSendingOverride()
{
    if (isUserView()) {
        if (auto* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget()))
            input->setSendingState(false);
    }
}

void IdentityView::selectSessionRow(int row)
{
    if (!m_chatListWidget || row < 0)
        return;
    QAbstractItemModel* model = m_chatListWidget->listView()->model();
    QModelIndex sel;
    if (QSortFilterProxyModel* proxy = qobject_cast<QSortFilterProxyModel*>(model))
        sel = proxy->mapFromSource(m_chatListWidget->listView()->standardModel()->index(row, 0));
    else if (model)
        sel = model->index(row, 0);
    if (sel.isValid())
        m_chatListWidget->listView()->setCurrentIndex(sel);
}

// ==================== 会话列表 ====================

void IdentityView::reloadSessionList()
{
    m_filteredSessionIds.clear();
    m_chatListWidget->clearChats();

    // 阶段 1/2：严格按 Identity 隔离视角数据，避免“全局会话列表”泄漏。
    const QList<Session*> sessions = SessionManager::instance()->sessionsForIdentity(m_identityId);

    for (Session* s : sessions) {
        m_filteredSessionIds.append(s->id());
        const int row = m_chatListWidget->addChatItem(
            sessionDisplayName(s),
            QString(), QString(), QColor(Qt::gray), 0);
        const QString avatarPath = sessionAvatarPath(s);
        if (row >= 0 && !avatarPath.isEmpty())
            m_chatListWidget->updateChatItemData(row, ChatListAvatarPathRole, avatarPath);
    }

    // 恢复选中状态
    if (!m_currentSessionId.isEmpty()) {
        int row = rowForSessionId(m_currentSessionId);
        selectSessionRow(row);
    }

    m_sessionListLoaded = true;
    m_sessionListDirty = false;
}

void IdentityView::markSessionListDirty()
{
    m_sessionListDirty = true;
}

void IdentityView::refreshSendingState()
{
    updateSendingState();
}

void IdentityView::refreshHistoryForSession(const QString& sessionId)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;
    if (!m_isActive || sessionId != m_currentSessionId)
        return;
    updateHistoryDisplay();
}

QString IdentityView::sessionIdForRow(int row) const
{
    return (row >= 0 && row < m_filteredSessionIds.size()) ? m_filteredSessionIds.at(row) : QString();
}

int IdentityView::rowForSessionId(const QString& sessionId) const
{
    return m_filteredSessionIds.indexOf(sessionId);
}

void IdentityView::updateChatListItem(const QString& sessionId, const QString& preview)
{
    if (!m_chatListWidget)
        return;
    int row = rowForSessionId(sessionId);
    if (row < 0)
        return;
    QStandardItemModel* src = m_chatListWidget->listView()->standardModel();
    if (!src || row >= src->rowCount())
        return;
    Session* session = SessionManager::instance()->findById(sessionId);
    QString name = src->index(row, 0).data(ChatListNameRole).toString();
    if (name.isEmpty())
        name = sessionDisplayName(session);
    QString shortPreview = preview;
    if (shortPreview.length() > 80)
        shortPreview = shortPreview.left(80) + QStringLiteral("...");
    m_chatListWidget->updateChatItem(row, name, shortPreview, QTime::currentTime().toString(QStringLiteral("hh:mm")), QColor(Qt::gray), 0);
    m_chatListWidget->updateChatItemData(row, ChatListAvatarPathRole, sessionAvatarPath(session));
}

QString IdentityView::sessionDisplayName(Session* session) const
{
    if (!session)
        return tr("新对话");

    const QStringList participants = session->participantIds();
    if (session->type() == Session::SessionType::Private) {
        QString counterpartId;
        for (const QString& pid : participants) {
            if (pid != m_identityId) {
                counterpartId = pid;
                break;
            }
        }
        if (counterpartId.isEmpty() && !participants.isEmpty())
            counterpartId = participants.first();

        if (!counterpartId.isEmpty()) {
            Identity* identity = IdentityManager::instance()->findById(counterpartId);
            if (identity) {
                if (identity->isUser())
                    return tr("用户");
                const QString displayName = identity->name().trimmed();
                if (!displayName.isEmpty())
                    return displayName;
                return tr("Agent");
            }
        }
    }

    const QString title = session->title().trimmed();
    if (!title.isEmpty())
        return title;

    if (session->type() == Session::SessionType::Group) {
        QStringList names;
        for (const QString& pid : participants) {
            if (pid == m_identityId)
                continue;
            Identity* identity = IdentityManager::instance()->findById(pid);
            if (!identity)
                continue;
            if (identity->isUser())
                names.append(tr("用户"));
            else if (!identity->name().trimmed().isEmpty())
                names.append(identity->name().trimmed());
            if (names.size() >= 3)
                break;
        }
        names.removeDuplicates();
        if (!names.isEmpty())
            return names.join(QStringLiteral("、"));
        return tr("群聊");
    }

    return tr("新对话");
}

QString IdentityView::sessionAvatarPath(Session* session) const
{
    if (!session)
        return QString();

    const QStringList participants = session->participantIds();
    if (participants.isEmpty())
        return QString();

    QString counterpartId;

    if (session->type() == Session::SessionType::Private) {
        for (const QString& pid : participants) {
            if (pid != m_identityId) {
                counterpartId = pid;
                break;
            }
        }
    } else {
        // 群聊：Agent 视角优先显示用户头像；用户视角优先显示 Agent 头像。
        const bool preferUser = !isUserView();
        for (const QString& pid : participants) {
            if (pid == m_identityId)
                continue;
            Identity* candidate = IdentityManager::instance()->findById(pid);
            if (!candidate)
                continue;
            if (preferUser && candidate->isUser()) {
                counterpartId = pid;
                break;
            }
            if (!preferUser && candidate->isAgent()) {
                counterpartId = pid;
                break;
            }
            if (counterpartId.isEmpty())
                counterpartId = pid;
        }
    }

    if (counterpartId.isEmpty())
        counterpartId = participants.first();

    Identity* identity = IdentityManager::instance()->findById(counterpartId);
    if (!identity)
        return QString();
    return identity->avatar().trimmed();
}

QString IdentityView::identityAvatarPath(const QString& identityId) const
{
    const QString normalized = identityId.trimmed();
    if (normalized.isEmpty())
        return QString();

    if (normalized == QLatin1String("user")) {
        Identity* userIdentity = IdentityManager::instance()->userIdentity();
        return userIdentity ? userIdentity->avatar().trimmed() : QString();
    }

    Identity* identity = IdentityManager::instance()->findById(normalized);
    return identity ? identity->avatar().trimmed() : QString();
}

QString IdentityView::streamAgentIdentityId(const QString& sessionId) const
{
    if (m_chatService) {
        if (AgentRuntime* runtime = m_chatService->runtimeForSession(sessionId)) {
            const QString runtimeIdentityId = runtime->identityId().trimmed();
            if (!runtimeIdentityId.isEmpty())
                return runtimeIdentityId;
            if (runtime->identity())
                return runtime->identity()->id();
        }
    }

    Session* session = SessionManager::instance()->findById(sessionId);
    if (!session)
        return QString();
    for (const QString& participantId : session->participantIds()) {
        Identity* identity = IdentityManager::instance()->findById(participantId);
        if (identity && identity->isAgent())
            return identity->id();
    }
    return QString();
}

// ==================== UI 辅助 ====================

void IdentityView::updateSendingState()
{
    if (!m_chatWidget)
        return;
    bool sending = m_chatService->isSessionStreaming(m_currentSessionId);
    m_chatWidget->setSendingState(sending);

    // 用户视角：保留 ChatWidget::m_isSending 门控，但按钮始终显示"发送"
    if (sending)
        applyUserSendingOverride();
}

void IdentityView::setSendingState(bool isSending)
{
    ChatWidgetInput* input = nullptr;
    if (m_chatWidget) {
        input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget());
        if (input)
            input->setSendingState(isSending);
    }
}

void IdentityView::clearChatMessages()
{
    if (m_chatWidget)
        m_chatWidget->clearMessages();
}

void IdentityView::restoreChatFromSession(Session* session)
{
    if (!session) {
        restoreChatFromHistory(QJsonArray());
        return;
    }

    const QList<Message> messages = session->allMessages();
    if (messages.isEmpty()) {
        clearChatMessages();
        return;
    }

    clearChatMessages();
    for (const Message& msg : messages) {
        if (msg.content.type == MessageContent::Type::ToolCall
            || msg.content.type == MessageContent::Type::ToolResult) {
            continue;
        }

        // File 类型消息：通过 HistoryMessage 渲染文件卡片
        if (msg.content.type == MessageContent::Type::File) {
            const QString filePath = msg.content.payload.value("file_path").toString();
            const QString fileName = msg.content.payload.value("file_name").toString();
            if (filePath.isEmpty() || fileName.isEmpty())
                continue;

            ChatWidget::HistoryMessage historyMsg;
            historyMsg.messageType = ChatWidgetMessage::MessageType::File;
            historyMsg.messageId = msg.id;
            historyMsg.filePath = filePath;
            historyMsg.fileName = fileName;
            historyMsg.fileSize = static_cast<qint64>(msg.content.payload.value("file_size").toDouble());
            historyMsg.content = msg.content.text.isEmpty() ? fileName : msg.content.text;
            historyMsg.timestamp = msg.timestamp;

            Identity* senderIdentity = IdentityManager::instance()->findById(msg.senderId);
            if (senderIdentity && !senderIdentity->isUser()) {
                historyMsg.senderId = senderIdentity->id();
                historyMsg.displayName = senderIdentity->name().trimmed().isEmpty()
                    ? QStringLiteral("Agent")
                    : senderIdentity->name().trimmed();
                historyMsg.avatarPath = senderIdentity->avatar().trimmed();
                historyMsg.isMine = (msg.senderId == m_identityId);
            } else {
                historyMsg.senderId = QStringLiteral("user");
                historyMsg.displayName = QStringLiteral("用户");
                historyMsg.avatarPath = identityAvatarPath(QStringLiteral("user"));
                historyMsg.isMine = true;
            }
            m_chatWidget->appendHistoryMessages({historyMsg});
            continue;
        }

        if (msg.content.text.trimmed().isEmpty())
            continue;

        ChatWidget::MessageParams params;
        params.messageId = msg.id;
        params.content = msg.content.text;

        if (msg.content.type == MessageContent::Type::System || msg.senderId == QLatin1String("system")) {
            params.senderId = QStringLiteral("system");
            params.displayName = QStringLiteral("System");
        } else {
            Identity* senderIdentity = IdentityManager::instance()->findById(msg.senderId);
            const bool isSelf = (msg.senderId == m_identityId);
            if (!senderIdentity || senderIdentity->isUser()) {
                params.senderId = QStringLiteral("user");
                params.displayName = QStringLiteral("用户");
                params.avatarPath = identityAvatarPath(QStringLiteral("user"));
            } else if (isSelf) {
                params.senderId = m_identityId;
                params.displayName = senderIdentity->name().trimmed().isEmpty()
                    ? QStringLiteral("Me")
                    : senderIdentity->name();
                params.avatarPath = senderIdentity->avatar().trimmed();
            } else {
                params.senderId = senderIdentity->id();
                params.displayName = senderIdentity->name().trimmed().isEmpty()
                    ? QStringLiteral("Agent")
                    : senderIdentity->name().trimmed();
                params.avatarPath = senderIdentity->avatar().trimmed();
            }
        }

        m_chatWidget->addMessage(params);
    }
}

void IdentityView::restoreChatFromHistory(const QJsonArray& history)
{
    if (!m_chatWidget)
        return;
    clearChatMessages();
    const QString assistantName = m_chatService->agentDisplayNameForSession(m_currentSessionId);
    for (const QJsonValue& v : history) {
        QJsonObject o = v.toObject();
        QString role = o["role"].toString();
        QString content = o["content"].toString();
        if (content.isEmpty())
            continue;
        if (role == QLatin1String("tool"))
            continue;

        ChatWidget::MessageParams params;
        params.messageId = o.value(QStringLiteral("message_id")).toString().trimmed();
        params.content = content;
        if (role == QLatin1String("user")) {
            params.senderId = QStringLiteral("user");
            params.displayName = QStringLiteral("用户");
            params.avatarPath = identityAvatarPath(QStringLiteral("user"));
        } else {
            const QString assistantId = streamAgentIdentityId(m_currentSessionId);
            params.senderId = assistantId.isEmpty() ? m_identityId : assistantId;
            params.displayName = assistantName;
            params.avatarPath = identityAvatarPath(params.senderId);
        }
        m_chatWidget->addMessage(params);
    }
}

// ==================== 会话操作 ====================

void IdentityView::onNewChatRequested()
{
    if (!m_chatService || !m_chatService->canIdentityManageSessions(m_identityId))
        return;

    IdentityManager* identityMgr = IdentityManager::instance();
    if (!identityMgr)
        return;

    Identity* currentIdentity = identityMgr->findById(m_identityId);
    if (!currentIdentity || !currentIdentity->isUser())
        return;

    QList<Identity*> agents = identityMgr->allAgents();
    agents.erase(std::remove_if(agents.begin(), agents.end(), [](Identity* identity) {
                     return identity == nullptr;
                 }),
                 agents.end());
    std::sort(agents.begin(), agents.end(), [](Identity* a, Identity* b) {
        return a->name().localeAwareCompare(b->name()) < 0;
    });

    if (agents.isEmpty()) {
        QMessageBox::information(this, tr("新建聊天会话"), tr("当前还没有可用的助手，请先创建助手。"));
        return;
    }

    QDialog picker(this);
    picker.setWindowTitle(tr("选择助手"));
    picker.setMinimumWidth(360);

    QVBoxLayout* layout = new QVBoxLayout(&picker);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    QListWidget* listWidget = new QListWidget(&picker);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    listWidget->setAlternatingRowColors(false);
    listWidget->setIconSize(QSize(38, 38));
    listWidget->setUniformItemSizes(true);
    listWidget->setSpacing(2);

    for (Identity* agent : agents) {
        const QString agentName = agent->name().trimmed().isEmpty() ? tr("未命名助手") : agent->name().trimmed();
        auto* item = new QListWidgetItem(
            AvatarUtils::makeAvatarIcon(agent->id(), agentName, agent->avatar(), 38, 10),
            agentName,
            listWidget);
        item->setData(Qt::UserRole, agent->id());
        item->setToolTip(agentName);
    }

    if (listWidget->count() > 0)
        listWidget->setCurrentRow(0);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &picker);
    QPushButton* okButton = buttons->button(QDialogButtonBox::Ok);
    if (okButton)
        okButton->setText(tr("确定"));
    QPushButton* cancelButton = buttons->button(QDialogButtonBox::Cancel);
    if (cancelButton)
        cancelButton->setText(tr("取消"));

    connect(buttons, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
    connect(listWidget, &QListWidget::itemDoubleClicked, &picker, &QDialog::accept);

    layout->addWidget(listWidget, 1);
    layout->addWidget(buttons);

    if (picker.exec() != QDialog::Accepted)
        return;

    QListWidgetItem* selectedItem = listWidget->currentItem();
    if (!selectedItem)
        return;

    const QString agentIdentityId = selectedItem->data(Qt::UserRole).toString().trimmed();
    if (agentIdentityId.isEmpty())
        return;

    Session* session = findLatestPrivateSessionBetween(m_identityId, agentIdentityId);
    if (!session) {
        session = m_chatService->createSessionForIdentityAs(m_identityId, agentIdentityId, tr("新对话"));
    }

    if (!session)
        return;

    m_chatService->switchSession(session->id());
    m_currentSessionId = session->id();
    reloadSessionList();

    selectSessionRow(rowForSessionId(m_currentSessionId));

    if (m_chatWidget) {
        m_chatWidget->setEmptyStateVisible(false);
        syncInputAvailability();
        restoreChatFromSession(session);
    }

    updateHistoryDisplay();
    updateSendingState();
    m_chatService->saveSessionsToDisk();
}

void IdentityView::onChatItemActivated(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount)
{
    Q_UNUSED(message);
    Q_UNUSED(time);
    Q_UNUSED(avatarColor);
    Q_UNUSED(unreadCount);
    Q_UNUSED(name);
    if (!m_chatListWidget || !m_chatWidget)
        return;
    QModelIndex idx = m_chatListWidget->listView()->currentIndex();
    if (!idx.isValid())
        return;
    int row;
    if (QSortFilterProxyModel* proxy = qobject_cast<QSortFilterProxyModel*>(m_chatListWidget->listView()->model()))
        row = proxy->mapToSource(idx).row();
    else
        row = idx.row();

    QString sessionId = sessionIdForRow(row);
    if (sessionId.isEmpty() || sessionId == m_currentSessionId)
        return;

    m_chatService->switchSession(sessionId);
    m_currentSessionId = sessionId;

    Session* session = SessionManager::instance()->findById(sessionId);
    m_chatWidget->setEmptyStateVisible(false);
    syncInputAvailability();
    restoreChatFromSession(session);
    updateHistoryDisplay();
    updateSendingState();
    m_chatService->saveSessionsToDisk();
}

void IdentityView::onChatItemRemoved(int row)
{
    QString sessionId = sessionIdForRow(row);
    if (sessionId.isEmpty())
        return;

    if (!m_chatService->removeSessionAs(m_identityId, sessionId)) {
        reloadSessionList();
        return;
    }
    m_filteredSessionIds.removeAll(sessionId);

    if (m_currentSessionId == sessionId) {
        m_currentSessionId.clear();
        m_chatListWidget->listView()->clearSelection();
        m_chatListWidget->listView()->setCurrentIndex(QModelIndex());
        clearChatMessages();
        if (m_chatWidget)
            m_chatWidget->setEmptyStateVisible(true);
        syncInputAvailability();
    }
    updateHistoryDisplay();
    updateSendingState();
    m_chatService->saveSessionsToDisk();
}

void IdentityView::onChatItemRenamed(int row, const QString& name)
{
    if (!m_chatService || !m_chatService->canIdentityManageSessions(m_identityId)) {
        reloadSessionList();
        return;
    }

    QString sessionId = sessionIdForRow(row);
    if (sessionId.isEmpty())
        return;
    Session* session = SessionManager::instance()->findById(sessionId);
    if (session)
        session->setTitle(name.trimmed());

    AgentRuntime* runtime = m_chatService->runtimeForSession(sessionId);
    if (runtime && runtime->identity()) {
        runtime->identity()->setName(name.trimmed());
        LLMConfig cfg = runtime->config();
        cfg.userName = name.trimmed();
        runtime->setConfig(cfg);
    }
    m_chatService->saveSessionsToDisk();
}

void IdentityView::onRemoveCurrentChatRequested()
{
    if (!m_chatService || !m_chatService->canIdentityManageSessions(m_identityId))
        return;

    int row = rowForSessionId(m_currentSessionId);
    if (row < 0)
        return;
    if (!m_chatListWidget->removeChatItem(row))
        return;
    onChatItemRemoved(row);
}

// ==================== 用户消息与中止 ====================

void IdentityView::onUserMessageSent(const QString& content)
{
    QString prompt = content.trimmed();
    if (prompt.isEmpty())
        return;

    // 立即重置 sending state（抵消子模块内部的自动锁定）
    m_chatWidget->setSendingState(false);

    updateChatListItem(m_currentSessionId, prompt);
    const QString turnId = m_chatService->enqueueUserMessageAs(m_identityId, m_currentSessionId, prompt);
    if (turnId.isEmpty()) {
        if (!m_chatService->canIdentitySendMessage(m_identityId, m_currentSessionId))
            m_chatWidget->addMessage(makeSystemMessage(QStringLiteral("[当前视角无发送权限]")));
        return;
    }
    updateHistoryDisplay();
}

void IdentityView::onAbortClicked()
{
    qDebug() << "IdentityView: [Signal Received] Stop requested by User UI";

    const bool wasStreaming = m_chatService->isSessionStreaming(m_currentSessionId);
    QString rolledBackUserMsg = m_chatService->abortAndRollback(m_currentSessionId);

    if (m_chatWidget && wasStreaming) {
        m_chatWidget->addMessage(makeSystemMessage(QStringLiteral("[已手动中断]")));
        if (!rolledBackUserMsg.isEmpty()) {
            if (auto* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget())) {
                if (auto* edit = input->findChild<QTextEdit*>("chatWidgetInputEdit")) {
                    edit->setPlainText(rolledBackUserMsg);
                    edit->setFocus();
                }
            }
        }
    }
    updateHistoryDisplay();
    updateSendingState();
}

void IdentityView::onMessageActionRequested(const QString& action, const QString& messageId, const QString& content)
{
    if (action != QLatin1String("remember"))
        return;
    if (!m_chatService || m_currentSessionId.trimmed().isEmpty())
        return;

    QString err;
    const bool ok = m_chatService->rememberMessageAs(m_identityId, m_currentSessionId, messageId, content, &err);
    if (ok) {
        if (m_chatWidget)
            m_chatWidget->addMessage(makeSystemMessage(QStringLiteral("[已加入长期记忆]")));
        updateHistoryDisplay();
        return;
    }

    const QString msg = err.trimmed().isEmpty()
        ? QStringLiteral("[记忆失败]")
        : QStringLiteral("[记忆失败] %1").arg(err.trimmed());
    if (m_chatWidget)
        m_chatWidget->addMessage(makeSystemMessage(msg));
}

// ==================== 流式处理（由 MainWindow 路由） ====================

void IdentityView::handleStreamData(const QString& sessionId, const QString& data)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;
    if (!m_isActive || sessionId != m_currentSessionId)
        return;
    if (!m_chatWidget)
        return;

    Session* session = SessionManager::instance()->findById(sessionId);
    if (!session)
        return;

    m_chatWidget->setSendingState(true);
    applyUserSendingOverride();
    if (!m_hasPendingStreamMsg) {
        QString agentName = m_chatService->agentDisplayNameForSession(sessionId);
        const QString agentId = streamAgentIdentityId(sessionId);
        ChatWidget::MessageParams params;
        params.content = QString();
        params.senderId = agentId.isEmpty() ? m_identityId : agentId;
        params.displayName = agentName;
        params.avatarPath = identityAvatarPath(params.senderId);
        m_chatWidget->addMessage(params);
        m_pendingStreamMsgRow = m_chatWidget->messageCount() - 1;
        m_chatWidget->setStreamTargetRow(m_pendingStreamMsgRow);
        m_hasPendingStreamMsg = true;
    }
    m_chatWidget->streamOutput(data);
}

void IdentityView::handleFinished(const QString& sessionId, const QString& fullContent)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    if (!fullContent.isEmpty())
        updateChatListItem(sessionId, fullContent);

    if (!m_isActive || !m_chatWidget || sessionId != m_currentSessionId) {
        resetStreamState();
        updateSendingState();
        return;
    }

    if (m_hasPendingStreamMsg) {
        if (fullContent.isEmpty())
            m_chatWidget->removeMessageAt(m_pendingStreamMsgRow);
        else
            m_chatWidget->updateMessageContentAtRow(m_pendingStreamMsgRow, fullContent);
    } else if (!fullContent.isEmpty()) {
        QString agentName = m_chatService->agentDisplayNameForSession(sessionId);
        const QString agentId = streamAgentIdentityId(sessionId);
        ChatWidget::MessageParams params;
        params.content = fullContent;
        params.senderId = agentId.isEmpty() ? m_identityId : agentId;
        params.displayName = agentName;
        params.avatarPath = identityAvatarPath(params.senderId);
        m_chatWidget->addMessage(params);
    }
    resetStreamState();
    updateSendingState();
    updateHistoryDisplay();
}

void IdentityView::handleError(const QString& sessionId, const QString& errorMsg)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    resetStreamState();
    updateSendingState();

    if (m_isActive && m_chatWidget && sessionId == m_currentSessionId) {
        m_chatWidget->addMessage(makeSystemMessage(
            QString::fromUtf8("❌ 错误: %1").arg(errorMsg)));
        updateHistoryDisplay();
    }
}

void IdentityView::handleToolCallsStarted(const QString& sessionId)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    resetStreamState();

    if (m_isActive && sessionId == m_currentSessionId)
        updateHistoryDisplay();
}

void IdentityView::handleToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;
    if (!m_isActive || sessionId != m_currentSessionId || !m_chatWidget)
        return;

    // send_file 工具完成后，在聊天区渲染文件卡片
    if (event.toolName == QLatin1String("send_file")
        && event.status == QLatin1String("completed")
        && event.success
        && !event.data.isEmpty()) {
        const QString filePath = event.data.value(QStringLiteral("file_path")).toString();
        const QString fileName = event.data.value(QStringLiteral("file_name")).toString();
        const qint64 fileSize = static_cast<qint64>(event.data.value(QStringLiteral("file_size")).toDouble());
        const QString description = event.data.value(QStringLiteral("description")).toString();
        if (filePath.isEmpty() || fileName.isEmpty())
            return;

        const QString agentId = streamAgentIdentityId(sessionId);
        const QString agentName = m_chatService->agentDisplayNameForSession(sessionId);

        ChatWidget::HistoryMessage fileMsg;
        fileMsg.messageType = ChatWidgetMessage::MessageType::File;
        fileMsg.senderId = agentId.isEmpty() ? m_identityId : agentId;
        fileMsg.displayName = agentName;
        fileMsg.avatarPath = identityAvatarPath(fileMsg.senderId);
        fileMsg.content = description.isEmpty() ? fileName : description;
        fileMsg.filePath = filePath;
        fileMsg.fileName = fileName;
        fileMsg.fileSize = fileSize;
        fileMsg.timestamp = QDateTime::currentDateTime();
        fileMsg.isMine = false;
        m_chatWidget->appendHistoryMessages({fileMsg});
    }
}

// ==================== 历史面板 ====================

static QString jsonValueToString(const QJsonValue& value)
{
    switch (value.type()) {
    case QJsonValue::Null:
        return QStringLiteral("null");
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:
        return QString::number(value.toDouble());
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array:
        return QStringLiteral("[%1]").arg(value.toArray().size());
    case QJsonValue::Object:
        return QStringLiteral("{%1}").arg(value.toObject().size());
    case QJsonValue::Undefined:
        return QStringLiteral("undefined");
    }
    return QString();
}

static QString compactText(const QString& text, int maxChars = 120)
{
    QString compact = text;
    compact.replace(QLatin1Char('\r'), QLatin1Char(' '));
    compact.replace(QLatin1Char('\n'), QLatin1Char(' '));
    compact = compact.simplified();
    if (compact.size() > maxChars)
        compact = compact.left(maxChars) + QStringLiteral("...");
    return compact;
}

static QString jsonStringField(const QJsonObject& obj, const QString& key)
{
    return obj.value(key).toString().trimmed();
}

static QString extractLastUserMessage(const QJsonObject& request)
{
    const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
    for (int i = messages.size() - 1; i >= 0; --i) {
        const QJsonObject msg = messages.at(i).toObject();
        if (msg.value(QStringLiteral("role")).toString() != QLatin1String("user"))
            continue;
        return msg.value(QStringLiteral("content")).toString().trimmed();
    }
    return QString();
}

static QString extractAssistantMessage(const QJsonObject& response)
{
    const QJsonArray choices = response.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return QString();
    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    return message.value(QStringLiteral("content")).toString().trimmed();
}

static QString extractFinishReason(const QJsonObject& response)
{
    const QJsonArray choices = response.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return QString();
    return choices.first().toObject().value(QStringLiteral("finish_reason")).toString().trimmed();
}

static int extractResponseToolCallCount(const QJsonObject& response)
{
    const QJsonArray choices = response.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return 0;
    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    return message.value(QStringLiteral("tool_calls")).toArray().size();
}

static void appendJsonToItem(QTreeWidgetItem* item, const QJsonValue& value)
{
    if (!item)
        return;
    item->setText(1, jsonValueToString(value));
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QTreeWidgetItem* child = new QTreeWidgetItem(item);
            child->setText(0, it.key());
            appendJsonToItem(child, it.value());
        }
    } else if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QTreeWidgetItem* child = new QTreeWidgetItem(item);
            child->setText(0, QStringLiteral("[%1]").arg(i));
            appendJsonToItem(child, arr.at(i));
        }
    }
}

QString IdentityView::buildTurnListTitle(const QJsonObject& entry, int row) const
{
    if (entry.contains(QStringLiteral("event"))) {
        const QJsonObject eventObj = entry.value(QStringLiteral("event")).toObject();
        const QString eventType = eventObj.value(QStringLiteral("type")).toString().trimmed();
        const QString eventSummary = compactText(eventObj.value(QStringLiteral("summary")).toString(), 28);
        if (!eventSummary.isEmpty()) {
            return QStringLiteral("#%1 [EVT] %2: %3")
                .arg(row + 1)
                .arg(eventType.isEmpty() ? QStringLiteral("event") : eventType, eventSummary);
        }
        return QStringLiteral("#%1 [EVT] %2")
            .arg(row + 1)
            .arg(eventType.isEmpty() ? QStringLiteral("event") : eventType);
    }

    const QJsonObject request = entry.value(QStringLiteral("request")).toObject();
    const QJsonObject response = entry.value(QStringLiteral("response")).toObject();
    const bool hasError = entry.contains(QStringLiteral("error"));
    const int toolCalls = extractResponseToolCallCount(response);

    QString status = QStringLiteral("OK");
    if (hasError)
        status = QStringLiteral("ERR");
    else if (toolCalls > 0)
        status = QStringLiteral("TOOL");
    const QString userMsg = compactText(extractLastUserMessage(request), 34);
    if (userMsg.isEmpty())
        return QStringLiteral("#%1 [%2]").arg(row + 1).arg(status);
    return QStringLiteral("#%1 [%2] %3").arg(row + 1).arg(status, userMsg);
}

QString IdentityView::buildTurnSummaryText(const QJsonObject& entry, int row) const
{
    if (entry.contains(QStringLiteral("event"))) {
        const QJsonObject eventObj = entry.value(QStringLiteral("event")).toObject();
        const QString eventType = eventObj.value(QStringLiteral("type")).toString().trimmed();
        const QString requestId = jsonStringField(entry, QStringLiteral("request_id"));
        const QString traceId = eventObj.value(QStringLiteral("trace_id")).toString().trimmed();
        const QString turnId = eventObj.value(QStringLiteral("turn_id")).toString().trimmed();
        const QString runId = eventObj.value(QStringLiteral("run_id")).toString().trimmed();
        const QString errorMsg = eventObj.value(QStringLiteral("error")).toString().trimmed();

        QString text;
        text += QStringLiteral("事件 #%1\n").arg(row + 1);
        if (!requestId.isEmpty())
            text += QStringLiteral("request_id: %1\n").arg(requestId);
        text += QStringLiteral("type: %1\n")
                    .arg(eventType.isEmpty() ? QStringLiteral("event") : eventType);
        if (!traceId.isEmpty())
            text += QStringLiteral("trace_id: %1\n").arg(traceId);
        if (!turnId.isEmpty())
            text += QStringLiteral("turn_id: %1\n").arg(turnId);
        if (!runId.isEmpty())
            text += QStringLiteral("run_id: %1\n").arg(runId);
        if (!errorMsg.isEmpty())
            text += QStringLiteral("error: %1\n").arg(errorMsg);

        text += QStringLiteral("\n事件详情:\n");
        for (auto it = eventObj.constBegin(); it != eventObj.constEnd(); ++it) {
            if (it.key() == QLatin1String("type")
                || it.key() == QLatin1String("trace_id")
                || it.key() == QLatin1String("turn_id")
                || it.key() == QLatin1String("run_id")
                || it.key() == QLatin1String("error")) {
                continue;
            }
            QString valueText;
            if (it.value().isObject()) {
                valueText = QString::fromUtf8(
                    QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact));
            } else if (it.value().isArray()) {
                valueText = QString::fromUtf8(
                    QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Compact));
            } else {
                valueText = it.value().toVariant().toString();
            }
            text += QStringLiteral("- %1: %2\n").arg(it.key(), compactText(valueText, 280));
        }
        return text.trimmed();
    }

    const QJsonObject request = entry.value(QStringLiteral("request")).toObject();
    const QJsonObject response = entry.value(QStringLiteral("response")).toObject();
    const QJsonObject error = entry.value(QStringLiteral("error")).toObject();

    const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
    const QString requestId = jsonStringField(entry, QStringLiteral("request_id"));
    const QString model = jsonStringField(request, QStringLiteral("model"));
    const QString finishReason = extractFinishReason(response);
    const int requestToolCount = request.value(QStringLiteral("tools")).toArray().size();
    const int responseToolCount = extractResponseToolCallCount(response);
    const QString userMsg = extractLastUserMessage(request);
    const QString assistantMsg = extractAssistantMessage(response);
    const QString errorMsg = error.value(QStringLiteral("message")).toString().trimmed();

    QString text;
    text += QStringLiteral("回合 #%1\n").arg(row + 1);
    if (!requestId.isEmpty())
        text += QStringLiteral("request_id: %1\n").arg(requestId);
    if (!model.isEmpty())
        text += QStringLiteral("model: %1\n").arg(model);
    text += QStringLiteral("messages_count: %1\n").arg(messages.size());
    text += QStringLiteral("request_tools: %1\n").arg(requestToolCount);

    if (!errorMsg.isEmpty()) {
        text += QStringLiteral("status: ERROR\n");
        text += QStringLiteral("error: %1\n\n").arg(errorMsg);
    } else {
        text += QStringLiteral("status: %1\n")
                    .arg(finishReason.isEmpty() ? QStringLiteral("unknown") : finishReason);
        text += QStringLiteral("response_tool_calls: %1\n\n").arg(responseToolCount);
    }

    text += QStringLiteral("流程（详细数据）\n");
    text += QStringLiteral("1) System + 历史 + 本轮用户输入 -> 发送给模型\n");
    if (requestToolCount > 0)
        text += QStringLiteral("2) 本轮可用工具数: %1\n").arg(requestToolCount);
    if (responseToolCount > 0)
        text += QStringLiteral("3) 模型请求工具调用数: %1\n").arg(responseToolCount);
    text += QStringLiteral("\n本轮用户输入:\n%1\n\n")
                .arg(userMsg.isEmpty() ? QStringLiteral("(empty)") : userMsg);
    text += QStringLiteral("本轮模型输出:\n%1\n\n")
                .arg(assistantMsg.isEmpty() ? QStringLiteral("(empty)") : assistantMsg);

    text += QStringLiteral("请求消息序列:\n");
    for (int i = 0; i < messages.size(); ++i) {
        const QJsonObject msg = messages.at(i).toObject();
        const QString role = msg.value(QStringLiteral("role")).toString();
        const QString content = msg.value(QStringLiteral("content")).toString();
        text += QStringLiteral("[%1] %2\n").arg(role, compactText(content, 220));
    }
    return text.trimmed();
}

void IdentityView::renderRawEntry(const QJsonObject& entry, int row)
{
    m_historyDisplay->clear();
    QTreeWidgetItem* top = new QTreeWidgetItem(m_historyDisplay);
    top->setText(0, QString("第 %1 次").arg(row + 1));

    if (entry.contains(QStringLiteral("event"))) {
        QTreeWidgetItem* evtItem = new QTreeWidgetItem(top);
        evtItem->setText(0, QStringLiteral("Event"));
        appendJsonToItem(evtItem, entry.value(QStringLiteral("event")));
    }

    QTreeWidgetItem* reqItem = new QTreeWidgetItem(top);
    reqItem->setText(0, QStringLiteral("Request"));
    if (entry.contains(QStringLiteral("request")))
        appendJsonToItem(reqItem, entry.value(QStringLiteral("request")));
    else
        reqItem->setText(1, QStringLiteral("(missing)"));

    QTreeWidgetItem* respItem = new QTreeWidgetItem(top);
    respItem->setText(0, QStringLiteral("Response"));
    if (entry.contains(QStringLiteral("response")))
        appendJsonToItem(respItem, entry.value(QStringLiteral("response")));
    else
        respItem->setText(1, QStringLiteral("(pending)"));

    if (entry.contains(QStringLiteral("error"))) {
        QTreeWidgetItem* errItem = new QTreeWidgetItem(top);
        errItem->setText(0, QStringLiteral("Error"));
        appendJsonToItem(errItem, entry.value(QStringLiteral("error")));
    }

    m_historyDisplay->expandToDepth(2);
}

void IdentityView::updateHistoryDetailsForRow(int row)
{
    if (!m_historySummaryDisplay)
        return;
    if (row < 0 || row >= m_historyEntries.size()) {
        m_historySummaryDisplay->setPlainText(tr("请选择一条回合记录查看详情。"));
        m_historyDisplay->clear();
        return;
    }
    const QJsonObject entry = m_historyEntries.at(row).toObject();
    m_historySummaryDisplay->setPlainText(buildTurnSummaryText(entry, row));
    renderRawEntry(entry, row);
}

void IdentityView::onTurnSelectionChanged(int row)
{
    updateHistoryDetailsForRow(row);
}

void IdentityView::updateHistoryDisplayFrom(const QJsonArray& history)
{
    m_historyEntries = history;
    m_historyLabel->setText(QString("请求/响应历史 (共 %1 次)").arg(history.size()));
    if (m_turnList)
        m_turnList->clear();
    if (m_historyDisplay)
        m_historyDisplay->clear();
    if (m_historySummaryDisplay)
        m_historySummaryDisplay->clear();

    if (history.isEmpty()) {
        if (m_historySummaryDisplay)
            m_historySummaryDisplay->setPlainText(tr("暂无请求/响应记录。"));
        return;
    }

    if (m_turnList) {
        for (int i = 0; i < history.size(); ++i) {
            const QJsonObject entry = history.at(i).toObject();
            m_turnList->addItem(buildTurnListTitle(entry, i));
        }
        m_turnList->setCurrentRow(history.size() - 1);
    } else {
        updateHistoryDetailsForRow(history.size() - 1);
    }
}

void IdentityView::updateHistoryDisplay()
{
    QJsonArray ioH;
    AgentRuntime* runtime = m_chatService ? m_chatService->runtimeForSession(m_currentSessionId) : nullptr;
    if (runtime && runtime->currentSessionId() == m_currentSessionId)
        ioH = runtime->getIoHistory();
    updateHistoryDisplayFrom(ioH);
}

void IdentityView::onClearHistoryClicked()
{
    Session* session = SessionManager::instance()->findById(m_currentSessionId);
    if (session) {
        session->clearMessages();
    }

    AgentRuntime* runtime = m_chatService->runtimeForSession(m_currentSessionId);
    if (runtime && runtime->currentSessionId() == m_currentSessionId)
        runtime->clearHistory();

    m_historyEntries = QJsonArray();
    if (m_turnList)
        m_turnList->clear();
    m_historyDisplay->clear();
    if (m_historySummaryDisplay)
        m_historySummaryDisplay->setPlainText(tr("暂无请求/响应记录。"));
    m_historyLabel->setText(tr("请求/响应历史 (共 0 次)"));
    if (m_chatWidget)
        m_chatWidget->addMessage(makeSystemMessage(QStringLiteral("[对话历史已清空]")));
    if (m_chatService)
        m_chatService->saveSessionsToDisk();
}

// ==================== 语音（占位） ====================

void IdentityView::onVoiceStartRequested()
{
    if (m_chatWidget)
        m_chatWidget->addMessage(makeSystemMessage(QStringLiteral("[语音输入功能暂未接入]")));
}

void IdentityView::onVoiceStopRequested()
{
}

// ==================== 头像点击 ====================

void IdentityView::onAvatarClicked(const QString& sender, bool isMine, int row)
{
    ProfileWidget* profile = new ProfileWidget(this);
    profile->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    profile->setAttribute(Qt::WA_DeleteOnClose);
    profile->applyDefaultStyle();

    QString clickedSenderId;
    QString clickedDisplayName = sender.trimmed();
    QString clickedAvatarPath;
    if (m_chatWidget) {
        ChatWidgetModel* model = m_chatWidget->model();
        if (model && row >= 0 && row < model->rowCount()) {
            const QModelIndex idx = model->index(row, 0);
            if (idx.isValid()) {
                clickedSenderId = idx.data(ChatWidgetModel::ChatWidgetSenderIdRole).toString().trimmed();
                if (clickedDisplayName.isEmpty()) {
                    clickedDisplayName = idx.data(ChatWidgetModel::ChatWidgetSenderRole).toString().trimmed();
                }
                clickedAvatarPath = idx.data(ChatWidgetModel::ChatWidgetAvatarRole).toString().trimmed();
            }
        }
    }

    auto setProfileAvatar = [profile](const QString& avatarPath) {
        if (avatarPath.trimmed().isEmpty())
            return;
        QPixmap avatar(avatarPath);
        if (!avatar.isNull())
            profile->setAvatar(avatar);
    };

    // 优先用 senderId 判定，缺失时回退到 isMine 语义判断。
    bool isRealUser = false;
    if (clickedSenderId == QLatin1String("user")) {
        isRealUser = true;
    } else if (!clickedSenderId.isEmpty()) {
        Identity* clickedIdentity = IdentityManager::instance()->findById(clickedSenderId);
        isRealUser = clickedIdentity && clickedIdentity->isUser();
    } else {
        // isMine 的含义取决于视角：
        //   用户视角：isMine=true → 用户消息；Agent 视角：isMine=true → Agent 消息
        isRealUser = isUserView() ? isMine : !isMine;
    }

    if (isRealUser) {
        Identity* userIdentity = IdentityManager::instance()->userIdentity();
        profile->setUserName(userIdentity && !userIdentity->name().trimmed().isEmpty() ? userIdentity->name().trimmed() : QStringLiteral("我"));
        profile->setTmId(userIdentity ? userIdentity->id() : QStringLiteral("user"));
        if (!clickedAvatarPath.isEmpty())
            setProfileAvatar(clickedAvatarPath);
        else
            setProfileAvatar(identityAvatarPath(QStringLiteral("user")));
        profile->addDetailItem(QStringLiteral("角色"), QStringLiteral("用户"));
    } else {
        QString agentIdentityId = clickedSenderId;
        if (agentIdentityId.isEmpty() || agentIdentityId == QLatin1String("user"))
            agentIdentityId = streamAgentIdentityId(m_currentSessionId);
        if (agentIdentityId.isEmpty())
            agentIdentityId = m_identityId;

        Identity* agentIdentity = IdentityManager::instance()->findById(agentIdentityId);
        const QString agentName = !clickedDisplayName.isEmpty()
            ? clickedDisplayName
            : (agentIdentity && !agentIdentity->name().trimmed().isEmpty()
                   ? agentIdentity->name().trimmed()
                   : QStringLiteral("Agent"));

        profile->setUserName(agentName);
        profile->setTmId(agentIdentityId.isEmpty() ? QStringLiteral("agent") : agentIdentityId);
        if (!clickedAvatarPath.isEmpty())
            setProfileAvatar(clickedAvatarPath);
        else if (agentIdentity)
            setProfileAvatar(agentIdentity->avatar().trimmed());

        profile->addDetailItem(QStringLiteral("角色"), QStringLiteral("AI 助手"));
        profile->addSeparator();
        QString roleName = QStringLiteral("智能对话");
        QString modelInfo = QStringLiteral("默认模型");
        LLMConfig cfg;
        if (agentIdentity && agentIdentity->profile()) {
            IdentityProfile* idProfile = agentIdentity->profile();
            const QString desc = idProfile->description().trimmed();
            if (!desc.isEmpty())
                roleName = desc;
            cfg = idProfile->llmConfig();
        } else if (AgentRuntime* runtime = m_chatService->runtimeForSession(m_currentSessionId)) {
            cfg = runtime->config();
            Identity* runtimeIdentity = runtime->identity();
            if (runtimeIdentity && runtimeIdentity->profile()) {
                const QString desc = runtimeIdentity->profile()->description().trimmed();
                if (!desc.isEmpty())
                    roleName = desc;
            }
        }
        if (cfg.isValid()) {
            if (ModelFactory* factory = m_chatService ? m_chatService->modelFactory() : nullptr)
                modelInfo = factory->resolveModelId(cfg).trimmed();
            else
                modelInfo = cfg.selectedModelId.trimmed();
            if (modelInfo.trimmed().isEmpty())
                modelInfo = QStringLiteral("未指定模型");
        }
        profile->addDetailItem(QStringLiteral("岗位"), roleName);
        profile->addSeparator();
        profile->addDetailItem(QStringLiteral("模型"), modelInfo);
    }

    const QString sessionId = m_currentSessionId.trimmed();
    if (!sessionId.isEmpty()) {
        profile->addSeparator();
        profile->addDetailItem(QStringLiteral("会话ID"), sessionId);
        profile->addDetailItem(QStringLiteral("复制"), QStringLiteral("点击复制会话ID"), true);
        connect(profile, &ProfileWidget::detailItemClicked, profile, [sessionId](const QString& title) {
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

    QPoint pos = QCursor::pos();
    profile->move(pos.x() - profile->width() / 2, pos.y() - 20);
    profile->show();
}
