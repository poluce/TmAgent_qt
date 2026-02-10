#include "IdentityView.h"
#include "chat_widget.h"
#include "chat_widget_view.h"
#include "chat_widget_input.h"
#include "chat_list_widget.h"
#include "chat_list_view.h"
#include "chat_list_roles.h"
#include "profile_widget.h"
#include "core/service/ChatService.h"
#include "core/service/AgentRuntime.h"
#include "core/manager/SessionManager.h"
#include "core/manager/IdentityManager.h"
#include "core/model/Session.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "newCore/ModelFactory.h"
#include "newCore/LLMTypes.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTextEdit>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTime>
#include <QToolTip>
#include <QVBoxLayout>

namespace {
ChatWidget::MessageParams makeSystemMessage(const QString& content)
{
    ChatWidget::MessageParams params;
    params.content = content;
    params.senderId = QStringLiteral("system");
    params.displayName = QStringLiteral("System");
    return params;
}
} // namespace

// ==================== 构造函数 ====================

IdentityView::IdentityView(const QString& identityId,
                            ChatService* chatService,
                            QWidget* parent)
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

    const bool hasActiveSession =
        !m_currentSessionId.isEmpty() &&
        SessionManager::instance()->findById(m_currentSessionId) != nullptr;
    const bool canSendMessage =
        hasActiveSession &&
        m_chatService &&
        m_chatService->canIdentitySendMessage(m_identityId, m_currentSessionId);
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

    // --- 右侧：历史面板 ---
    QWidget* historyContainer = new QWidget(this);
    QVBoxLayout* historyLayout = new QVBoxLayout(historyContainer);
    historyLayout->setContentsMargins(0, 0, 0, 0);

    m_historyLabel = new QLabel(tr("请求/响应历史 (共 0 次)"), this);
    QFont labelFont = m_historyLabel->font();
    labelFont.setBold(true);
    m_historyLabel->setFont(labelFont);
    historyLayout->addWidget(m_historyLabel);

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
    historyLayout->addWidget(m_historyDisplay, 1);

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
    connect(m_chatWidget, &ChatWidget::messageSent, this, &IdentityView::onUserMessageSent);
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
        updateHistoryDisplayFrom(session ? session->ioHistory() : QJsonArray());
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
        m_chatWidget->setCurrentUser(QStringLiteral("user"), QStringLiteral("Me"));
    } else {
        Identity* identity = IdentityManager::instance()->findById(m_identityId);
        QString agentName = identity ? identity->name() : QStringLiteral("Agent");
        m_chatWidget->setCurrentUser(m_identityId, agentName);

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
    m_hasPendingStreamMsg = false;
    m_pendingStreamMsgRow = -1;
    if (m_chatWidget)
        m_chatWidget->clearStreamTargetRow();
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
            // 用户视角：保留门控但按钮始终显示"发送"
            if (isUserView()) {
                if (auto* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget()))
                    input->setSendingState(false);
            }
            const Session::StreamState& state = session->streamState();
            if (!state.buffer.isEmpty()) {
                // 添加一个占位消息并填入已有 buffer
                QString agentName = m_chatService->agentDisplayNameForSession(m_currentSessionId);
                ChatWidget::MessageParams params;
                params.content = QString();
                params.senderId = m_identityId;
                params.displayName = agentName;
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
    m_hasPendingStreamMsg = false;
    m_pendingStreamMsgRow = -1;
    if (m_chatWidget)
        m_chatWidget->clearStreamTargetRow();
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
        if (row >= 0) {
            QAbstractItemModel* model = m_chatListWidget->listView()->model();
            QModelIndex sel;
            if (QSortFilterProxyModel* proxy = qobject_cast<QSortFilterProxyModel*>(model))
                sel = proxy->mapFromSource(m_chatListWidget->listView()->standardModel()->index(row, 0));
            else if (model)
                sel = model->index(row, 0);
            if (sel.isValid())
                m_chatListWidget->listView()->setCurrentIndex(sel);
        }
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
    m_chatListWidget->updateChatItem(row, name, shortPreview,
                                     QTime::currentTime().toString(QStringLiteral("hh:mm")),
                                     QColor(Qt::gray), 0);
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

// ==================== UI 辅助 ====================

void IdentityView::updateSendingState()
{
    if (!m_chatWidget)
        return;
    bool sending = m_chatService->isSessionStreaming(m_currentSessionId);
    m_chatWidget->setSendingState(sending);

    // 用户视角：保留 ChatWidget::m_isSending 门控，但按钮始终显示"发送"
    if (isUserView() && sending) {
        if (auto* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget()))
            input->setSendingState(false);
    }
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
        if (msg.content.text.trimmed().isEmpty())
            continue;

        ChatWidget::MessageParams params;
        params.content = msg.content.text;

        if (msg.content.type == MessageContent::Type::System || msg.senderId == QLatin1String("system")) {
            params.senderId = QStringLiteral("system");
            params.displayName = QStringLiteral("System");
        } else if (msg.senderId == m_identityId) {
            Identity* selfIdentity = IdentityManager::instance()->findById(m_identityId);
            if (selfIdentity && selfIdentity->isUser()) {
                params.senderId = QStringLiteral("user");
                params.displayName = QStringLiteral("用户");
            } else {
                params.senderId = m_identityId;
                params.displayName = selfIdentity ? selfIdentity->name() : QStringLiteral("Me");
            }
        } else {
            Identity* senderIdentity = IdentityManager::instance()->findById(msg.senderId);
            if (senderIdentity && senderIdentity->isUser()) {
                params.senderId = QStringLiteral("user");
                params.displayName = QStringLiteral("用户");
            } else if (senderIdentity) {
                params.senderId = senderIdentity->id();
                params.displayName = senderIdentity->name().trimmed().isEmpty()
                    ? QStringLiteral("Agent")
                    : senderIdentity->name().trimmed();
            } else {
                params.senderId = QStringLiteral("user");
                params.displayName = QStringLiteral("用户");
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
        params.content = content;
        if (role == QLatin1String("user")) {
            params.senderId = QStringLiteral("user");
            params.displayName = QStringLiteral("用户");
        } else {
            params.senderId = m_identityId;
            params.displayName = assistantName;
        }
        m_chatWidget->addMessage(params);
    }
}

// ==================== 会话操作 ====================

void IdentityView::onNewChatRequested()
{
    if (!m_chatService || !m_chatService->canIdentityManageSessions(m_identityId))
        return;

    if (m_chatWidget) {
        m_chatWidget->setEmptyStateVisible(false);
        syncInputAvailability();
        clearChatMessages();
    }
    updateSendingState();

    Session* session = nullptr;
    session = m_chatService->createSessionForIdentityAs(m_identityId, m_identityId, tr("新对话"));

    if (!session)
        return;

    m_currentSessionId = session->id();
    // createNewSession 内部会 emit sessionCreated → MainWindow::onSessionCreated
    // → reloadSessionList()，所以这里不再手动 addChatItem，只需选中最后一项

    int row = rowForSessionId(m_currentSessionId);
    if (row >= 0) {
        QAbstractItemModel* model = m_chatListWidget->listView()->model();
        QModelIndex sel;
        if (QSortFilterProxyModel* proxy = qobject_cast<QSortFilterProxyModel*>(model))
            sel = proxy->mapFromSource(m_chatListWidget->listView()->standardModel()->index(row, 0));
        else if (model)
            sel = model->index(row, 0);
        if (sel.isValid())
            m_chatListWidget->listView()->setCurrentIndex(sel);
    }

    updateHistoryDisplay();
    updateSendingState();
    m_chatService->saveSessionsToDisk();
}

void IdentityView::onChatItemActivated(const QString& name, const QString& message, const QString& time,
                                        const QColor& avatarColor, int unreadCount)
{
    Q_UNUSED(message); Q_UNUSED(time); Q_UNUSED(avatarColor); Q_UNUSED(unreadCount); Q_UNUSED(name);
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
    updateHistoryDisplayFrom(session ? session->ioHistory() : QJsonArray());
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
    // 用户视角：保留门控但按钮始终显示"发送"
    if (isUserView()) {
        if (auto* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget()))
            input->setSendingState(false);
    }
    if (!m_hasPendingStreamMsg) {
        QString agentName = m_chatService->agentDisplayNameForSession(sessionId);
        ChatWidget::MessageParams params;
        params.content = QString();
        params.senderId = m_identityId;
        params.displayName = agentName;
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
    m_chatService->saveSessionsToDisk();

    if (!m_isActive || !m_chatWidget || sessionId != m_currentSessionId) {
        m_hasPendingStreamMsg = false;
        m_pendingStreamMsgRow = -1;
        if (m_chatWidget)
            m_chatWidget->clearStreamTargetRow();
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
        ChatWidget::MessageParams params;
        params.content = fullContent;
        params.senderId = m_identityId;
        params.displayName = agentName;
        m_chatWidget->addMessage(params);
    }
    m_hasPendingStreamMsg = false;
    m_pendingStreamMsgRow = -1;
    m_chatWidget->clearStreamTargetRow();
    updateSendingState();
    updateHistoryDisplay();
}

void IdentityView::handleError(const QString& sessionId, const QString& errorMsg)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    m_hasPendingStreamMsg = false;
    m_pendingStreamMsgRow = -1;
    if (m_chatWidget)
        m_chatWidget->clearStreamTargetRow();
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

    m_hasPendingStreamMsg = false;
    m_pendingStreamMsgRow = -1;
    if (m_chatWidget)
        m_chatWidget->clearStreamTargetRow();

    if (m_isActive && sessionId == m_currentSessionId)
        updateHistoryDisplay();
}

void IdentityView::handleToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    Q_UNUSED(sessionId);
    Q_UNUSED(event);
}

// ==================== 历史面板 ====================

static QString jsonValueToString(const QJsonValue& value)
{
    switch (value.type()) {
    case QJsonValue::Null:      return QStringLiteral("null");
    case QJsonValue::Bool:      return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double:    return QString::number(value.toDouble());
    case QJsonValue::String:    return value.toString();
    case QJsonValue::Array:     return QStringLiteral("[%1]").arg(value.toArray().size());
    case QJsonValue::Object:    return QStringLiteral("{%1}").arg(value.toObject().size());
    case QJsonValue::Undefined: return QStringLiteral("undefined");
    }
    return QString();
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

void IdentityView::updateHistoryDisplayFrom(const QJsonArray& history)
{
    m_historyLabel->setText(QString("请求/响应历史 (共 %1 次)").arg(history.size()));
    m_historyDisplay->clear();
    if (history.isEmpty())
        return;

    for (int i = 0; i < history.size(); ++i) {
        const QJsonObject entry = history.at(i).toObject();
        QTreeWidgetItem* top = new QTreeWidgetItem(m_historyDisplay);
        top->setText(0, QString("第 %1 次").arg(i + 1));

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
    }
    m_historyDisplay->expandToDepth(1);
}

void IdentityView::updateHistoryDisplay()
{
    Session* session = SessionManager::instance()->findById(m_currentSessionId);
    QJsonArray ioH = session ? session->ioHistory() : QJsonArray();
    updateHistoryDisplayFrom(ioH);
}

void IdentityView::onClearHistoryClicked()
{
    Session* session = SessionManager::instance()->findById(m_currentSessionId);
    if (session) {
        session->setIoHistory(QJsonArray());
        session->clearMessages();
    }

    AgentRuntime* runtime = m_chatService->runtimeForSession(m_currentSessionId);
    if (runtime && runtime->currentSessionId() == m_currentSessionId)
        runtime->clearHistory();

    m_historyDisplay->clear();
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
    Q_UNUSED(row);

    ProfileWidget* profile = new ProfileWidget(this);
    profile->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    profile->setAttribute(Qt::WA_DeleteOnClose);
    profile->applyDefaultStyle();

    // isMine 的含义取决于视角：
    //   用户视角：isMine=true → 用户消息；Agent 视角：isMine=true → Agent 消息
    // 统一判断：点击的是否为"真实用户"
    bool isRealUser = isUserView() ? isMine : !isMine;

    if (isRealUser) {
        profile->setUserName(QStringLiteral("我"));
        profile->setTmId(QStringLiteral("user"));
        profile->addDetailItem(QStringLiteral("角色"), QStringLiteral("用户"));
    } else {
        profile->setUserName(sender.isEmpty() ? QStringLiteral("Agent") : sender);
        profile->setTmId(QStringLiteral("agent"));
        profile->addDetailItem(QStringLiteral("角色"), QStringLiteral("AI 助手"));
        profile->addSeparator();
        AgentRuntime* runtime = m_chatService->runtimeForSession(m_currentSessionId);
        QString roleName = QStringLiteral("智能对话");
        QString modelInfo = QStringLiteral("默认模型");
        if (runtime) {
            Identity* runtimeIdentity = runtime->identity();
            LLMConfig cfg = runtime->config();
            if (runtimeIdentity && runtimeIdentity->profile()) {
                IdentityProfile* idProfile = runtimeIdentity->profile();
                const QString desc = idProfile->description().trimmed();
                if (!desc.isEmpty())
                    roleName = desc;
                cfg = idProfile->llmConfig();
            }
            modelInfo = ModelFactory::modelIdToString(cfg.model);
            if (modelInfo.isEmpty() || cfg.model == ModelId::Unknown)
                modelInfo = QStringLiteral("默认模型");
            else if (cfg.model == ModelId::Custom && !cfg.customModelId.isEmpty())
                modelInfo = cfg.customModelId;
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
