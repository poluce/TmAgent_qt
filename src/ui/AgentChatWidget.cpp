#include "AgentChatWidget.h"
#include "ToolLogWidget.h"
#include "chat_list_roles.h"
#include "chat_list_view.h"
#include "chat_list_widget.h"
#include "chat_widget.h"
#include "chat_widget_input.h"
#include "chat_widget_view.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/service/AgentRuntime.h"
#include "core/service/ChatService.h"
#include "core/utils/DefaultPrompts.h"
#include "core/utils/KeychainHelper.h"
#include "core/utils/ModelConfigLoader.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include "modelconfig/model_config_import_page.h"
#include "profile_widget.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QTime>
#include <QTimer>
#include <QToolTip>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "ThinkingIndicatorWidget.h"

namespace {
bool extractEnvVarName(const QString& value, QString* varName)
{
    if (!varName)
        return false;
    const QString trimmed = value.trimmed();
    if (trimmed.startsWith(QStringLiteral("$ENV{")) && trimmed.endsWith('}')) {
        *varName = trimmed.mid(5, trimmed.size() - 6).trimmed();
        return !varName->isEmpty();
    }
    if (trimmed.startsWith(QStringLiteral("${")) && trimmed.endsWith('}')) {
        *varName = trimmed.mid(2, trimmed.size() - 3).trimmed();
        return !varName->isEmpty();
    }
    if (trimmed.startsWith('$') && trimmed.size() > 1 && !trimmed.contains(' ')) {
        *varName = trimmed.mid(1).trimmed();
        return !varName->isEmpty();
    }
    return false;
}

bool isEnvVarReference(const QString& value)
{
    QString dummy;
    return extractEnvVarName(value, &dummy);
}

QString resolveAssistantIdForSession(const QString& sessionId)
{
    if (sessionId.trimmed().isEmpty())
        return QString();
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

AgentChatWidget::AgentChatWidget(QWidget* parent)
    : QWidget(parent)
{
    m_chatService = new ChatService(this);
    m_chatService->initialize();

    setupUI();

    // 连接 ChatService 信号
    connect(m_chatService, &ChatService::streamDataReceived, this, &AgentChatWidget::onServiceStreamData);
    connect(m_chatService, &ChatService::finished, this, &AgentChatWidget::onServiceFinished);
    connect(m_chatService, &ChatService::errorOccurred, this, &AgentChatWidget::onServiceError);
    connect(m_chatService, &ChatService::toolCallsStarted, this, &AgentChatWidget::onServiceToolCallsStarted);
    connect(m_chatService, &ChatService::toolEvent, this, &AgentChatWidget::onServiceToolEvent);
    connect(m_chatService, &ChatService::reasoningStarted, this, &AgentChatWidget::onServiceReasoningStarted);
    connect(m_chatService, &ChatService::reasoningStopped, this, &AgentChatWidget::onServiceReasoningStopped);

    // 加载持久化会话
    if (m_chatService->loadSessionsFromDisk()) {
        // 填充 ChatListWidget
        SessionManager* sm = SessionManager::instance();
        QList<Session*> sessions = sm->allSessions();
        for (Session* session : sessions) {
            QString name = session->title();
            if (name.isEmpty())
                name = tr("新对话");
            m_chatListWidget->addChatItem(name, QString(), QString(), QColor(Qt::gray), 0);
        }
        // 恢复当前会话
        QString currentId = m_chatService->currentSessionId();
        if (!currentId.isEmpty()) {
            m_currentSessionId = currentId;
            int row = rowForSessionId(currentId);
            if (row >= 0) {
                QAbstractItemModel* model = m_chatListWidget->listView()->model();
                QModelIndex sel;
                if (QSortFilterProxyModel* proxy = qobject_cast<QSortFilterProxyModel*>(model))
                    sel = proxy->mapFromSource(m_chatListWidget->listView()->standardModel()->index(row, 0));
                else if (model)
                    sel = model->index(row, 0);
                if (sel.isValid())
                    m_chatListWidget->listView()->setCurrentIndex(sel);

                Session* session = SessionManager::instance()->findById(currentId);
                if (session) {
                    m_chatWidget->setEmptyStateVisible(false);
                    restoreChatFromSession(session);
                }
            }
            updateHistoryDisplay();
            updateSendingState();
        }
    }

    if (m_currentSessionId.isEmpty()) {
        // 没有加载到任何会话，创建默认会话
        Session* session = m_chatService->createNewSession(tr("新对话"));
        if (session) {
            m_currentSessionId = session->id();
            m_chatListWidget->addChatItem(tr("新对话"), QString(), QString(), QColor(Qt::gray), 0);
            QAbstractItemModel* model = m_chatListWidget->listView()->model();
            if (model && model->rowCount() > 0) {
                QModelIndex last = model->index(model->rowCount() - 1, 0);
                if (last.isValid())
                    m_chatListWidget->listView()->setCurrentIndex(last);
            }
        }
    }

    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        m_chatService->saveSessionsToDisk();
    });
}

// ==================== setupUI ====================

void AgentChatWidget::setupUI()
{
    setWindowTitle("TmAgent - Team of Agents");
    resize(1200, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
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

    QPushButton* modelImportBtn = new QPushButton(tr("从厂商导入…"), this);
    modelImportBtn->setToolTip(tr("使用 DeepSeek / OpenAI / Claude / Ollama / Gemini 等预设填写 Base URL、API Key、模型"));
    connect(modelImportBtn, &QPushButton::clicked, this, &AgentChatWidget::onModelConfigImportClicked);

    QPushButton* mcpConfigBtn = new QPushButton(tr("配置 MCP…"), this);
    mcpConfigBtn->setToolTip(tr("配置 MCP 工具服务（可选）"));
    connect(mcpConfigBtn, &QPushButton::clicked, this, &AgentChatWidget::onMcpConfigClicked);

    QPushButton* showLogBtn = new QPushButton(tr("查看工具执行日志 (RAW)"), this);
    showLogBtn->setStyleSheet("background-color: #607D8B; color: white; font-weight: bold; border: none; border-radius: 10px; padding: 6px 10px;");
    connect(showLogBtn, &QPushButton::clicked, this, [this]() {
        if (!m_toolLogWidget) {
            m_toolLogWidget = new ToolLogWidget();
        }
        m_toolLogWidget->show();
        m_toolLogWidget->raise();
        m_toolLogWidget->activateWindow();
    });

    QVBoxLayout* btnLayout = new QVBoxLayout();
    btnLayout->addWidget(modelImportBtn);
    btnLayout->addWidget(mcpConfigBtn);
    btnLayout->addWidget(showLogBtn);
    leftLayout->addLayout(btnLayout);

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

    m_historyLabel = new QLabel("请求/响应历史 (共 0 次)", this);
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

    m_clearHistoryBtn = new QPushButton("清空历史", this);
    m_clearHistoryBtn->setStyleSheet("border: 1px solid #e5e7eb; border-radius: 10px; padding: 6px 10px; background: #f5f5f5;");
    historyLayout->addWidget(m_clearHistoryBtn);

    splitter->addWidget(historyContainer);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes(QList<int>() << 300 << 580 << 320);

    mainLayout->addWidget(splitter);

    // 悬浮在中间聊天区顶部的指示器
    m_thinkingIndicator = new ThinkingIndicatorWidget(centerContainer);
    // 默认隐藏
    m_thinkingIndicator->hide();

    // 连接 UI 信号
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &AgentChatWidget::onClearHistoryClicked);
    connect(m_chatWidget, &ChatWidget::messageSent, this, &AgentChatWidget::onUserMessageSent);
    connect(m_chatWidget, &ChatWidget::stopRequested, this, &AgentChatWidget::onAbortClicked);
    connect(m_chatListWidget, &ChatListWidget::headerActionTriggered, this, [this](QAction* action) {
        QString data = action->data().toString();
        if (data == QLatin1String("new_chat"))
            onNewChatRequested();
        else if (data == QLatin1String("remove_current"))
            onRemoveCurrentChatRequested();
    });
    connect(m_chatListWidget, &ChatListWidget::chatItemActivated, this, &AgentChatWidget::onChatItemActivated);
    connect(m_chatListWidget, &ChatListWidget::chatItemRemoved, this, &AgentChatWidget::onChatItemRemoved);
    connect(m_chatListWidget, &ChatListWidget::chatItemRenamed, this, &AgentChatWidget::onChatItemRenamed);
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
        restoreChatFromSession(session);
        updateHistoryDisplay();
        updateSendingState();
        m_chatService->saveSessionsToDisk();
    });

    if (ChatWidgetInput* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget())) {
        connect(input, &ChatWidgetInput::voiceStartRequested, this, &AgentChatWidget::onVoiceStartRequested);
        connect(input, &ChatWidgetInput::voiceStopRequested, this, &AgentChatWidget::onVoiceStopRequested);
    }

    if (ChatWidgetView* chatView = m_chatWidget->view()) {
        connect(chatView, &ChatWidgetView::avatarClicked, this, &AgentChatWidget::onAvatarClicked);
    }
}

// ==================== 行号 <-> SessionId 转换 ====================

QString AgentChatWidget::sessionIdForRow(int row) const
{
    SessionManager* sm = SessionManager::instance();
    Session* session = sm->sessionAt(row);
    return session ? session->id() : QString();
}

int AgentChatWidget::rowForSessionId(const QString& sessionId) const
{
    return SessionManager::instance()->indexOf(sessionId);
}

void AgentChatWidget::updateChatListItem(const QString& sessionId, const QString& preview)
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
    m_chatListWidget->updateChatItem(row, name, shortPreview, QTime::currentTime().toString(QStringLiteral("hh:mm")), QColor(Qt::gray), 0);
}

// ==================== UI 辅助 ====================

void AgentChatWidget::updateSendingState()
{
    if (!m_chatWidget)
        return;
    bool sending = m_chatService->isSessionStreaming(m_currentSessionId);
    m_chatWidget->setSendingState(sending);
}

void AgentChatWidget::setSendingState(bool isSending)
{
    ChatWidgetInput* input = nullptr;
    if (m_chatWidget) {
        input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget());
        if (input)
            input->setSendingState(isSending);
    }
}

void AgentChatWidget::clearChatMessages()
{
    if (m_chatWidget)
        m_chatWidget->clearMessages();
}

void AgentChatWidget::restoreChatFromSession(Session* session)
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

    QList<ChatWidget::HistoryMessage> historyMessages;
    historyMessages.reserve(messages.size());
    QHash<QString, Identity*> senderIdentityCache;
    senderIdentityCache.reserve(8);
    const QString assistantName = m_chatService->agentDisplayNameForSession(session->id());

    for (const Message& msg : messages) {
        if (msg.content.type == MessageContent::Type::ToolCall
            || msg.content.type == MessageContent::Type::ToolResult) {
            continue;
        }
        if (msg.content.text.trimmed().isEmpty())
            continue;

        ChatWidget::HistoryMessage historyMsg;
        historyMsg.messageId = msg.id;
        historyMsg.content = msg.content.text;
        historyMsg.timestamp = msg.timestamp.isValid() ? msg.timestamp : QDateTime::currentDateTime();

        // 识别文件类型消息并提取附件信息
        if (msg.content.type == MessageContent::Type::File) {
            historyMsg.messageType = ChatWidgetMessage::MessageType::File;
            historyMsg.filePath = msg.content.payload.value(QStringLiteral("file_path")).toString();
            historyMsg.fileName = msg.content.payload.value(QStringLiteral("file_name")).toString();
            historyMsg.fileSize = static_cast<qint64>(msg.content.payload.value(QStringLiteral("file_size")).toDouble());
        }

        if (msg.content.type == MessageContent::Type::System || msg.senderId == QLatin1String("system")) {
            historyMsg.messageType = ChatWidgetMessage::MessageType::System;
            historyMsg.senderId = QStringLiteral("system");
            historyMsg.displayName = QStringLiteral("System");
        } else {
            Identity* senderIdentity = senderIdentityCache.value(msg.senderId, nullptr);
            if (!senderIdentityCache.contains(msg.senderId)) {
                senderIdentity = IdentityManager::instance()->findById(msg.senderId);
                senderIdentityCache.insert(msg.senderId, senderIdentity);
            }
            const bool isUser = senderIdentity && senderIdentity->isUser();
            historyMsg.senderId = isUser ? QStringLiteral("user") : msg.senderId;
            historyMsg.displayName = isUser
                ? QStringLiteral("Me")
                : (senderIdentity && !senderIdentity->name().trimmed().isEmpty()
                       ? senderIdentity->name().trimmed()
                       : assistantName);
            historyMsg.isMine = isUser;
        }

        historyMessages.append(historyMsg);
    }

    clearChatMessages();
    if (historyMessages.isEmpty()) {
        return;
    }
    m_chatWidget->setHistoryMessages(historyMessages, true);
}

void AgentChatWidget::restoreChatFromHistory(const QJsonArray& history)
{
    if (!m_chatWidget)
        return;
    clearChatMessages();
    const QString assistantName = m_chatService->agentDisplayNameForSession(m_currentSessionId);
    const QString assistantId = resolveAssistantIdForSession(m_currentSessionId).trimmed();
    QList<ChatWidget::HistoryMessage> historyMessages;
    historyMessages.reserve(history.size());
    for (const QJsonValue& v : history) {
        QJsonObject o = v.toObject();
        QString role = o["role"].toString();
        QString content = o["content"].toString();
        if (content.isEmpty())
            continue;
        if (role == QLatin1String("tool"))
            continue;

        const bool isMine = (role == QLatin1String("user"));
        ChatWidget::HistoryMessage msg;
        msg.content = content;
        msg.timestamp = QDateTime::currentDateTime();
        msg.senderId = isMine ? QStringLiteral("user")
                              : (assistantId.isEmpty() ? QStringLiteral("assistant") : assistantId);
        msg.displayName = isMine ? QStringLiteral("Me") : assistantName;
        msg.isMine = isMine;
        historyMessages.append(msg);
    }

    if (historyMessages.isEmpty()) {
        return;
    }
    m_chatWidget->setHistoryMessages(historyMessages, true);
}

// ==================== 会话操作 ====================

void AgentChatWidget::onNewChatRequested()
{
    if (m_chatWidget) {
        m_chatWidget->setEmptyStateVisible(false);
        clearChatMessages();
    }
    updateSendingState();

    Session* session = m_chatService->createNewSession(tr("新对话"));
    if (!session)
        return;

    m_currentSessionId = session->id();
    m_chatListWidget->addChatItem(tr("新对话"), QString(), QString(), QColor(Qt::gray), 0);

    QAbstractItemModel* model = m_chatListWidget->listView()->model();
    if (model && model->rowCount() > 0) {
        QModelIndex last = model->index(model->rowCount() - 1, 0);
        if (last.isValid())
            m_chatListWidget->listView()->setCurrentIndex(last);
    }
    updateHistoryDisplay();
    updateSendingState();
    m_chatService->saveSessionsToDisk();
}

void AgentChatWidget::onChatItemActivated(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount)
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
    restoreChatFromSession(session);
    updateHistoryDisplay();
    updateSendingState();
    m_chatService->saveSessionsToDisk();
}

void AgentChatWidget::onChatItemRemoved(int row)
{
    QString sessionId = sessionIdForRow(row);
    if (sessionId.isEmpty())
        return;

    m_chatService->removeSession(sessionId);

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

void AgentChatWidget::onChatItemRenamed(int row, const QString& name)
{
    QString sessionId = sessionIdForRow(row);
    if (sessionId.isEmpty())
        return;
    Session* session = SessionManager::instance()->findById(sessionId);
    if (session)
        session->setTitle(name.trimmed());

    // 同步更新 Agent Identity 名称
    AgentRuntime* runtime = m_chatService->runtimeForSession(sessionId);
    if (runtime && runtime->identity()) {
        runtime->identity()->setName(name.trimmed());
        LLMConfig cfg = runtime->config();
        cfg.userName = name.trimmed();
        runtime->setConfig(cfg);
    }
    m_chatService->saveSessionsToDisk();
}

void AgentChatWidget::onRemoveCurrentChatRequested()
{
    int row = rowForSessionId(m_currentSessionId);
    if (row < 0)
        return;
    if (!m_chatListWidget->removeChatItem(row))
        return;
    onChatItemRemoved(row);
}

// ==================== 用户消息与中止 ====================

void AgentChatWidget::onUserMessageSent(const QString& content)
{
    QString prompt = content.trimmed();
    if (prompt.isEmpty())
        return;

    setSendingState(true);
    updateChatListItem(m_currentSessionId, prompt);
    m_chatService->sendUserMessage(m_currentSessionId, prompt);
    updateHistoryDisplay();
}

void AgentChatWidget::onAbortClicked()
{
    qDebug() << "AgentChatWidget: [Signal Received] Stop requested by User UI";

    const bool wasStreaming = m_chatService->isSessionStreaming(m_currentSessionId);
    QString rolledBackUserMsg = m_chatService->abortAndRollback(m_currentSessionId);

    if (m_chatWidget && wasStreaming) {
        m_chatWidget->addMessage(makeMessageParams("[已手动中断]", false, "System"));
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
    if (m_thinkingIndicator) {
        m_thinkingIndicator->hideIndicator();
    }
}

// ==================== ChatService 信号处理 ====================

void AgentChatWidget::onServiceStreamData(const QString& sessionId, const QString& data)
{
    if (!m_chatWidget)
        return;
    if (sessionId != m_currentSessionId)
        return;

    Session* session = SessionManager::instance()->findById(sessionId);
    if (!session)
        return;

    Session::StreamState& state = session->streamState();
    m_chatWidget->setSendingState(true);
    if (!state.hasPendingMessage) {
        QString agentName = m_chatService->agentDisplayNameForSession(sessionId);
        m_chatWidget->addMessage(makeMessageParams("", false, agentName));
        state.hasPendingMessage = true;
        state.lastMsgIsTool = false;
    }
    m_chatWidget->streamOutput(data);
}

void AgentChatWidget::onServiceFinished(const QString& sessionId, const QString& fullContent)
{
    Session* session = SessionManager::instance()->findById(sessionId);
    bool hadPending = false;
    if (session) {
        Session::StreamState& state = session->streamState();
        hadPending = state.hasPendingMessage;
        state.isStreaming = false;
        state.hasPendingMessage = false;
        state.buffer.clear();
        state.lastMsgIsTool = false;
    }
    updateSendingState();

    updateChatListItem(sessionId, fullContent);
    m_chatService->saveSessionsToDisk();

    if (!m_chatWidget || sessionId != m_currentSessionId)
        return;

    if (hadPending)
        m_chatWidget->removeLastMessage();
    if (!fullContent.isEmpty()) {
        QString agentName = m_chatService->agentDisplayNameForSession(sessionId);
        m_chatWidget->addMessage(makeMessageParams(fullContent, false, agentName));
        m_chatWidget->setSendingState(false);
    }
    if (m_thinkingIndicator) {
        m_thinkingIndicator->hideIndicator();
    }
    updateHistoryDisplay();
}

void AgentChatWidget::onServiceError(const QString& sessionId, const QString& errorMsg)
{
    Session* session = SessionManager::instance()->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.isStreaming = false;
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
    }
    updateSendingState();

    if (m_chatWidget && sessionId == m_currentSessionId) {
        if (session && session->streamState().hasPendingMessage) {
            m_chatWidget->setSendingState(false);
            m_chatWidget->streamOutput(QStringLiteral("\n\n[系统] 请求失败: ") + errorMsg);
        } else {
            m_chatWidget->addMessage(makeMessageParams(
                QString("❌ 错误: %1").arg(errorMsg), false, "System"));
        }
        updateHistoryDisplay();
    }
    if (m_thinkingIndicator) {
        m_thinkingIndicator->hideIndicator();
    }
}

void AgentChatWidget::onServiceToolCallsStarted(const QString& sessionId)
{
    if (sessionId != m_currentSessionId)
        return;

    m_chatWidget->clearStreamTargetRow();
    updateSendingState();

    // 如果工具调用开始且指示器展示中，平滑过渡文本
    if (m_thinkingIndicator) {
        m_thinkingIndicator->showThinking(QStringLiteral("🔧 工具调用与反思中..."));
    }

    Session* session = SessionManager::instance()->findById(sessionId);
    if (!session)
        return;

    Session::StreamState& state = session->streamState();
    if (state.hasPendingMessage) {
        state.hasPendingMessage = false;
        state.buffer.clear();
    }
    state.lastMsgIsTool = false;

    if (sessionId == m_currentSessionId)
        updateHistoryDisplay();
}

void AgentChatWidget::onServiceReasoningStarted(const QString& sessionId)
{
    if (sessionId != m_currentSessionId)
        return;

    if (m_thinkingIndicator) {
        m_thinkingIndicator->showThinking(QStringLiteral("💡 正在深度思考中..."));
    }
}

void AgentChatWidget::onServiceReasoningStopped(const QString& sessionId)
{
    if (sessionId != m_currentSessionId)
        return;

    if (m_thinkingIndicator) {
        m_thinkingIndicator->hideIndicator();
    }
}

void AgentChatWidget::onServiceToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    if (m_toolLogWidget)
        m_toolLogWidget->logEvent(event);

    // send_file 工具完成后，在聊天区实时渲染文件卡片
    if (event.toolName == QLatin1String("send_file")
        && event.status == QLatin1String("completed")
        && event.success
        && !event.data.isEmpty()
        && sessionId == m_currentSessionId) {

        const QString fileName = event.data.value(QStringLiteral("file_name")).toString();
        const qint64 fileSize = static_cast<qint64>(event.data.value(QStringLiteral("file_size")).toDouble());
        const QString description = event.data.value(QStringLiteral("description")).toString();
        const QString filePath = event.data.value(QStringLiteral("file_path")).toString();

        if (!filePath.isEmpty() && m_chatWidget) {
            QString agentName = m_chatService->agentDisplayNameForSession(sessionId);
            ChatWidget::MessageParams params = makeMessageParams(description, false, agentName);
            m_chatWidget->addFileMessage(params, filePath, fileName, fileSize);
        }
    }

    Q_UNUSED(sessionId);
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

void AgentChatWidget::updateHistoryDisplayFrom(const QJsonArray& history)
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

void AgentChatWidget::updateHistoryDisplay()
{
    QJsonArray ioH;
    AgentRuntime* runtime = m_chatService ? m_chatService->runtimeForSession(m_currentSessionId) : nullptr;
    if (runtime && runtime->currentSessionId() == m_currentSessionId)
        ioH = runtime->getIoHistory();
    updateHistoryDisplayFrom(ioH);
}

void AgentChatWidget::onClearHistoryClicked()
{
    Session* session = SessionManager::instance()->findById(m_currentSessionId);
    if (session) {
        session->clearMessages();
    }

    AgentRuntime* runtime = m_chatService->runtimeForSession(m_currentSessionId);
    if (runtime && runtime->currentSessionId() == m_currentSessionId)
        runtime->clearHistory();
    m_historyDisplay->clear();
    m_historyLabel->setText("请求/响应历史 (共 0 次)");
    if (m_chatWidget)
        m_chatWidget->addMessage(makeMessageParams("[对话历史已清空]", false, "System"));
    if (m_chatService)
        m_chatService->saveSessionsToDisk();
}

// ==================== 语音（占位） ====================

void AgentChatWidget::onVoiceStartRequested()
{
    if (m_chatWidget)
        m_chatWidget->addMessage(makeMessageParams("[语音输入功能暂未接入]", false, "System"));
}

void AgentChatWidget::onVoiceStopRequested()
{
}

// ==================== MCP 配置 ====================

void AgentChatWidget::onMcpConfigClicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("配置 MCP 工具服务"));

    auto* layout = new QVBoxLayout(&dlg);
    auto* hint = new QLabel(tr("每行一个 server：name|url|token|header|prefix|async\n"
                               "示例: exa|https://example.com/mcp|TOKEN|Authorization|1|1\n"
                               "说明: prefix=1 将工具名前缀为 name:tool，async=1 使用异步回传。"),
                            &dlg);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* editor = new QPlainTextEdit(&dlg);
    const QStringList specs = m_chatService->loadMcpConfigSpecs();
    editor->setPlainText(specs.join('\n'));
    layout->addWidget(editor, 1);

    auto* envHint = new QLabel(tr("注意：环境变量 TMAGENT_MCP_SERVERS 会在运行时追加，但不会写入此配置。"), &dlg);
    envHint->setWordWrap(true);
    layout->addWidget(envHint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted)
        return;

    QStringList newSpecs;
    const QStringList lines = editor->toPlainText().split('\n');
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;
        newSpecs.append(trimmed);
    }

    if (!m_chatService->saveMcpConfigSpecs(newSpecs)) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法写入 MCP 配置文件。"));
        return;
    }

    m_chatService->applyMcpConfig(newSpecs);
    m_chatService->applyToolDispatcherToAllRuntimes();
    QMessageBox::information(this, tr("配置已保存"), tr("MCP 配置已更新。"));
}

// ==================== 头像点击 ====================

void AgentChatWidget::onAvatarClicked(const QString& sender, bool isMine, int row)
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
            if (cfg.isValid()) {
                if (ModelFactory* factory = m_chatService ? m_chatService->modelFactory() : nullptr)
                    modelInfo = factory->resolveModelId(cfg).trimmed();
                else
                    modelInfo = cfg.selectedModelId.trimmed();
                if (modelInfo.trimmed().isEmpty())
                    modelInfo = QStringLiteral("未指定模型");
            }
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

// ==================== 模型导入 ====================

static QString inferProviderIdFromBaseUrl(const QString& baseUrl)
{
    const QString u = baseUrl.trimmed().toLower();
    if (u.contains("deepseek"))
        return QStringLiteral("deepseek");
    if (u.contains("openai.com"))
        return QStringLiteral("openai");
    if (u.contains("anthropic"))
        return QStringLiteral("anthropic");
    if (u.contains("localhost:11434") || u.contains("ollama"))
        return QStringLiteral("ollama");
    if (u.contains("generativelanguage") || u.contains("googleapis"))
        return QStringLiteral("gemini");
    return QString();
}

static QString canonicalProviderId(const QString& providerId)
{
    const QString id = providerId.trimmed().toLower();
    if (id == QStringLiteral("claude") || id == QStringLiteral("claudeai")
        || id == QStringLiteral("anthropic")) {
        return QStringLiteral("anthropic");
    }
    if (id == QStringLiteral("google"))
        return QStringLiteral("gemini");
    return id;
}

static bool isAnthropicProviderId(const QString& providerId)
{
    return canonicalProviderId(providerId) == QStringLiteral("anthropic");
}

static QString normalizeBaseUrl(const QString& baseUrl)
{
    QString url = baseUrl.trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

static QString resolveApiKeyInputForTest(const QString& apiKeyInput)
{
    QString varName;
    if (extractEnvVarName(apiKeyInput, &varName))
        return QProcessEnvironment::systemEnvironment().value(varName);
    return apiKeyInput.trimmed();
}

static QString buildModelsEndpoint(const QString& providerId, const QString& baseUrl)
{
    const QString root = normalizeBaseUrl(baseUrl);
    if (root.isEmpty())
        return QString();

    if (isAnthropicProviderId(providerId)) {
        if (root.endsWith(QStringLiteral("/v1/models"), Qt::CaseInsensitive))
            return root;
        if (root.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive))
            return root + QStringLiteral("/models");
        return root + QStringLiteral("/v1/models");
    }

    if (root.endsWith(QStringLiteral("/models"), Qt::CaseInsensitive))
        return root;
    return root + QStringLiteral("/models");
}

static bool isHttpReachable(QNetworkReply* reply)
{
    if (!reply)
        return false;
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    return status.isValid() || reply->error() == QNetworkReply::NoError;
}

static QString extractErrorMessage(const QByteArray& body, const QString& fallback)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fallback.trimmed();

    const QJsonObject root = doc.object();
    const QJsonObject errObj = root.value(QStringLiteral("error")).toObject();
    const QString errMsg = errObj.value(QStringLiteral("message")).toString().trimmed();
    if (!errMsg.isEmpty())
        return errMsg;

    const QString msg = root.value(QStringLiteral("message")).toString().trimmed();
    if (!msg.isEmpty())
        return msg;

    return fallback.trimmed();
}

static QStringList parseModelIdsFromResponse(const QByteArray& body)
{
    QStringList modelIds;
    QSet<QString> seen;
    auto append = [&modelIds, &seen](const QString& candidate) {
        const QString id = candidate.trimmed();
        if (id.isEmpty() || seen.contains(id))
            return;
        seen.insert(id);
        modelIds.append(id);
    };

    auto parseArray = [&append](const QJsonArray& arr) {
        for (const QJsonValue& item : arr) {
            if (item.isString()) {
                append(item.toString());
                continue;
            }
            if (!item.isObject())
                continue;
            const QJsonObject obj = item.toObject();
            append(obj.value(QStringLiteral("id")).toString());
            append(obj.value(QStringLiteral("model")).toString());
            append(obj.value(QStringLiteral("name")).toString());
        }
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return modelIds;

    if (doc.isArray()) {
        parseArray(doc.array());
        return modelIds;
    }

    if (!doc.isObject())
        return modelIds;

    const QJsonObject root = doc.object();
    parseArray(root.value(QStringLiteral("data")).toArray());
    parseArray(root.value(QStringLiteral("models")).toArray());
    parseArray(root.value(QStringLiteral("result")).toArray());
    return modelIds;
}

static QList<ModelConfigProvider> defaultModelConfigProviders()
{
    QList<ModelConfigProvider> list;
    ModelConfigProvider deepseek { "deepseek", "DeepSeek", "中国高性能 AI 模型" };
    deepseek.fields << ModelConfigField { "apiKey", "API 密钥", "sk-...", "", true, true };
    deepseek.fields << ModelConfigField { "modelId", "模型名称", "deepseek-chat", "deepseek-chat" };
    deepseek.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.deepseek.com", "https://api.deepseek.com" };
    list << deepseek;

    ModelConfigProvider openai { "openai", "OpenAI", "全球领先的 AI 语言模型" };
    openai.fields << ModelConfigField { "apiKey", "API 密钥", "sk-...", "", true, true };
    openai.fields << ModelConfigField { "modelId", "模型名称", "gpt-4o", "gpt-4o" };
    openai.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.openai.com/v1", "https://api.openai.com/v1" };
    list << openai;

    ModelConfigProvider anthropic { "anthropic", "Anthropic / Claude", "Anthropic 强大的 AI 模型" };
    anthropic.fields << ModelConfigField { "apiKey", "API 密钥", "sk-ant-...", "", true, true };
    anthropic.fields << ModelConfigField { "modelId", "模型名称", "claude-sonnet-4-5-20250929", "claude-sonnet-4-5-20250929" };
    anthropic.fields << ModelConfigField { "baseUrl", "接口地址", "https://api.anthropic.com", "https://api.anthropic.com" };
    list << anthropic;

    ModelConfigProvider ollama { "ollama", "Ollama", "本地运行的各类型开源模型" };
    ollama.fields << ModelConfigField { "modelId", "模型名称", "llama3", "llama3" };
    ollama.fields << ModelConfigField { "baseUrl", "接口地址", "http://localhost:11434", "http://localhost:11434" };
    list << ollama;

    ModelConfigProvider gemini { "gemini", "Gemini", "Google 强大的 AI 服务" };
    gemini.fields << ModelConfigField { "apiKey", "API 密钥", "在此输入密钥", "", true, true };
    gemini.fields << ModelConfigField { "modelId", "模型名称", "gemini-1.5-pro", "gemini-1.5-pro" };
    gemini.fields << ModelConfigField { "baseUrl", "接口地址", "https://generativelanguage.googleapis.com", "" };
    list << gemini;

    return list;
}

void AgentChatWidget::onModelConfigImportClicked()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("从厂商导入模型配置"));
    dlg->resize(720, 480);

    auto* page = new ModelConfigImportPage(dlg);
    page->setProviders(defaultModelConfigProviders());
    page->applyStyleSheet();

    QString yamlPath = m_chatService->modelConfigPath();
    QString defaultConfigId = ModelConfigLoader::getDefaultConfigId(yamlPath);

    QVariantMap initial;
    if (!defaultConfigId.isEmpty()) {
        ModelConfig existingConfig = ModelConfigLoader::getModelConfig(yamlPath, defaultConfigId, false);
        QString pid = canonicalProviderId(existingConfig.provider);
        if (pid.isEmpty())
            pid = inferProviderIdFromBaseUrl(existingConfig.baseUrl);
        if (pid.isEmpty())
            pid = QStringLiteral("deepseek");
        initial["providerId"] = pid;
        initial["apiKey"] = existingConfig.apiKey;
        initial["baseUrl"] = existingConfig.baseUrl;
        initial["modelId"] = existingConfig.modelId;
        initial["configId"] = existingConfig.configId;
        initial["enabled"] = existingConfig.enabled;
    } else {
        initial["providerId"] = QStringLiteral("deepseek");
    }
    page->setConfigData(initial);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(page);

    connect(page, &ModelConfigImportPage::importRequested, this, [this, dlg, yamlPath](const QVariantMap& config) {
        ModelConfig modelConfig;
        modelConfig.modelId = config.value("modelId").toString().trimmed();
        modelConfig.configId = config.value("configId").toString().trimmed();
        modelConfig.enabled = config.value("enabled", true).toBool();
        modelConfig.displayName = config.value("providerName").toString();
        modelConfig.provider = canonicalProviderId(config.value("providerId").toString());
        if (modelConfig.provider.isEmpty())
            modelConfig.provider = config.value("providerId").toString().trimmed();
        modelConfig.baseUrl = config.value("baseUrl").toString().trimmed();

        // configId 为空时自动生成
        if (modelConfig.configId.isEmpty())
            modelConfig.configId = modelConfig.modelId;

        const ModelConfig existingById = ModelConfigLoader::getModelConfig(yamlPath, modelConfig.configId, false);
        const QString existingProvider = canonicalProviderId(existingById.provider);
        if (existingById.isValid() && !existingProvider.isEmpty()
            && existingProvider != modelConfig.provider) {
            QMessageBox::warning(
                this,
                tr("配置ID冲突"),
                tr("配置ID「%1」已归属于 Provider「%2」。\n为避免混用，请修改配置 ID 或先删除旧配置后再导入。")
                    .arg(modelConfig.configId, existingById.provider));
            return;
        }

        QString apiKeyStored;
        QString apiKeyRuntime;
        const QString apiKeyInput = config.value("apiKey").toString().trimmed();
        if (!apiKeyInput.isEmpty()) {
            QString keychainId;
            if (KeychainHelper::parseKeyRef(apiKeyInput, &keychainId)) {
                apiKeyStored = KeychainHelper::makeKeyRef(keychainId);
                bool ok = false;
                QString error;
                apiKeyRuntime = KeychainHelper::readPasswordSync(keychainId, &ok, &error);
                if (!ok || apiKeyRuntime.isEmpty()) {
                    QMessageBox::warning(this, tr("读取失败"), tr("无法从系统密钥库读取：%1").arg(error.isEmpty() ? tr("未知错误") : error));
                    return;
                }
            } else if (isEnvVarReference(apiKeyInput)) {
                apiKeyStored = apiKeyInput;
                QString varName;
                if (extractEnvVarName(apiKeyInput, &varName))
                    apiKeyRuntime = QProcessEnvironment::systemEnvironment().value(varName);
                if (apiKeyRuntime.isEmpty()) {
                    QMessageBox::warning(this, tr("环境变量未设置"), tr("未读取到 %1，请先设置环境变量后再导入。").arg(apiKeyInput));
                    return;
                }
            } else {
                keychainId = KeychainHelper::entryIdForModel(modelConfig.provider, modelConfig.modelId);
                QString error;
                if (!KeychainHelper::writePasswordSync(keychainId, apiKeyInput, &error)) {
                    QMessageBox::warning(this, tr("保存失败"), tr("无法写入系统密钥库：%1").arg(error.isEmpty() ? tr("未知错误") : error));
                    return;
                }
                apiKeyStored = KeychainHelper::makeKeyRef(keychainId);
                apiKeyRuntime = apiKeyInput;
            }
        }
        modelConfig.apiKey = apiKeyRuntime;
        modelConfig.authType = isAnthropicProviderId(modelConfig.provider)
            ? QStringLiteral("X-API-Key")
            : QStringLiteral("Bearer");
        modelConfig.temperature = 0.7;
        modelConfig.maxTokens = 4096;
        modelConfig.timeoutMs = 180000;
        modelConfig.capabilities << Capability::TextGeneration << Capability::ToolCalling;
        modelConfig.toolCalling = true;
        modelConfig.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt();

        ModelConfig saveConfig = modelConfig;
        saveConfig.apiKey = apiKeyStored;
        ModelConfigLoader::addOrUpdateModel(yamlPath, saveConfig);
        const QString currentDefaultConfigId = ModelConfigLoader::getDefaultConfigId(yamlPath);
        if (currentDefaultConfigId.trimmed().isEmpty()) {
            ModelConfigLoader::setDefaultConfigId(yamlPath, modelConfig.configId);
        }

        m_chatService->modelFactory()->registerModelConfig(modelConfig);

        LLMConfig agentConfig;
        agentConfig.configId = modelConfig.configId;
        agentConfig.systemPrompt = modelConfig.systemPrompt;
        agentConfig.userName = tr("TM Agent");
        m_chatService->setDefaultAgentConfig(agentConfig);
        m_chatService->applyConfigToAllRuntimes();

        dlg->accept();
        QMessageBox::information(this, tr("已导入"), tr("已从「%1」导入配置并保存到 %2").arg(config.value("providerName").toString(), QDir::toNativeSeparators(yamlPath)));
    });
    connect(page, &ModelConfigImportPage::cancelled, dlg, &QDialog::reject);

    connect(page, &ModelConfigImportPage::testConnectionRequested, this, [page](const QVariantMap& config) {
        const QString providerId = canonicalProviderId(config.value(QStringLiteral("providerId")).toString());
        const QString baseUrl = config.value(QStringLiteral("baseUrl")).toString().trimmed();
        const QString apiKey = resolveApiKeyInputForTest(config.value(QStringLiteral("apiKey")).toString());

        page->clearFieldErrors();
        page->setTestStatus(ModelConfigImportPage::TestStatus::Testing, tr("正在验证地址连通性…"));

        if (baseUrl.isEmpty()) {
            page->setFieldError(providerId, QStringLiteral("baseUrl"), tr("接口地址不能为空"));
            page->setTestStatus(ModelConfigImportPage::TestStatus::Failed, tr("接口地址不能为空"));
            return;
        }

        const QUrl parsedBase(baseUrl);
        if (!parsedBase.isValid()
            || (parsedBase.scheme() != QStringLiteral("http")
                && parsedBase.scheme() != QStringLiteral("https"))) {
            page->setFieldError(providerId, QStringLiteral("baseUrl"), tr("请输入合法的 http/https 地址"));
            page->setTestStatus(ModelConfigImportPage::TestStatus::Failed, tr("地址格式不合法"));
            return;
        }

        const QString modelsEndpoint = buildModelsEndpoint(providerId, baseUrl);
        if (modelsEndpoint.isEmpty()) {
            page->setFieldError(providerId, QStringLiteral("baseUrl"), tr("无法生成模型列表地址"));
            page->setTestStatus(ModelConfigImportPage::TestStatus::Failed, tr("模型列表地址无效"));
            return;
        }

        QPointer<ModelConfigImportPage> safePage(page);
        auto* nam = new QNetworkAccessManager(page);
        QNetworkReply* pingReply = nam->get(QNetworkRequest(parsedBase));
        connect(pingReply, &QNetworkReply::finished, page, [safePage, nam, pingReply, providerId, modelsEndpoint, apiKey]() {
            const bool reachable = isHttpReachable(pingReply);
            const QString pingError = pingReply->errorString();
            pingReply->deleteLater();

            if (!safePage) {
                nam->deleteLater();
                return;
            }

            if (!reachable) {
                safePage->setFieldError(providerId, QStringLiteral("baseUrl"), QObject::tr("无法连通：%1").arg(pingError));
                safePage->setTestStatus(ModelConfigImportPage::TestStatus::Failed, QObject::tr("接口地址不可达"));
                nam->deleteLater();
                return;
            }

            safePage->setTestStatus(ModelConfigImportPage::TestStatus::Testing, QObject::tr("地址可达，正在拉取模型列表…"));

            QNetworkRequest modelsReq { QUrl(modelsEndpoint) };
            modelsReq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

            if (isAnthropicProviderId(providerId)) {
                if (!apiKey.isEmpty())
                    modelsReq.setRawHeader("x-api-key", apiKey.toUtf8());
                modelsReq.setRawHeader("anthropic-version", "2023-06-01");
            } else if (!apiKey.isEmpty()) {
                modelsReq.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
            }

            QNetworkReply* modelsReply = nam->get(modelsReq);
            connect(modelsReply, &QNetworkReply::finished, safePage.data(), [safePage, nam, modelsReply, providerId]() {
                const int httpStatus = modelsReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QByteArray body = modelsReply->readAll();
                const QString fallbackMsg = modelsReply->errorString();
                modelsReply->deleteLater();

                if (!safePage) {
                    nam->deleteLater();
                    return;
                }

                const bool ok = (httpStatus >= 200 && httpStatus < 300);
                if (!ok) {
                    const QString errorMsg = extractErrorMessage(body, fallbackMsg);
                    if (httpStatus == 401 || httpStatus == 403) {
                        safePage->setFieldError(providerId, QStringLiteral("apiKey"), QObject::tr("鉴权失败，请检查 API Key"));
                    }
                    safePage->setTestStatus(ModelConfigImportPage::TestStatus::Failed, QObject::tr("地址可达，但拉取模型失败（HTTP %1）：%2").arg(httpStatus).arg(errorMsg));
                    nam->deleteLater();
                    return;
                }

                const QStringList modelIds = parseModelIdsFromResponse(body);
                if (!modelIds.isEmpty()) {
                    safePage->setFieldOptions(providerId, QStringLiteral("modelId"), modelIds, true);
                    safePage->setTestStatus(ModelConfigImportPage::TestStatus::Success, QObject::tr("连接成功，发现 %1 个可用模型").arg(modelIds.size()));
                } else {
                    safePage->setTestStatus(ModelConfigImportPage::TestStatus::Success, QObject::tr("连接成功，但未返回模型列表，可手动输入模型名称"));
                }

                nam->deleteLater();
            });
        });
    });

    connect(page, &ModelConfigImportPage::importFromFileRequested, this, [this, page]() {
        QString path = QFileDialog::getOpenFileName(this, tr("从文件导入配置"), QString(), tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (!f.open(QFile::ReadOnly | QFile::Text)) {
            QMessageBox::warning(this, tr("打开失败"), tr("无法读取文件：%1").arg(path));
            return;
        }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        f.close();
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, tr("解析失败"), tr("不是有效的 JSON：%1").arg(err.errorString()));
            return;
        }
        page->setConfigData(doc.object().toVariantMap());
    });

    connect(page, &ModelConfigImportPage::exportRequested, this, [this](const QVariantMap& config) {
        QString path = QFileDialog::getSaveFileName(this, tr("导出配置"), QString(), tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        if (!path.endsWith(".json", Qt::CaseInsensitive))
            path.append(".json");
        QFile f(path);
        if (!f.open(QFile::WriteOnly | QFile::Text)) {
            QMessageBox::warning(this, tr("保存失败"), tr("无法写入文件：%1").arg(path));
            return;
        }
        f.write(QJsonDocument(QJsonObject::fromVariantMap(config)).toJson(QJsonDocument::Indented));
        f.close();
        QMessageBox::information(this, tr("已导出"), tr("已保存到 %1").arg(path));
    });

    dlg->exec();
    dlg->deleteLater();
}
