#include "AgentChatWidget.h"
#include "ToolLogWidget.h"
#include "chat_widget.h"
#include "chat_widget_input.h"
#include "chat_list_widget.h"
#include "chat_list_view.h"
#include "chat_list_roles.h"
#include "core/agent/ToolDispatcher.h"
#include "core/agent/McpToolProvider.h"
#include "core/utils/ModelConfigLoader.h"
#include "newCore/ModelFactory.h"
#include "newCore/OpenAICompatibleProvider.h"
#include "newCore/LLMTypes.h"
#include "modelconfig/model_config_import_page.h"
#include <QAbstractItemModel>
#include <QAction>
#include <QColor>
#include <QDebug>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTime>
#include <QTimer>
#include <QProcessEnvironment>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>

AgentChatWidget::AgentChatWidget(QWidget* parent)
    : QWidget(parent)
{
    // ModelFactory 采用全局单例
    m_modelFactory = ModelFactory::instance();

    // ToolDispatcher 采用全局单例
    m_toolDispatcher = ToolDispatcher::instance();
    m_toolDispatcher->registerDefaultTools(); // 注册默认工具

    m_mcpProvider = new McpToolProvider(m_toolDispatcher);
    m_toolDispatcher->registerProvider(m_mcpProvider, "mcp");
    applyMcpConfig(loadMcpConfigSpecs());

    // NOTE: 将 ToolDispatcher 传给 Agent，实现自治执行（会自动注册工具）
    // 具体 Agent 在会话创建时设置 ToolDispatcher

    setupUI();
    loadConfig();

    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] { saveSessionsToDisk(); });
}

void AgentChatWidget::setupUI()
{
    setWindowTitle("TmAgent - Team of Agents");
    resize(1200, 600); // 扩大窗口宽度以容纳三列

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    // --- 左侧：会话列表 + 厂商导入 / 工具日志 ---
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
    showLogBtn->setStyleSheet("background-color: #607D8B; color: white; font-weight: bold; padding: 5px;");
    connect(showLogBtn, &QPushButton::clicked, this, [this]() {
        if (!m_toolLogWindow) {
            m_toolLogWindow = new ToolLogWidget();
        }
        m_toolLogWindow->show();
        m_toolLogWindow->raise();
        m_toolLogWindow->activateWindow();
    });

    QVBoxLayout* btnLayout = new QVBoxLayout();
    btnLayout->addWidget(modelImportBtn);
    btnLayout->addWidget(mcpConfigBtn);
    btnLayout->addWidget(showLogBtn);
    leftLayout->addLayout(btnLayout);

    splitter->addWidget(leftContainer);

    // --- 右侧：交流面板 ---
    QWidget* centerContainer = new QWidget(this);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerContainer);
    centerLayout->setContentsMargins(0, 0, 0, 0);

    m_chatWidget = new ChatWidget(this);
    m_chatWidget->applyStyleSheetFile("chat_widget.qss");
    centerLayout->addWidget(m_chatWidget, 1);

    splitter->addWidget(centerContainer);

    // --- 右侧:对话历史面板 ---
    QWidget* historyContainer = new QWidget(this);
    QVBoxLayout* historyLayout = new QVBoxLayout(historyContainer);
    historyLayout->setContentsMargins(0, 0, 0, 0);

    m_historyLabel = new QLabel("对话历史 (共 0 轮)", this);
    QFont labelFont = m_historyLabel->font();
    labelFont.setBold(true);
    m_historyLabel->setFont(labelFont);
    historyLayout->addWidget(m_historyLabel);

    m_historyDisplay = new QTextBrowser(this);
    m_historyDisplay->setPlaceholderText("对话历史将在此显示...");
    historyLayout->addWidget(m_historyDisplay, 1);

    m_clearHistoryBtn = new QPushButton("清空历史", this);
    historyLayout->addWidget(m_clearHistoryBtn);

    splitter->addWidget(historyContainer);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes(QList<int>() << 300 << 580 << 320);

    mainLayout->addWidget(splitter);

    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &AgentChatWidget::onClearHistoryClicked);
    connect(m_chatWidget, &ChatWidget::messageSent, this, &AgentChatWidget::onUserMessageSent);
    connect(m_chatWidget, &ChatWidget::stopRequested, this, &AgentChatWidget::onAbortClicked);
    connect(m_chatListWidget, &ChatListWidget::headerActionTriggered, this, [this](QAction *action) {
        QString data = action->data().toString();
        if (data == QLatin1String("new_chat"))
            onNewChatRequested();
        else if (data == QLatin1String("remove_current"))
            onRemoveCurrentChatRequested();
    });
    connect(m_chatListWidget, &ChatListWidget::chatItemActivated, this, &AgentChatWidget::onChatItemActivated);

    if (!loadSessionsFromDisk()) {
        m_chatListWidget->addChatItem(tr("新对话"), QString(), QString(), QColor(Qt::gray), 0);
        setCurrentAgentForRow(0);
        if (m_currentAgent)
            m_currentAgent->setHistory(QJsonArray());
    }

    // 适配 ChatWidget 输入子组件的额外信号（语音等）
    if (ChatWidgetInput* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget())) {
        connect(input, &ChatWidgetInput::voiceStartRequested, this, &AgentChatWidget::onVoiceStartRequested);
        connect(input, &ChatWidgetInput::voiceStopRequested, this, &AgentChatWidget::onVoiceStopRequested);
    }
}

LLMAgent* AgentChatWidget::agentForRow(int row) const
{
    return m_sessionAgents.value(row, nullptr);
}

LLMAgent* AgentChatWidget::ensureAgentForRow(int row)
{
    if (row < 0)
        return nullptr;
    if (LLMAgent* existing = agentForRow(row))
        return existing;

    auto* agent = new LLMAgent(this);
    agent->setModelFactory(m_modelFactory);
    agent->setConfig(m_defaultAgentConfig);
    if (m_toolDispatcher)
        agent->setToolDispatcher(m_toolDispatcher);
    connectAgentSignals(agent);
    m_sessionAgents.insert(row, agent);
    return agent;
}

void AgentChatWidget::connectAgentSignals(LLMAgent* agent)
{
    if (!agent)
        return;
    connect(agent, &LLMAgent::streamDataReceived, this, &AgentChatWidget::onStreamDataReceived);
    connect(agent, &LLMAgent::finished, this, &AgentChatWidget::onFinished);
    connect(agent, &LLMAgent::errorOccurred, this, &AgentChatWidget::onErrorOccurred);
    connect(agent, &LLMAgent::toolCallsStarted, this, &AgentChatWidget::onToolCallsStarted);
    connect(agent, &LLMAgent::toolEvent, this, &AgentChatWidget::onToolEvent);
}

void AgentChatWidget::setCurrentAgentForRow(int row)
{
    m_currentSessionRow = row;
    m_currentAgent = ensureAgentForRow(row);
}

void AgentChatWidget::applyConfigToAllAgents()
{
    for (LLMAgent* agent : m_sessionAgents) {
        if (agent)
            agent->setConfig(m_defaultAgentConfig);
    }
    if (m_currentAgent)
        m_currentAgent->setConfig(m_defaultAgentConfig);
}

void AgentChatWidget::applyToolDispatcherToAllAgents()
{
    if (!m_toolDispatcher)
        return;
    for (LLMAgent* agent : m_sessionAgents) {
        if (agent)
            agent->setToolDispatcher(m_toolDispatcher);
    }
    if (m_currentAgent)
        m_currentAgent->setToolDispatcher(m_toolDispatcher);
}

QJsonArray AgentChatWidget::historyForRow(int row) const
{
    QJsonArray history;
    if (LLMAgent* agent = m_sessionAgents.value(row, nullptr))
        history = agent->getHistory();
    else
        history = m_sessionHistories.value(row);

    if (row == m_streamingForSessionRow && !m_currentAssistantReply.isEmpty()) {
        QJsonObject part;
        part.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        part.insert(QStringLiteral("content"), m_currentAssistantReply);
        history.append(part);
    }
    return history;
}

void AgentChatWidget::removeAgentForRow(int row)
{
    LLMAgent* agent = m_sessionAgents.take(row);
    if (agent)
        agent->deleteLater();
    if (m_currentSessionRow == row)
        m_currentAgent = nullptr;
}

void AgentChatWidget::reindexAgentsAfterRemoval(int removedRow)
{
    if (removedRow < 0)
        return;
    QHash<int, LLMAgent*> newAgents;
    for (auto it = m_sessionAgents.begin(); it != m_sessionAgents.end(); ++it) {
        const int row = it.key();
        const int newRow = row > removedRow ? (row - 1) : row;
        newAgents.insert(newRow, it.value());
    }
    m_sessionAgents = newAgents;
}

void AgentChatWidget::setSendingState(bool isSending)
{
    ChatWidgetInput* input = nullptr;
    if (m_chatWidget) {
        input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget());
        if (input) {
            input->setSendingState(isSending);
        }
    }
}

void AgentChatWidget::loadConfig()
{
    QString yamlPath = QCoreApplication::applicationDirPath() + "/resources/models.yaml";
    
    // 如果 models.yaml 不存在，创建空模板
    if (!QFile::exists(yamlPath)) {
        QVector<ModelConfig> emptyModels;
        ModelConfigLoader::saveToFile(yamlPath, emptyModels, "");
        QMessageBox::information(this, tr("欢迎使用"), 
            tr("请点击「从厂商导入…」按钮添加模型配置。"));
        return;
    }
    
    // 加载所有模型配置
    QVector<ModelConfig> models = ModelConfigLoader::loadFromFile(yamlPath);
    
    if (models.isEmpty()) {
        QMessageBox::information(this, tr("未配置模型"), 
            tr("请点击「从厂商导入…」按钮添加模型配置。"));
        return;
    }
    
    // 注册所有模型到 ModelFactory
    for (const ModelConfig& config : models) {
        m_modelFactory->registerModelConfig(config);
    }
    
    // 获取默认模型 ID
    QString defaultModelId = ModelConfigLoader::getDefaultModelId(yamlPath);
    if (defaultModelId.isEmpty()) {
        defaultModelId = models.first().modelId;
    }
    
    // 获取默认模型配置（包含 systemPrompt）
    ModelConfig defaultConfig = ModelConfigLoader::getModelConfig(yamlPath, defaultModelId);
    
    // 设置到 Agent
    LLMConfig agentConfig;
    agentConfig.model = defaultModelId;
    agentConfig.systemPrompt = defaultConfig.systemPrompt;  // 从模型配置读取
    agentConfig.temperature = defaultConfig.temperature;
    agentConfig.apiKey = defaultConfig.apiKey;
    agentConfig.baseUrl = defaultConfig.baseUrl;
    m_defaultAgentConfig = agentConfig;
    applyConfigToAllAgents();
    
    qInfo() << "已加载" << models.size() << "个模型，默认:" << defaultModelId;
}

QString AgentChatWidget::mcpConfigPath() const
{
    QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
    return dir + QStringLiteral("/mcp_servers.json");
}

QStringList AgentChatWidget::loadMcpConfigSpecs() const
{
    QStringList specs;
    QFile f(mcpConfigPath());
    if (!f.open(QFile::ReadOnly | QFile::Text))
        return specs;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return specs;

    QJsonArray arr = doc.object().value(QStringLiteral("servers")).toArray();
    for (const QJsonValue& v : arr) {
        const QString spec = v.toString().trimmed();
        if (!spec.isEmpty())
            specs.append(spec);
    }
    return specs;
}

bool AgentChatWidget::saveMcpConfigSpecs(const QStringList& specs) const
{
    QJsonArray arr;
    for (const QString& spec : specs) {
        if (!spec.trimmed().isEmpty())
            arr.append(spec.trimmed());
    }
    QJsonObject root;
    root.insert(QStringLiteral("servers"), arr);

    QString path = mcpConfigPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QFile::WriteOnly | QFile::Text))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void AgentChatWidget::applyMcpConfig(const QStringList& specs)
{
    if (!m_mcpProvider)
        return;

    m_mcpProvider->clearServers();
    for (const QString& spec : specs) {
        if (!m_mcpProvider->addServerFromSpec(spec)) {
            qWarning() << "MCP server spec 无效:" << spec;
        }
    }

    // 可选: 通过环境变量追加 MCP server
    // 格式: TMAGENT_MCP_SERVERS="name|url|token|header|prefix|async;name2|url2|token2"
    const QString envSpec = QProcessEnvironment::systemEnvironment().value("TMAGENT_MCP_SERVERS");
    if (!envSpec.trimmed().isEmpty()) {
        const QStringList servers = envSpec.split(';', Qt::SkipEmptyParts);
        for (const QString& serverSpec : servers) {
            if (!m_mcpProvider->addServerFromSpec(serverSpec)) {
                qWarning() << "MCP server spec 无效(ENV):" << serverSpec;
            }
        }
    }

    if (m_toolDispatcher) {
        m_toolDispatcher->refreshProvider(QStringLiteral("mcp"));
    }
}

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
    const QStringList specs = loadMcpConfigSpecs();
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

    if (!saveMcpConfigSpecs(newSpecs)) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法写入 MCP 配置文件。"));
        return;
    }

    applyMcpConfig(newSpecs);
    applyToolDispatcherToAllAgents();
    QMessageBox::information(this, tr("配置已保存"), tr("MCP 配置已更新。"));
}


void AgentChatWidget::onNewChatRequested()
{
    if (m_chatListWidget && m_currentSessionRow >= 0) {
        QJsonArray toSave = historyForRow(m_currentSessionRow);
        if (m_hasPendingAssistantMessage && !m_currentAssistantReply.isEmpty()) {
            QJsonObject part;
            part.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            part.insert(QStringLiteral("content"), m_currentAssistantReply);
            toSave.append(part);
        }
        m_sessionHistories[m_currentSessionRow] = toSave;
    }
    if (m_streamingForSessionRow >= 0) {
        if (LLMAgent* streamingAgent = agentForRow(m_streamingForSessionRow)) {
            streamingAgent->abort();
        }
        m_streamingForSessionRow = -1;
        setSendingState(false);
    }
    if (m_chatWidget)
        clearChatMessages();
    m_currentAssistantReply.clear();
    m_hasPendingAssistantMessage = false;
    m_lastMsgIsTool = false;
    if (m_chatListWidget) {
        int newRow = m_chatListWidget->addChatItem(tr("新对话"), QString(), QString(), QColor(Qt::gray), 0);
        setCurrentAgentForRow(newRow);
        if (m_currentAgent)
            m_currentAgent->clearHistory();
        m_sessionHistories[newRow] = QJsonArray();
        QAbstractItemModel *model = m_chatListWidget->listView()->model();
        if (model && model->rowCount() > 0) {
            QModelIndex last = model->index(model->rowCount() - 1, 0);
            if (last.isValid())
                m_chatListWidget->listView()->setCurrentIndex(last);
        }
        updateHistoryDisplay();
        saveSessionsToDisk();
    }
}

void AgentChatWidget::onChatItemActivated(const QString &name, const QString &message, const QString &time,
                                          const QColor &avatarColor, int unreadCount)
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
    if (QSortFilterProxyModel *proxy = qobject_cast<QSortFilterProxyModel *>(m_chatListWidget->listView()->model())) {
        row = proxy->mapToSource(idx).row();
    } else {
        row = idx.row();
    }
    if (row == m_currentSessionRow)
        return;

    // 保存当前会话（含未收完的部分回复），然后切到新会话；不中断进行中的请求
    QJsonArray toSave = historyForRow(m_currentSessionRow);
    if (m_hasPendingAssistantMessage && !m_currentAssistantReply.isEmpty()) {
        QJsonObject part;
        part.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        part.insert(QStringLiteral("content"), m_currentAssistantReply);
        toSave.append(part);
    }
    m_sessionHistories[m_currentSessionRow] = toSave;

    setCurrentAgentForRow(row);
    QJsonArray h = historyForRow(row);
    restoreChatFromHistory(h);
    updateHistoryDisplayFrom(h); // 右侧历史面板始终显示当前看的会话
    // 仅当没有进行中的请求时，重置当前显示相关缓存
    if (m_streamingForSessionRow < 0) {
        m_currentAssistantReply.clear();
        m_hasPendingAssistantMessage = false;
        m_lastMsgIsTool = false;
    }
    saveSessionsToDisk();
}

void AgentChatWidget::onRemoveCurrentChatRequested()
{
    if (!m_chatListWidget || !m_chatWidget)
        return;
    QStandardItemModel *src = m_chatListWidget->listView()->standardModel();
    if (!src)
        return;
    const int n = src->rowCount();
    if (n <= 0)
        return;
    const int rowToRemove = m_currentSessionRow;
    if (rowToRemove < 0 || rowToRemove >= n)
        return;

    QJsonArray currentSessionHistory = historyForRow(rowToRemove);
    if (m_hasPendingAssistantMessage && !m_currentAssistantReply.isEmpty()) {
        QJsonObject part;
        part.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        part.insert(QStringLiteral("content"), m_currentAssistantReply);
        currentSessionHistory.append(part);
    }
    QHash<int, QJsonArray> newHistories;
    for (int i = 0, ni = 0; i < n; ++i) {
        if (i == rowToRemove)
            continue;
        newHistories[ni++] = (i == m_currentSessionRow) ? currentSessionHistory : historyForRow(i);
    }

    if (m_streamingForSessionRow == rowToRemove) {
        if (LLMAgent* streamingAgent = agentForRow(rowToRemove)) {
            streamingAgent->abort();
        }
        m_streamingForSessionRow = -1;
        setSendingState(false);
    }
    m_currentAssistantReply.clear();
    m_hasPendingAssistantMessage = false;
    m_sessionHistories = newHistories;
    removeAgentForRow(rowToRemove);
    reindexAgentsAfterRemoval(rowToRemove);

    if (!m_chatListWidget->removeCurrentChat())
        return;

    const int newCount = m_chatListWidget->listView()->standardModel()->rowCount();
    if (newCount <= 0) {
        m_sessionHistories.clear();
        for (LLMAgent* agent : m_sessionAgents) {
            if (agent)
                agent->deleteLater();
        }
        m_sessionAgents.clear();
        m_currentAgent = nullptr;
        m_currentSessionRow = 0;
        m_chatListWidget->addChatItem(tr("新对话"), QString(), QString(), QColor(Qt::gray), 0);
        setCurrentAgentForRow(0);
        if (m_currentAgent)
            m_currentAgent->clearHistory();
        m_sessionHistories[0] = QJsonArray();
        clearChatMessages();
        updateHistoryDisplay();
        saveSessionsToDisk();
        return;
    }

    const int newRow = (rowToRemove >= newCount) ? (newCount - 1) : rowToRemove;
    setCurrentAgentForRow(newRow);
    QAbstractItemModel *model = m_chatListWidget->listView()->model();
    QModelIndex sel;
    if (QSortFilterProxyModel *proxy = qobject_cast<QSortFilterProxyModel *>(model))
        sel = proxy->mapFromSource(m_chatListWidget->listView()->standardModel()->index(newRow, 0));
    else if (model)
        sel = model->index(newRow, 0);
    if (sel.isValid())
        m_chatListWidget->listView()->setCurrentIndex(sel);

    QJsonArray h = historyForRow(newRow);
    restoreChatFromHistory(h);
    updateHistoryDisplay();
    saveSessionsToDisk();
}

void AgentChatWidget::clearChatMessages()
{
    if (!m_chatWidget)
        return;
    m_chatWidget->clearMessages();
}

void AgentChatWidget::restoreChatFromHistory(const QJsonArray& history)
{
    if (!m_chatWidget)
        return;
    clearChatMessages();
    for (const QJsonValue& v : history) {
        QJsonObject o = v.toObject();
        QString role = o["role"].toString();
        QString content = o["content"].toString();
        if (content.isEmpty())
            continue;
        bool isMine = (role == QLatin1String("user"));
        m_chatWidget->addMessage(content, isMine, isMine ? QStringLiteral("Me") : QStringLiteral("TM Agent"));
    }
}

QString AgentChatWidget::sessionsFilePath() const
{
    QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
    return dir + QStringLiteral("/chat_sessions.json");
}

void AgentChatWidget::saveSessionsToDisk()
{
    if (!m_chatListWidget)
        return;
    QStandardItemModel *src = m_chatListWidget->listView()->standardModel();
    if (!src || src->rowCount() == 0)
        return;
    QJsonObject root;
    root.insert(QStringLiteral("currentRow"), m_currentSessionRow);
    QJsonArray arr;
    for (int r = 0; r < src->rowCount(); ++r) {
        QModelIndex idx = src->index(r, 0);
        QJsonObject s;
        s.insert(QStringLiteral("name"), idx.data(ChatListNameRole).toString());
        s.insert(QStringLiteral("message"), idx.data(ChatListMessageRole).toString());
        s.insert(QStringLiteral("time"), idx.data(ChatListTimeRole).toString());
        QJsonArray hist = historyForRow(r);
        s.insert(QStringLiteral("history"), hist);
        arr.append(s);
    }
    root.insert(QStringLiteral("sessions"), arr);
    QString path = sessionsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QFile::WriteOnly | QFile::Text))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool AgentChatWidget::loadSessionsFromDisk()
{
    if (!m_chatListWidget || !m_chatWidget)
        return false;
    QFile f(sessionsFilePath());
    if (!f.open(QFile::ReadOnly | QFile::Text))
        return false;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    QJsonObject root = doc.object();
    QJsonArray arr = root[QStringLiteral("sessions")].toArray();
    if (arr.isEmpty())
        return false;
    int currentRow = root[QStringLiteral("currentRow")].toInt(0);
    m_chatListWidget->clearChats();
    m_sessionHistories.clear();
    for (LLMAgent* agent : m_sessionAgents) {
        if (agent)
            agent->deleteLater();
    }
    m_sessionAgents.clear();
    m_currentAgent = nullptr;
    for (const QJsonValue& v : arr) {
        QJsonObject s = v.toObject();
        QString name = s[QStringLiteral("name")].toString();
        if (name.isEmpty())
            name = tr("新对话");
        m_chatListWidget->addChatItem(name,
                                      s[QStringLiteral("message")].toString(),
                                      s[QStringLiteral("time")].toString(),
                                      QColor(Qt::gray), 0);
        const int row = m_sessionHistories.size();
        QJsonArray history = s[QStringLiteral("history")].toArray();
        m_sessionHistories[row] = history;
        if (LLMAgent* agent = ensureAgentForRow(row)) {
            agent->setHistory(history);
        }
    }
    int n = m_sessionHistories.size();
    setCurrentAgentForRow(qBound(0, currentRow, n - 1));
    QJsonArray h = historyForRow(m_currentSessionRow);
    restoreChatFromHistory(h);
    m_currentAssistantReply.clear();
    m_hasPendingAssistantMessage = false;
    m_lastMsgIsTool = false;
    updateHistoryDisplay();
    QAbstractItemModel *model = m_chatListWidget->listView()->model();
    QModelIndex sel;
    if (QSortFilterProxyModel *proxy = qobject_cast<QSortFilterProxyModel *>(model))
        sel = proxy->mapFromSource(m_chatListWidget->listView()->standardModel()->index(m_currentSessionRow, 0));
    else if (model)
        sel = model->index(m_currentSessionRow, 0);
    if (sel.isValid())
        m_chatListWidget->listView()->setCurrentIndex(sel);
    return true;
}

static QString inferProviderIdFromBaseUrl(const QString& baseUrl)
{
    const QString u = baseUrl.trimmed().toLower();
    if (u.contains("deepseek")) return QStringLiteral("deepseek");
    if (u.contains("openai.com")) return QStringLiteral("openai");
    if (u.contains("anthropic")) return QStringLiteral("claude");
    if (u.contains("localhost:11434") || u.contains("ollama")) return QStringLiteral("ollama");
    if (u.contains("generativelanguage") || u.contains("googleapis")) return QStringLiteral("gemini");
    return QString();
}

static QList<ModelConfigProvider> defaultModelConfigProviders()
{
    QList<ModelConfigProvider> list;
    ModelConfigProvider deepseek{"deepseek", "DeepSeek", "中国高性能 AI 模型"};
    deepseek.fields << ModelConfigField{"apiKey", "API 密钥", "sk-...", "", true, true};
    deepseek.fields << ModelConfigField{"modelId", "模型名称", "deepseek-chat", "deepseek-chat"};
    deepseek.fields << ModelConfigField{"baseUrl", "接口地址", "https://api.deepseek.com", "https://api.deepseek.com"};
    list << deepseek;

    ModelConfigProvider openai{"openai", "OpenAI", "全球领先的 AI 语言模型"};
    openai.fields << ModelConfigField{"apiKey", "API 密钥", "sk-...", "", true, true};
    openai.fields << ModelConfigField{"modelId", "模型名称", "gpt-4o", "gpt-4o"};
    openai.fields << ModelConfigField{"baseUrl", "接口地址", "https://api.openai.com/v1", "https://api.openai.com/v1"};
    list << openai;

    ModelConfigProvider claude{"claude", "Claude", "Anthropic 强大的 AI 模型"};
    claude.fields << ModelConfigField{"apiKey", "API 密钥", "sk-ant-...", "", true, true};
    claude.fields << ModelConfigField{"modelId", "模型名称", "claude-3-5-sonnet", "claude-3-5-sonnet"};
    claude.fields << ModelConfigField{"baseUrl", "接口地址", "https://api.anthropic.com/v1", "https://api.anthropic.com/v1"};
    list << claude;

    ModelConfigProvider ollama{"ollama", "Ollama", "本地运行的各类型开源模型"};
    ollama.fields << ModelConfigField{"modelId", "模型名称", "llama3", "llama3"};
    ollama.fields << ModelConfigField{"baseUrl", "接口地址", "http://localhost:11434", "http://localhost:11434"};
    list << ollama;

    ModelConfigProvider gemini{"gemini", "Gemini", "Google 强大的 AI 服务"};
    gemini.fields << ModelConfigField{"apiKey", "API 密钥", "在此输入密钥", "", true, true};
    gemini.fields << ModelConfigField{"modelId", "模型名称", "gemini-1.5-pro", "gemini-1.5-pro"};
    gemini.fields << ModelConfigField{"baseUrl", "接口地址", "https://generativelanguage.googleapis.com", ""};
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

    // 从 models.yaml 读取现有配置作为初始值
    QString yamlPath = QCoreApplication::applicationDirPath() + "/resources/models.yaml";
    QString defaultModelId = ModelConfigLoader::getDefaultModelId(yamlPath);
    
    QVariantMap initial;
    if (!defaultModelId.isEmpty()) {
        ModelConfig existingConfig = ModelConfigLoader::getModelConfig(yamlPath, defaultModelId);
        QString pid = inferProviderIdFromBaseUrl(existingConfig.baseUrl);
        if (pid.isEmpty()) pid = QStringLiteral("deepseek");
        
        initial["providerId"] = pid;
        initial["apiKey"] = existingConfig.apiKey;
        initial["baseUrl"] = existingConfig.baseUrl;
        initial["modelId"] = existingConfig.modelId;
    } else {
        initial["providerId"] = QStringLiteral("deepseek");
    }
    page->setConfigData(initial);

    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(page);

    connect(page, &ModelConfigImportPage::importRequested, this, [this, dlg, yamlPath](const QVariantMap& config) {
        // 构建 ModelConfig
        ModelConfig modelConfig;
        modelConfig.modelId = config.value("modelId").toString().trimmed();
        modelConfig.displayName = config.value("providerName").toString();
        modelConfig.provider = config.value("providerId").toString();
        modelConfig.apiKey = config.value("apiKey").toString().trimmed();
        modelConfig.baseUrl = config.value("baseUrl").toString().trimmed();
        modelConfig.authType = "Bearer";
        modelConfig.temperature = 0.7;
        modelConfig.maxTokens = 4096;
        modelConfig.timeoutMs = 180000;
        modelConfig.capabilities << Capability::TextGeneration << Capability::ToolCalling;
        modelConfig.toolCalling = true;
        
        // 默认系统提示词
        modelConfig.systemPrompt = tr("你是一个专业的 Qt 高级开发工程师，精通 C++、Qt 框架和跨平台开发。");
        
        // 保存到 YAML
        ModelConfigLoader::addOrUpdateModel(yamlPath, modelConfig);
        ModelConfigLoader::setDefaultModelId(yamlPath, modelConfig.modelId);
        
        // 注册到 Factory
        m_modelFactory->registerModelConfig(modelConfig);
        
        // 切换 Agent 配置
        LLMConfig agentConfig;
        agentConfig.model = modelConfig.modelId;
        agentConfig.systemPrompt = modelConfig.systemPrompt;
        agentConfig.temperature = modelConfig.temperature;
        agentConfig.apiKey = modelConfig.apiKey;
        agentConfig.baseUrl = modelConfig.baseUrl;
        m_defaultAgentConfig = agentConfig;
        applyConfigToAllAgents();
        
        dlg->accept();
        QMessageBox::information(this, tr("已导入"), 
            tr("已从「%1」导入配置并保存到 models.yaml").arg(config.value("providerName").toString()));
    });
    connect(page, &ModelConfigImportPage::cancelled, dlg, &QDialog::reject);

    connect(page, &ModelConfigImportPage::testConnectionRequested, this, [page](const QVariantMap&) {
        page->setTestStatus(ModelConfigImportPage::TestStatus::Testing, tr("验证中…"));
        QTimer::singleShot(800, page, [page]() {
            page->setTestStatus(ModelConfigImportPage::TestStatus::Success, tr("可在主界面保存后发送消息验证"));
        });
    });

    connect(page, &ModelConfigImportPage::importFromFileRequested, this, [this, page]() {
        QString path = QFileDialog::getOpenFileName(this, tr("从文件导入配置"), QString(), tr("JSON (*.json)"));
        if (path.isEmpty()) return;
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
        if (path.isEmpty()) return;
        if (!path.endsWith(".json", Qt::CaseInsensitive)) path.append(".json");
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

void AgentChatWidget::onUserMessageSent(const QString& content)
{
    QString prompt = content.trimmed();
    if (prompt.isEmpty())
        return;

    m_streamingForSessionRow = m_currentSessionRow;
    m_currentAssistantReply.clear();
    m_hasPendingAssistantMessage = false;
    m_lastMsgIsTool = false;
    setSendingState(true);
    if (m_chatListWidget && m_currentSessionRow >= 0) {
        QStandardItemModel *src = m_chatListWidget->listView()->standardModel();
        if (src && m_currentSessionRow < src->rowCount()) {
            QString name = src->index(m_currentSessionRow, 0).data(ChatListNameRole).toString();
            if (name.isEmpty())
                name = tr("新对话");
            QString preview = prompt;
            if (preview.length() > 80)
                preview = preview.left(80) + QStringLiteral("...");
            m_chatListWidget->updateChatItem(m_currentSessionRow, name, preview,
                                            QTime::currentTime().toString(QStringLiteral("hh:mm")),
                                            QColor(Qt::gray), 0);
        }
    }
    // 使用 sendMessage，已注册工具会自动附带
    if (!m_currentAgent)
        setCurrentAgentForRow(m_currentSessionRow);
    if (m_currentAgent)
        m_currentAgent->sendMessage(prompt);
}

void AgentChatWidget::onAbortClicked()
{
    qDebug() << "------------------------------------------";
    qDebug() << "AgentChatWidget: [Signal Received] Stop requested by User UI";

    // 中断并回滚，获取被回滚的用户消息
    LLMAgent* targetAgent = nullptr;
    if (m_streamingForSessionRow >= 0)
        targetAgent = agentForRow(m_streamingForSessionRow);
    if (!targetAgent)
        targetAgent = m_currentAgent;
    QString rolledBackUserMsg = targetAgent ? targetAgent->abortAndRollback() : QString();

    if (m_chatWidget && m_streamingForSessionRow == m_currentSessionRow) {
        m_chatWidget->addMessage("[已手动中断]", false, "System");

        // 将用户消息恢复到输入框
        if (!rolledBackUserMsg.isEmpty()) {
            if (auto* input = qobject_cast<ChatWidgetInput*>(m_chatWidget->inputWidget())) {
                if (auto* edit = input->findChild<QLineEdit*>("chatWidgetInputEdit")) {
                    edit->setText(rolledBackUserMsg);
                    edit->setFocus();
                }
            }
        }
    }
    m_hasPendingAssistantMessage = false;
    m_lastMsgIsTool = false;
    if (m_streamingForSessionRow == m_currentSessionRow)
        updateHistoryDisplay();
    setSendingState(false);
}

void AgentChatWidget::onVoiceStartRequested()
{
    if (m_chatWidget) {
        m_chatWidget->addMessage("[语音输入功能暂未接入]", false, "System");
    }
}

void AgentChatWidget::onVoiceStopRequested()
{
    // 与 onVoiceStartRequested 配对，当前无实际操作
}

void AgentChatWidget::onStreamDataReceived(const QString& data)
{
    if (!m_chatWidget)
        return;
    LLMAgent* agent = qobject_cast<LLMAgent*>(sender());
    int streamRow = agent ? m_sessionAgents.key(agent, -1) : -1;
    if (streamRow < 0)
        streamRow = m_streamingForSessionRow;
    if (streamRow < 0)
        return;
    // 仅当正在回复的会话就是当前显示的会话时，才更新聊天区域
    if (streamRow != m_currentSessionRow) {
        m_currentAssistantReply += data;
        return;
    }
    if (!m_hasPendingAssistantMessage) {
        m_chatWidget->addMessage("", false, "TM Agent");
        m_hasPendingAssistantMessage = true;
        m_lastMsgIsTool = false;
    }
    m_currentAssistantReply += data;
    m_chatWidget->streamOutput(data);
}

void AgentChatWidget::onToolCallsStarted()
{
    if (!m_chatWidget)
        return;
    LLMAgent* agent = qobject_cast<LLMAgent*>(sender());
    int streamRow = agent ? m_sessionAgents.key(agent, -1) : -1;
    if (streamRow < 0)
        streamRow = m_streamingForSessionRow;
    if (streamRow != m_currentSessionRow)
        return;
    if (m_hasPendingAssistantMessage) {
        m_chatWidget->removeLastMessage();
        m_hasPendingAssistantMessage = false;
        m_currentAssistantReply.clear();
    }
    m_lastMsgIsTool = false;
}

void AgentChatWidget::onFinished(const QString& fullContent)
{
    LLMAgent* agent = qobject_cast<LLMAgent*>(sender());
    const int forRow = agent ? m_sessionAgents.key(agent, -1) : m_streamingForSessionRow;
    m_hasPendingAssistantMessage = false;
    m_currentAssistantReply.clear();
    m_streamingForSessionRow = -1;
    setSendingState(false);

    if (forRow < 0)
        return;
    // 把本次回复记到所属会话里（Agent 里已是该会话历史 + 本条，可直接取）
    if (agent)
        m_sessionHistories[forRow] = agent->getHistory();
    else
        m_sessionHistories[forRow] = historyForRow(forRow);
    if (m_chatListWidget) {
        QStandardItemModel *src = m_chatListWidget->listView()->standardModel();
        if (src && forRow < src->rowCount()) {
            QString name = src->index(forRow, 0).data(ChatListNameRole).toString();
            if (name.isEmpty())
                name = tr("新对话");
            QString preview = fullContent.trimmed();
            if (preview.length() > 80)
                preview = preview.left(80) + QStringLiteral("...");
            m_chatListWidget->updateChatItem(forRow, name, preview,
                                            QTime::currentTime().toString(QStringLiteral("hh:mm")),
                                            QColor(Qt::gray), 0);
        }
    }
    saveSessionsToDisk();

    if (!m_chatWidget)
        return;
    // 仅当完成的是当前显示的会话时，才刷新聊天区域与历史面板
    if (forRow == m_currentSessionRow) {
        if (!fullContent.isEmpty())
            m_chatWidget->addMessage(fullContent, false, "TM Agent");
        updateHistoryDisplay();
    }
}

void AgentChatWidget::updateHistoryDisplayFrom(const QJsonArray& history)
{
    int count = 0;
    for (int i = 0; i < history.size(); i++) {
        if (history[i].toObject()[QStringLiteral("role")].toString() == QLatin1String("user"))
            count++;
    }
    m_historyLabel->setText(QString("对话历史 (共 %1 轮)").arg(count));
    if (history.isEmpty()) {
        m_historyDisplay->clear();
        return;
    }
    QString htmlContent;
    int roundNum = 0;
    for (int i = 0; i < history.size(); i++) {
        QJsonObject msg = history[i].toObject();
        QString role = msg["role"].toString();
        QString content = msg["content"].toString();
        if (role == "user") {
            roundNum++;
            htmlContent += QString("<p><b>第 %1 轮:</b></p>").arg(roundNum);
            htmlContent += QString("<p style='color: blue;'><b>User:</b> %1</p>").arg(content.toHtmlEscaped());
        } else if (role == "assistant") {
            htmlContent += QString("<p style='color: green;'><b>Assistant:</b> %1</p><br>").arg(content.toHtmlEscaped());
        }
    }
    m_historyDisplay->setHtml(htmlContent);
}

void AgentChatWidget::updateHistoryDisplay()
{
    updateHistoryDisplayFrom(historyForRow(m_currentSessionRow));
}

void AgentChatWidget::onClearHistoryClicked()
{
    if (m_currentAgent)
        m_currentAgent->clearHistory();
    m_historyDisplay->clear();
    m_historyLabel->setText("对话历史 (共 0 轮)");
    if (m_chatWidget) {
        m_chatWidget->addMessage("[对话历史已清空]", false, "System");
    }
}

void AgentChatWidget::onErrorOccurred(const QString& errorMsg)
{
    LLMAgent* agent = qobject_cast<LLMAgent*>(sender());
    const int forRow = agent ? m_sessionAgents.key(agent, -1) : m_streamingForSessionRow;
    m_streamingForSessionRow = -1;
    m_hasPendingAssistantMessage = false;
    setSendingState(false);

    if (m_chatWidget && forRow == m_currentSessionRow) {
        m_chatWidget->addMessage(QString("❌ 错误: %1").arg(errorMsg), false, "System");
    }
}

// ==================== 工具事件处理 ====================

void AgentChatWidget::onToolEvent(const ToolExecutionEvent& event)
{
    // 1. 同步到独立日志窗口
    if (m_toolLogWindow) {
        m_toolLogWindow->logEvent(event);
    }

    // 2. 主界面简化处理
    if (!m_chatWidget)
        return;
    LLMAgent* agent = qobject_cast<LLMAgent*>(sender());
    int row = agent ? m_sessionAgents.key(agent, -1) : -1;
    if (row >= 0 && row != m_currentSessionRow)
        return;

    if (m_isDebugMode) {
        if (event.status == "started") {
            m_chatWidget->addMessage(QString("⚡ 正在执行工具: %1").arg(event.toolName), false, "Tool");
            m_lastMsgIsTool = true;
        } else if (event.status == "progress") {
            if (m_lastMsgIsTool)
                m_chatWidget->removeLastMessage();
            m_chatWidget->addMessage(QString("⏳ %1: %2").arg(event.toolName, event.formattedResult), false, "Tool");
            m_lastMsgIsTool = true;
        } else if (event.status == "completed") {
            if (m_lastMsgIsTool)
                m_chatWidget->removeLastMessage();
            QString icon = event.success ? "✅" : "❌";
            m_chatWidget->addMessage(
                QString("%1 %2 完成: %3").arg(icon, event.toolName, event.formattedResult),
                false,
                "Tool");
            m_lastMsgIsTool = true; // Completed is still a tool message, but maybe final one
        }
        return;
    }

    if (event.status == "progress") {
        if (m_lastMsgIsTool)
            m_chatWidget->removeLastMessage();
        m_chatWidget->addMessage(QString("⏳ %1: %2").arg(event.toolName, event.formattedResult), false, "Tool");
        m_lastMsgIsTool = true;
        return;
    }

    if (event.status == "completed") {
        if (event.success) {
            // 成功时，如果上一条是进度信息，则移除它（保持界面清爽）
            if (m_lastMsgIsTool) {
                m_chatWidget->removeLastMessage();
                m_lastMsgIsTool = false;
            }
        } else {
            // 失败时，保留错误提示
            if (m_lastMsgIsTool)
                m_chatWidget->removeLastMessage();
            m_chatWidget->addMessage(QString("❌ %1 执行失败").arg(event.toolName), false, "Tool");
            m_lastMsgIsTool = true;
        }
    } else if (event.status == "started") {
        m_lastMsgIsTool = false; // 重置，为后续 progress 做准备
    }
}
