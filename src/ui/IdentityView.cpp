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
#include <QDebug>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTime>
#include <QVBoxLayout>

namespace {
ChatWidget::MessageParams makeMessageParams(const QString& content, bool isMine, const QString& senderName)
{
    ChatWidget::MessageParams params;
    params.content = content;
    params.isMine = isMine;
    params.senderId = isMine ? QStringLiteral("user") : senderName;
    params.displayName = senderName;
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
    m_chatListWidget->addHeaderAction(tr("新会话"), QStringLiteral("new_chat"));
    m_chatListWidget->addHeaderAction(tr("删除"), QStringLiteral("remove_current"));
    leftLayout->addWidget(m_chatListWidget, 1);

    // 按钮改为发射信号，由 MainWindow 处理
    QPushButton* modelImportBtn = new QPushButton(tr("从厂商导入…"), this);
    modelImportBtn->setToolTip(tr("使用 DeepSeek / OpenAI / Claude / Ollama / Gemini 等预设填写 Base URL、API Key、模型"));
    connect(modelImportBtn, &QPushButton::clicked, this, &IdentityView::modelConfigImportRequested);

    QPushButton* mcpConfigBtn = new QPushButton(tr("配置 MCP…"), this);
    mcpConfigBtn->setToolTip(tr("配置 MCP 工具服务（可选）"));
    connect(mcpConfigBtn, &QPushButton::clicked, this, &IdentityView::mcpConfigRequested);

    QPushButton* showLogBtn = new QPushButton(tr("查看工具执行日志 (RAW)"), this);
    showLogBtn->setStyleSheet("background-color: #607D8B; color: white; font-weight: bold; padding: 5px;");
    connect(showLogBtn, &QPushButton::clicked, this, &IdentityView::toolLogRequested);

    QVBoxLayout* btnLayout = new QVBoxLayout();
    btnLayout->addWidget(modelImportBtn);
    btnLayout->addWidget(mcpConfigBtn);
    btnLayout->addWidget(showLogBtn);
    leftLayout->addLayout(btnLayout);

    splitter->addWidget(leftContainer);

    // --- 中间：聊天区 ---
    QWidget* centerContainer = new QWidget(this);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerContainer);
    centerLayout->setContentsMargins(0, 0, 0, 0);

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
    m_historyDisplay->header()->setStretchLastSection(true);
    historyLayout->addWidget(m_historyDisplay, 1);

    m_clearHistoryBtn = new QPushButton(tr("清空历史"), this);
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

        AgentRuntime* runtime = m_chatService->runtimeForSession(sessionId);
        QJsonArray h = runtime ? runtime->getHistory() : QJsonArray();
        m_chatWidget->setEmptyStateVisible(false);
        restoreChatFromHistory(h);

        QJsonArray ioH = runtime ? runtime->getIoHistory() : QJsonArray();
        updateHistoryDisplayFrom(ioH);
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
}

// ==================== activate / deactivate ====================

void IdentityView::activate()
{
    m_isActive = true;
    m_hasPendingStreamMsg = false;
    reloadSessionList();
    if (!m_currentSessionId.isEmpty()) {
        AgentRuntime* runtime = m_chatService->runtimeForSession(m_currentSessionId);
        if (runtime) {
            m_chatWidget->setEmptyStateVisible(false);
            restoreChatFromHistory(runtime->getHistory());
        }
        updateHistoryDisplay();

        // 如果当前 Session 正在流式输出，恢复流式渲染状态
        Session* session = SessionManager::instance()->findById(m_currentSessionId);
        if (session && session->isStreaming()) {
            m_chatWidget->setSendingState(true);
            const Session::StreamState& state = session->streamState();
            if (!state.buffer.isEmpty()) {
                // 添加一个占位消息并填入已有 buffer
                QString agentName = m_chatService->agentDisplayNameForSession(m_currentSessionId);
                m_chatWidget->addMessage(makeMessageParams("", false, agentName));
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
}

// ==================== 会话列表 ====================

void IdentityView::reloadSessionList()
{
    m_filteredSessionIds.clear();
    m_chatListWidget->clearChats();

    QList<Session*> sessions;
    if (isUserView())
        sessions = SessionManager::instance()->allSessions();
    else
        sessions = SessionManager::instance()->sessionsForIdentity(m_identityId);

    for (Session* s : sessions) {
        m_filteredSessionIds.append(s->id());
        m_chatListWidget->addChatItem(
            s->title().isEmpty() ? tr("新对话") : s->title(),
            QString(), QString(), QColor(Qt::gray), 0);
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
    QString name = src->index(row, 0).data(ChatListNameRole).toString();
    if (name.isEmpty())
        name = tr("新对话");
    QString shortPreview = preview;
    if (shortPreview.length() > 80)
        shortPreview = shortPreview.left(80) + QStringLiteral("...");
    m_chatListWidget->updateChatItem(row, name, shortPreview,
                                     QTime::currentTime().toString(QStringLiteral("hh:mm")),
                                     QColor(Qt::gray), 0);
}

// ==================== UI 辅助 ====================

void IdentityView::updateSendingState()
{
    if (!m_chatWidget)
        return;
    bool sending = m_chatService->isSessionStreaming(m_currentSessionId);
    m_chatWidget->setSendingState(sending);
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
        bool isMine = (role == QLatin1String("user"));
        m_chatWidget->addMessage(makeMessageParams(content, isMine, isMine ? QStringLiteral("Me") : assistantName));
    }
}

// ==================== 会话操作 ====================

void IdentityView::onNewChatRequested()
{
    if (m_chatWidget) {
        m_chatWidget->setEmptyStateVisible(false);
        clearChatMessages();
    }
    updateSendingState();

    Session* session = nullptr;
    if (isUserView())
        session = m_chatService->createNewSession(tr("新对话"));
    else
        session = m_chatService->createSessionForIdentity(m_identityId, tr("新对话"));

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

    AgentRuntime* runtime = m_chatService->runtimeForSession(sessionId);
    QJsonArray h = runtime ? runtime->getHistory() : QJsonArray();
    m_chatWidget->setEmptyStateVisible(false);
    restoreChatFromHistory(h);

    QJsonArray ioH = runtime ? runtime->getIoHistory() : QJsonArray();
    updateHistoryDisplayFrom(ioH);
    updateSendingState();
    m_chatService->saveSessionsToDisk();
}

void IdentityView::onChatItemRemoved(int row)
{
    QString sessionId = sessionIdForRow(row);
    if (sessionId.isEmpty())
        return;

    m_chatService->removeSession(sessionId);
    m_filteredSessionIds.removeAll(sessionId);

    if (m_currentSessionId == sessionId) {
        m_currentSessionId.clear();
        m_chatListWidget->listView()->clearSelection();
        m_chatListWidget->listView()->setCurrentIndex(QModelIndex());
        clearChatMessages();
        if (m_chatWidget)
            m_chatWidget->setEmptyStateVisible(true);
    }
    updateHistoryDisplay();
    updateSendingState();
    m_chatService->saveSessionsToDisk();
}

void IdentityView::onChatItemRenamed(int row, const QString& name)
{
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

    setSendingState(true);
    updateChatListItem(m_currentSessionId, prompt);
    m_chatService->sendUserMessage(m_currentSessionId, prompt);
    updateHistoryDisplay();
}

void IdentityView::onAbortClicked()
{
    qDebug() << "IdentityView: [Signal Received] Stop requested by User UI";

    const bool wasStreaming = m_chatService->isSessionStreaming(m_currentSessionId);
    QString rolledBackUserMsg = m_chatService->abortAndRollback(m_currentSessionId);

    if (m_chatWidget && wasStreaming) {
        m_chatWidget->addMessage(makeMessageParams("[已手动中断]", false, "System"));
        if (!rolledBackUserMsg.isEmpty()) {
            if (auto* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget())) {
                if (auto* edit = input->findChild<QLineEdit*>("chatWidgetInputEdit")) {
                    edit->setText(rolledBackUserMsg);
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

    // 累积到共享 buffer（供其他 View activate 时恢复）
    session->streamState().buffer.append(data);

    m_chatWidget->setSendingState(true);
    if (!m_hasPendingStreamMsg) {
        QString agentName = m_chatService->agentDisplayNameForSession(sessionId);
        m_chatWidget->addMessage(makeMessageParams("", false, agentName));
        m_hasPendingStreamMsg = true;
    }
    m_chatWidget->streamOutput(data);
}

void IdentityView::handleFinished(const QString& sessionId, const QString& fullContent)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    Session* session = SessionManager::instance()->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.isStreaming = false;
        state.buffer.clear();
        state.lastMsgIsTool = false;
    }

    updateChatListItem(sessionId, fullContent);
    m_chatService->saveSessionsToDisk();

    if (!m_isActive || !m_chatWidget || sessionId != m_currentSessionId) {
        m_hasPendingStreamMsg = false;
        updateSendingState();
        return;
    }

    if (m_hasPendingStreamMsg)
        m_chatWidget->removeLastMessage();
    m_hasPendingStreamMsg = false;

    if (!fullContent.isEmpty()) {
        QString agentName = m_chatService->agentDisplayNameForSession(sessionId);
        m_chatWidget->addMessage(makeMessageParams(fullContent, false, agentName));
    }
    updateSendingState();
    updateHistoryDisplay();
}

void IdentityView::handleError(const QString& sessionId, const QString& errorMsg)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    Session* session = SessionManager::instance()->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.isStreaming = false;
        state.buffer.clear();
        state.lastMsgIsTool = false;
    }
    m_hasPendingStreamMsg = false;
    updateSendingState();

    if (m_isActive && m_chatWidget && sessionId == m_currentSessionId) {
        m_chatWidget->addMessage(makeMessageParams(
            QString("❌ 错误: %1").arg(errorMsg), false, "System"));
        updateHistoryDisplay();
    }
}

void IdentityView::handleToolCallsStarted(const QString& sessionId)
{
    if (!m_filteredSessionIds.contains(sessionId))
        return;

    Session* session = SessionManager::instance()->findById(sessionId);
    if (!session)
        return;

    Session::StreamState& state = session->streamState();
    state.buffer.clear();
    state.lastMsgIsTool = false;
    m_hasPendingStreamMsg = false;

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
    AgentRuntime* runtime = m_chatService->runtimeForSession(m_currentSessionId);
    QJsonArray ioH = runtime ? runtime->getIoHistory() : QJsonArray();
    updateHistoryDisplayFrom(ioH);
}

void IdentityView::onClearHistoryClicked()
{
    AgentRuntime* runtime = m_chatService->runtimeForSession(m_currentSessionId);
    if (runtime)
        runtime->clearHistory();
    m_historyDisplay->clear();
    m_historyLabel->setText(tr("请求/响应历史 (共 0 次)"));
    if (m_chatWidget)
        m_chatWidget->addMessage(makeMessageParams("[对话历史已清空]", false, "System"));
}

// ==================== 语音（占位） ====================

void IdentityView::onVoiceStartRequested()
{
    if (m_chatWidget)
        m_chatWidget->addMessage(makeMessageParams("[语音输入功能暂未接入]", false, "System"));
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

    if (isMine) {
        profile->setUserName(QStringLiteral("我"));
        profile->setTmId(QStringLiteral("user"));
        profile->addDetailItem(QStringLiteral("角色"), QStringLiteral("用户"));
    } else {
        profile->setUserName(sender.isEmpty() ? QStringLiteral("Agent") : sender);
        profile->setTmId(QStringLiteral("agent"));
        profile->addDetailItem(QStringLiteral("角色"), QStringLiteral("AI 助手"));
        profile->addSeparator();
        profile->addDetailItem(QStringLiteral("岗位"), QStringLiteral("智能对话"));
        profile->addSeparator();

        AgentRuntime* runtime = m_chatService->runtimeForSession(m_currentSessionId);
        if (runtime) {
            LLMConfig cfg = runtime->config();
            QString modelInfo = ModelFactory::modelIdToString(cfg.model);
            if (modelInfo.isEmpty() || cfg.model == ModelId::Unknown)
                modelInfo = QStringLiteral("默认模型");
            else if (cfg.model == ModelId::Custom && !cfg.customModelId.isEmpty())
                modelInfo = cfg.customModelId;
            profile->addDetailItem(QStringLiteral("模型"), modelInfo);
        }
    }

    QPoint pos = QCursor::pos();
    profile->move(pos.x() - profile->width() / 2, pos.y() - 20);
    profile->show();
}
