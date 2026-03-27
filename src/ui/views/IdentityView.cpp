#include "IdentityView.h"
#include "AvatarUtils.h"
#include "ChatListUiSupport.h"
#include "ChatUiFlowSupport.h"
#include "HistoryUiSupport.h"
#include "ProfileUiSupport.h"
#include "SessionUiSupport.h"
#include "chat_list_roles.h"
#include "chat_list_view.h"
#include "chat_list_widget.h"
#include "chat_widget.h"
#include "chat_widget_input.h"
#include "chat_widget_model.h"
#include "chat_widget_view.h"
#include "ThinkingIndicatorWidget.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/Session.h"
#include "llm/LLMTypes.h"
#include "profile_widget.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {
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

IdentityView::IdentityView(const QString& identityId, IAppFacade& app, QWidget* parent)
    : QWidget(parent)
    , m_identityId(identityId)
    , m_app(app)
    , m_workspace(&app.workspace())
    , m_conversation(&app.conversation())
    , m_governance(&app.governance())
    , m_memory(&app.memory())
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
    const bool canSendMessage = hasActiveSession
        && m_workspace
        && m_workspace->canIdentitySendMessage(m_identityId, m_currentSessionId);
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
    const bool canManageSessions = m_workspace
        && m_workspace->canIdentityManageSessions(m_identityId);
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

    m_thinkingIndicator = new ThinkingIndicatorWidget(centerContainer);
    m_thinkingIndicator->hide();

    splitter->addWidget(centerContainer);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes(QList<int>() << 300 << 980);

    mainLayout->addWidget(splitter);

    // 连接 UI 信号
    connect(m_chatWidget, &ChatWidget::messageSent, this, &IdentityView::onUserMessageSent);
    connect(m_chatWidget, &ChatWidget::messageActionRequested, this, &IdentityView::onMessageActionRequested);
    const bool canSendMessage = m_workspace
        && m_workspace->canIdentitySendMessage(m_identityId);
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
        const int row = ChatListUiSupport::sourceRowForIndex(m_chatListWidget, current);
        if (row < 0)
            return;
        QString sessionId = sessionIdForRow(row);

        // 延迟到下一事件循环执行，避免在列表 currentChanged 回调中阻塞主线程。
        QTimer::singleShot(0, this, [this, sessionId]() {
            switchToSessionView(sessionId, true);
        });
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

        // 如果当前 Session 正在流式输出，恢复流式渲染状态
        if (session && session->isStreaming()) {
            m_chatWidget->setSendingState(true);
            applyUserSendingOverride();
            const Session::StreamState& state = session->streamState();
            if (!state.buffer.isEmpty()) {
                // 添加一个占位消息并填入已有 buffer
                QString agentName = m_workspace
                    ? m_workspace->agentDisplayNameForSession(m_currentSessionId)
                    : QString();
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
    ChatListUiSupport::selectSourceRow(m_chatListWidget, row);
}

void IdentityView::showSessionInView(Session* session, bool deferHistoryRefresh)
{
    if (!session || !m_chatWidget)
        return;
    Q_UNUSED(deferHistoryRefresh);

    ChatUiFlowSupport::activateConversationView(
        m_chatWidget,
        [this, session]() {
            syncInputAvailability();
            restoreChatFromSession(session);
        },
        [this]() { updateSendingState(); });
}

void IdentityView::clearCurrentSessionView()
{
    m_currentSessionId.clear();
    ChatListUiSupport::clearCurrentSelection(m_chatListWidget);
    ChatUiFlowSupport::clearConversationView(m_chatWidget, [this]() { clearChatMessages(); });
    syncInputAvailability();
}

bool IdentityView::switchToSessionView(const QString& sessionId, bool deferHistoryRefresh)
{
    if (!m_chatWidget || sessionId.isEmpty() || sessionId == m_currentSessionId)
        return false;

    Session* session = SessionUiSupport::activateSession(m_workspace, sessionId, &m_currentSessionId);
    if (!session)
        return false;

    showSessionInView(session, deferHistoryRefresh);
    if (m_workspace)
        m_workspace->saveSessionsToDisk();
    return true;
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
        if (row >= 0) {
            if (!avatarPath.isEmpty())
                m_chatListWidget->updateChatItemData(row, ChatListAvatarPathRole, avatarPath);
            applyHeartbeatDecoration(s, row);
        }
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

void IdentityView::refreshSessionContent(const QString& sessionId)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;
    if (!m_isActive || sessionId != m_currentSessionId)
        return;
    Session* session = SessionManager::instance()->findById(sessionId);
    if (session)
        restoreChatFromSession(session);
}

void IdentityView::refreshSessionHeartbeatBadges()
{
    if (!m_chatListWidget)
        return;

    for (int row = 0; row < m_filteredSessionIds.size(); ++row) {
        Session* session = SessionManager::instance()->findById(m_filteredSessionIds.at(row));
        applyHeartbeatDecoration(session, row);
    }
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
    ChatListUiSupport::updateChatItemPreview(
        m_chatListWidget,
        row,
        name,
        preview,
        QTime::currentTime().toString(QStringLiteral("hh:mm")),
        sessionAvatarPath(session));
    applyHeartbeatDecoration(session, row);
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
    if (m_conversation) {
        const QString runtimeIdentityId = m_conversation->runtimeIdentityIdForSession(sessionId).trimmed();
        if (!runtimeIdentityId.isEmpty())
            return runtimeIdentityId;
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

QString IdentityView::sessionHeartbeatAgentId(Session* session) const
{
    if (!session)
        return QString();

    if (!isUserView())
        return m_identityId;

    const QStringList participants = session->participantIds();
    for (const QString& pid : participants) {
        if (pid == m_identityId)
            continue;
        Identity* identity = IdentityManager::instance()->findById(pid);
        if (identity && identity->isAgent())
            return pid;
    }
    return QString();
}

void IdentityView::applyHeartbeatDecoration(Session* session, int row)
{
    if (!m_chatListWidget || row < 0)
        return;

    const QString agentId = sessionHeartbeatAgentId(session);
    if (agentId.isEmpty()) {
        m_chatListWidget->updateChatItemData(row, ChatListHeartbeatStateRole, QString());
        return;
    }

    bool enabled = false;
    if (m_memory)
        enabled = m_memory->heartbeatPolicyForAgent(agentId).enabled;

    bool ok = false;
    QJsonObject state;
    if (m_memory)
        state = m_memory->loadHeartbeatRuntimeState(agentId, &ok);

    const QString providerState = state.value(QStringLiteral("provider_state")).toString().trimmed();
    const QString laneState = state.value(QStringLiteral("lane_state")).toString().trimmed();
    const bool hasPendingTicket = state.value(QStringLiteral("has_pending_ticket")).toBool(false);
    const bool hasRecentState =
        !state.value(QStringLiteral("last_completed_at_utc")).toString().trimmed().isEmpty();

    QString heartbeatState;
    if (!enabled)
        heartbeatState = QStringLiteral("disabled");
    else if (providerState.compare(QStringLiteral("down"), Qt::CaseInsensitive) == 0)
        heartbeatState = QStringLiteral("down");
    else if (laneState == QLatin1String("running") || laneState == QLatin1String("escalating"))
        heartbeatState = QStringLiteral("busy");
    else if (hasPendingTicket || laneState == QLatin1String("deferred"))
        heartbeatState = QStringLiteral("active");
    else if (ok && hasRecentState)
        heartbeatState = QStringLiteral("active");
    else
        heartbeatState = QStringLiteral("idle");

    QStringList tooltipLines;
    tooltipLines << sessionDisplayName(session);
    tooltipLines << tr("心跳: %1").arg(enabled ? tr("已启用") : tr("未启用"));
    if (ok && !state.isEmpty()) {
        tooltipLines << tr("Lane: %1").arg(laneState.isEmpty() ? tr("未知") : laneState);
        tooltipLines << tr("待处理票据: %1").arg(hasPendingTicket ? tr("有") : tr("无"));
        tooltipLines << tr("Provider 状态: %1")
                            .arg(providerState.isEmpty() ? tr("未知") : providerState);
        const QString reason = state.value(QStringLiteral("last_deferred_reason")).toString().trimmed();
        if (!reason.isEmpty())
            tooltipLines << tr("最近原因: %1").arg(reason);
        const QString snapshot = state.value(QStringLiteral("last_completed_at_utc")).toString().trimmed();
        if (!snapshot.isEmpty())
            tooltipLines << tr("最近完成: %1").arg(snapshot);
    }

    m_chatListWidget->updateChatItemData(row, ChatListHeartbeatStateRole, heartbeatState);
    m_chatListWidget->updateChatItemData(row, ChatListHeartbeatTooltipRole, tooltipLines.join(QStringLiteral("\n")));
    m_chatListWidget->updateChatItemData(row, Qt::ToolTipRole, tooltipLines.join(QStringLiteral("\n")));
}

// ==================== UI 辅助 ====================

void IdentityView::updateSendingState()
{
    if (!m_chatWidget)
        return;
    const bool sending = m_conversation
        && m_conversation->isSessionStreaming(m_currentSessionId);
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

    const QList<Message> allMessages = session->allMessages();
    if (allMessages.isEmpty()) {
        clearChatMessages();
        return;
    }

    HistoryUiSupport::SessionRestoreOptions options;
    options.viewerIdentityId = m_identityId;
    options.userAvatarPath = identityAvatarPath(QStringLiteral("user"));
    options.filterHeartbeatMessages = true;
    options.requireCompleteFilePayload = true;
    options.maxRestoreMessages = 300;
    options.identityResolver = [](const QString& senderId) {
        return IdentityManager::instance()->findById(senderId);
    };

    const QList<ChatWidget::HistoryMessage> historyMessages =
        HistoryUiSupport::buildSessionHistoryMessages(allMessages, options);

    clearChatMessages();
    if (historyMessages.isEmpty()) {
        return;
    }
    m_chatWidget->setHistoryMessages(historyMessages, true);
}

void IdentityView::restoreChatFromHistory(const QJsonArray& history)
{
    if (!m_chatWidget)
        return;
    clearChatMessages();
    const QString assistantId = streamAgentIdentityId(m_currentSessionId);
    HistoryUiSupport::RawHistoryRestoreOptions options;
    options.fallbackAssistantSenderId = assistantId.isEmpty() ? m_identityId : assistantId;
    options.assistantDisplayName = m_workspace
        ? m_workspace->agentDisplayNameForSession(m_currentSessionId)
        : QString();
    options.assistantAvatarPath = identityAvatarPath(options.fallbackAssistantSenderId);
    options.userAvatarPath = identityAvatarPath(QStringLiteral("user"));
    const QList<ChatWidget::HistoryMessage> historyMessages =
        HistoryUiSupport::buildRawHistoryMessages(history, options);

    if (historyMessages.isEmpty()) {
        return;
    }
    m_chatWidget->setHistoryMessages(historyMessages, true);
}

// ==================== 会话操作 ====================

void IdentityView::onNewChatRequested()
{
    if (!m_workspace || !m_workspace->canIdentityManageSessions(m_identityId))
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
        session = m_workspace
            ? m_workspace->createSessionForIdentityAs(m_identityId, agentIdentityId, tr("新对话"))
            : nullptr;
    }

    if (!session)
        return;

    SessionUiSupport::activateCreatedSession(m_workspace, session, &m_currentSessionId);
    reloadSessionList();
    selectSessionRow(rowForSessionId(m_currentSessionId));
    showSessionInView(session, false);
    if (m_workspace)
        m_workspace->saveSessionsToDisk();
}

void IdentityView::onChatItemActivated(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount)
{
    Q_UNUSED(message);
    Q_UNUSED(time);
    Q_UNUSED(avatarColor);
    Q_UNUSED(unreadCount);
    Q_UNUSED(name);
    const int row = ChatListUiSupport::currentSourceRow(m_chatListWidget);
    if (row < 0)
        return;

    switchToSessionView(sessionIdForRow(row), false);
}

void IdentityView::onChatItemRemoved(int row)
{
    QString sessionId = sessionIdForRow(row);
    if (sessionId.isEmpty())
        return;

    if (!m_workspace
        || !m_workspace->removeSessionAs(m_identityId, sessionId)) {
        reloadSessionList();
        return;
    }
    const SessionUiSupport::RemoveSessionResult removeResult =
        SessionUiSupport::removeSession(m_workspace, sessionId, m_currentSessionId, m_identityId);
    if (removeResult == SessionUiSupport::RemoveSessionResult::Failed) {
        reloadSessionList();
        return;
    }
    m_filteredSessionIds.removeAll(sessionId);

    if (removeResult == SessionUiSupport::RemoveSessionResult::RemovedCurrent)
        clearCurrentSessionView();
    updateSendingState();
    if (m_workspace)
        m_workspace->saveSessionsToDisk();
}

void IdentityView::onChatItemRenamed(int row, const QString& name)
{
    if (!m_workspace || !m_workspace->canIdentityManageSessions(m_identityId)) {
        reloadSessionList();
        return;
    }

    QString sessionId = sessionIdForRow(row);
    if (sessionId.isEmpty())
        return;
    SessionUiSupport::renameSessionAndRuntime(m_conversation, sessionId, name);
    if (m_workspace)
        m_workspace->saveSessionsToDisk();
}

void IdentityView::onRemoveCurrentChatRequested()
{
    if (!m_workspace || !m_workspace->canIdentityManageSessions(m_identityId))
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
    const QString turnId = m_conversation
        ? m_conversation->enqueueUserMessageAs(m_identityId, m_currentSessionId, prompt)
        : QString();
    if (turnId.isEmpty()) {
        if (!m_workspace
            || !m_workspace->canIdentitySendMessage(m_identityId, m_currentSessionId))
            ChatUiFlowSupport::appendSystemMessage(m_chatWidget, QStringLiteral("[当前视角无发送权限]"));
        return;
    }
}

void IdentityView::onAbortClicked()
{
    qDebug() << "IdentityView: [Signal Received] Stop requested by User UI";

    const bool wasStreaming = m_conversation
        && m_conversation->isSessionStreaming(m_currentSessionId);
    QString rolledBackUserMsg = m_conversation
        ? m_conversation->abortAndRollback(m_currentSessionId)
        : QString();
    ChatUiFlowSupport::finalizeAbortUi(
        m_chatWidget,
        wasStreaming,
        rolledBackUserMsg,
        [this]() {
            ChatUiFlowSupport::appendSystemMessage(m_chatWidget, QStringLiteral("[已手动中断]"));
        },
        [this]() { updateSendingState(); },
        m_thinkingIndicator);
}

void IdentityView::onMessageActionRequested(const QString& action, const QString& messageId, const QString& content)
{
    if (action != QLatin1String("remember"))
        return;
    if (!m_memory || m_currentSessionId.trimmed().isEmpty())
        return;

    QString err;
    const bool ok = m_memory->rememberMessageAs(m_identityId, m_currentSessionId, messageId, content, &err);
    if (ok) {
        ChatUiFlowSupport::appendSystemMessage(m_chatWidget, QStringLiteral("[已加入长期记忆]"));
        return;
    }

    const QString msg = err.trimmed().isEmpty()
        ? QStringLiteral("[记忆失败]")
        : QStringLiteral("[记忆失败] %1").arg(err.trimmed());
    ChatUiFlowSupport::appendSystemMessage(m_chatWidget, msg);
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
    ChatUiFlowSupport::appendStreamingDelta(m_chatWidget, data, [this, sessionId]() {
        if (m_hasPendingStreamMsg)
            return;
        const QString agentName = m_workspace
            ? m_workspace->agentDisplayNameForSession(sessionId)
            : QString();
        const QString agentId = streamAgentIdentityId(sessionId);
        const QString senderId = agentId.isEmpty() ? m_identityId : agentId;
        m_pendingStreamMsgRow = ChatUiFlowSupport::appendStreamingPlaceholder(
            m_chatWidget,
            senderId,
            agentName,
            identityAvatarPath(senderId));
        m_hasPendingStreamMsg = (m_pendingStreamMsgRow >= 0);
    });
}

void IdentityView::handleFinished(const QString& sessionId, const QString& fullContent)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    if (!fullContent.isEmpty())
        updateChatListItem(sessionId, fullContent);

    if (!m_isActive || !m_chatWidget || sessionId != m_currentSessionId) {
        resetStreamState();
        ChatUiFlowSupport::finalizeUiUpdate([this]() { updateSendingState(); },
                                            sessionId == m_currentSessionId ? m_thinkingIndicator : nullptr);
        return;
    }

    QString agentName = m_workspace
        ? m_workspace->agentDisplayNameForSession(sessionId)
        : QString();
    const QString agentId = streamAgentIdentityId(sessionId);
    const QString senderId = agentId.isEmpty() ? m_identityId : agentId;
    ChatUiFlowSupport::completeStreamingResponse(
        m_chatWidget,
        ChatUiFlowSupport::StreamCompletionMode::UpdatePlaceholder,
        m_hasPendingStreamMsg,
        m_pendingStreamMsgRow,
        fullContent,
        senderId,
        agentName,
        identityAvatarPath(senderId));
    resetStreamState();
    ChatUiFlowSupport::finalizeUiUpdate([this]() { updateSendingState(); },
                                        m_thinkingIndicator);
}

void IdentityView::handleError(const QString& sessionId, const QString& errorMsg)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    ChatUiFlowSupport::finalizeErrorUi(
        [this]() { resetStreamState(); },
        [this, sessionId, errorMsg]() {
            if (m_isActive && m_chatWidget && sessionId == m_currentSessionId) {
                ChatUiFlowSupport::appendSystemMessage(
                    m_chatWidget,
                    QString::fromUtf8("❌ 错误: %1").arg(errorMsg));
            }
        },
        [this, sessionId]() {
            updateSendingState();
        },
        sessionId == m_currentSessionId ? m_thinkingIndicator : nullptr);
}

void IdentityView::handleToolCallsStarted(const QString& sessionId)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;
    if (!m_isActive || sessionId != m_currentSessionId) {
        resetStreamState();
        return;
    }
    ChatUiFlowSupport::beginToolPhase(
        [this]() { resetStreamState(); },
        []() {},
        m_thinkingIndicator);
}

void IdentityView::handleToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;
    if (!m_isActive || sessionId != m_currentSessionId || !m_chatWidget)
        return;

    const QString agentId = streamAgentIdentityId(sessionId);
    const QString senderId = agentId.isEmpty() ? m_identityId : agentId;
    ChatUiFlowSupport::appendSendFileToolResult(
        m_chatWidget,
        event,
        senderId,
        m_workspace
            ? m_workspace->agentDisplayNameForSession(sessionId)
            : QString(),
        identityAvatarPath(senderId),
        false);
}

void IdentityView::handleReasoningStarted(const QString& sessionId)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    if (!m_isActive || sessionId != m_currentSessionId)
        return;
    ChatUiFlowSupport::beginReasoningPhase(m_thinkingIndicator);
}

void IdentityView::handleReasoningStopped(const QString& sessionId)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    if (!m_isActive || sessionId != m_currentSessionId)
        return;
    ChatUiFlowSupport::hideThinkingIndicator(m_thinkingIndicator);
}

// ==================== 语音（占位） ====================

void IdentityView::onVoiceStartRequested()
{
    ChatUiFlowSupport::appendSystemMessage(m_chatWidget, QStringLiteral("[语音输入功能暂未接入]"));
}

void IdentityView::onVoiceStopRequested()
{
}

// ==================== 头像点击 ====================

void IdentityView::onAvatarClicked(const QString& sender, bool isMine, int row)
{
    ProfileWidget* profile = ProfileUiSupport::createProfilePopup(this);

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
        ProfileUiSupport::populateUserProfile(
            profile,
            userIdentity && !userIdentity->name().trimmed().isEmpty() ? userIdentity->name().trimmed() : QStringLiteral("我"),
            userIdentity ? userIdentity->id() : QStringLiteral("user"),
            !clickedAvatarPath.isEmpty() ? clickedAvatarPath : identityAvatarPath(QStringLiteral("user")));
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
        const ProfileUiSupport::AgentProfileInfo profileInfo =
            ProfileUiSupport::resolveAgentProfileInfo(m_conversation, agentIdentity, m_currentSessionId);
        ProfileUiSupport::populateAgentProfile(
            profile,
            agentName,
            agentIdentityId.isEmpty() ? QStringLiteral("agent") : agentIdentityId,
            profileInfo.roleName,
            profileInfo.modelInfo,
            !clickedAvatarPath.isEmpty()
                ? clickedAvatarPath
                : (agentIdentity ? agentIdentity->avatar().trimmed() : QString()));
    }

    ProfileUiSupport::attachSessionCopyAction(profile, m_currentSessionId.trimmed());

    // 模型切换：点击"模型"项弹出选择菜单
    if (m_governance) {
        const QString sessionId = m_currentSessionId;
        connect(profile, &ProfileWidget::detailItemClicked, profile,
            [profile, this, sessionId](const QString& title) {
                if (title != QStringLiteral("模型"))
                    return;
                QMenu menu(profile);
                const QStringList instanceIds = m_governance->enabledProviderInstanceIds();
                for (const QString& instanceId : instanceIds) {
                    const QString providerName = m_governance->displayNameForProviderInstance(instanceId);
                    const QList<AvailableModel> models = m_governance->cachedModelsForProviderInstance(instanceId);
                    if (models.isEmpty()) {
                        QAction* action = menu.addAction(providerName.isEmpty() ? instanceId : providerName);
                        action->setData(instanceId);
                    } else {
                        QMenu* sub = menu.addMenu(providerName.isEmpty() ? instanceId : providerName);
                        for (const AvailableModel& m : models) {
                            QAction* action = sub->addAction(m.displayName.isEmpty() ? m.modelId : m.displayName);
                            action->setData(QStringList{instanceId, m.modelId});
                        }
                    }
                }
                if (menu.isEmpty())
                    return;
                QAction* chosen = menu.exec(QCursor::pos());
                if (!chosen)
                    return;
                const QStringList data = chosen->data().toStringList();
                LLMConfig newConfig;
                if (data.size() >= 2) {
                    newConfig.providerInstanceId = data.at(0);
                    newConfig.selectedModelId = data.at(1);
                } else {
                    newConfig.providerInstanceId = chosen->data().toString();
                }
                m_governance->setDefaultAgentConfig(newConfig);
                m_governance->applyConfigToAllRuntimes();
                profile->close();
            });
    }

    ProfileUiSupport::showProfilePopup(profile);
}
