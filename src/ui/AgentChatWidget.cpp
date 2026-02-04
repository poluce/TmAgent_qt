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
#include "core/utils/KeychainHelper.h"
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
#include <QHeaderView>
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
#include <QStandardPaths>
#include <QTime>
#include <QTimer>
#include <QProcessEnvironment>
#include <QUuid>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>

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

// 辅助函数：构建 MessageParams（适配新版 ChatWidget API）
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
    m_historyDisplay->header()->setStretchLastSection(true);
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
    connect(m_chatListWidget, &ChatListWidget::chatItemRemoved, this, &AgentChatWidget::onChatItemRemoved);
    connect(m_chatListWidget, &ChatListWidget::chatItemRenamed, this, &AgentChatWidget::onChatItemRenamed);

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
    agent->setConfig(configForRow(row));
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
    for (auto it = m_sessionAgents.begin(); it != m_sessionAgents.end(); ++it) {
        LLMAgent* agent = it.value();
        if (agent)
            agent->setConfig(configForRow(it.key(), agent));
    }
    if (m_currentAgent)
        m_currentAgent->setConfig(configForRow(m_currentSessionRow, m_currentAgent));
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

LLMConfig AgentChatWidget::configForRow(int row, const LLMAgent* existing) const
{
    LLMConfig cfg = m_defaultAgentConfig;
    QString uuid = m_sessionUuids.value(row);
    QString name = chatItemNameForRow(row);
    if (name.isEmpty() && existing)
        name = existing->config().userName;
    if (!name.isEmpty())
        cfg.userName = name;
    if (existing) {
        const LLMConfig existingConfig = existing->config();
        if (uuid.isEmpty())
            uuid = existingConfig.uuid;
        cfg.recursionDepth = existingConfig.recursionDepth;
    }
    if (!uuid.isEmpty())
        cfg.uuid = uuid;
    return cfg;
}

QString AgentChatWidget::ensureSessionUuid(int row)
{
    if (row < 0)
        return QString();
    QString uuid = m_sessionUuids.value(row);
    if (!uuid.isEmpty())
        return uuid;
    if (LLMAgent* agent = m_sessionAgents.value(row, nullptr)) {
        uuid = agent->config().uuid;
    }
    if (uuid.isEmpty()) {
        uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_sessionUuids.insert(row, uuid);
    return uuid;
}

QString AgentChatWidget::chatItemNameForRow(int row) const
{
    if (!m_chatListWidget || row < 0)
        return QString();
    QStandardItemModel *src = m_chatListWidget->listView()->standardModel();
    if (!src || row >= src->rowCount())
        return QString();
    return src->index(row, 0).data(ChatListNameRole).toString().trimmed();
}

QString AgentChatWidget::agentDisplayNameForRow(int row) const
{
    QString name = chatItemNameForRow(row);
    if (name.isEmpty()) {
        if (LLMAgent* agent = m_sessionAgents.value(row, nullptr)) {
            name = agent->config().userName.trimmed();
        }
    }
    if (name.isEmpty())
        name = tr("TM Agent");
    return name;
}

QJsonArray AgentChatWidget::historyForRow(int row) const
{
    QJsonArray history;
    if (LLMAgent* agent = m_sessionAgents.value(row, nullptr))
        history = agent->getHistory();
    else
        history = m_sessionHistories.value(row);

    auto it = m_streamStates.constFind(row);
    if (it != m_streamStates.constEnd() && it->isStreaming && !it->buffer.isEmpty()) {
        QJsonObject part;
        part.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        part.insert(QStringLiteral("content"), it->buffer);
        history.append(part);
    }
    return history;
}

QJsonArray AgentChatWidget::ioHistoryForRow(int row) const
{
    if (LLMAgent* agent = m_sessionAgents.value(row, nullptr))
        return agent->getIoHistory();
    return m_sessionIoHistories.value(row);
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

AgentChatWidget::StreamState &AgentChatWidget::streamStateForRow(int row)
{
    return m_streamStates[row];
}

bool AgentChatWidget::isRowStreaming(int row) const
{
    const auto it = m_streamStates.constFind(row);
    return it != m_streamStates.constEnd() && it->isStreaming;
}

void AgentChatWidget::updateSendingStateForCurrentRow()
{
    if (!m_chatWidget)
        return;
    const bool sending = isRowStreaming(m_currentSessionRow);
    m_chatWidget->setSendingState(sending);
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
    QString yamlPath = modelConfigPath();
    if (!QFile::exists(yamlPath)) {
        QDir().mkpath(QFileInfo(yamlPath).absolutePath());
        QString bundledPath = QCoreApplication::applicationDirPath() + "/resources/models.yaml";
        if (QFile::exists(bundledPath)) {
            QFile::copy(bundledPath, yamlPath);
        } else {
            QVector<ModelConfig> emptyModels;
            ModelConfigLoader::saveToFile(yamlPath, emptyModels, "");
        }
    }
    
    // 加载所有模型配置
    QVector<ModelConfig> models = ModelConfigLoader::loadFromFile(yamlPath, true);
    
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
    ModelConfig defaultConfig = ModelConfigLoader::getModelConfig(yamlPath, defaultModelId, true);
    
    // 设置到 Agent
    LLMConfig agentConfig;
    {
        ModelFactory::ParsedModelId parsed = ModelFactory::parseModelKey(defaultModelId);
        agentConfig.model = parsed.model;
        agentConfig.customModelId = parsed.customModelId;
    }
    agentConfig.systemPrompt = defaultConfig.systemPrompt;  // 从模型配置读取
    agentConfig.userName = tr("TM Agent");
    m_defaultAgentConfig = agentConfig;
    applyConfigToAllAgents();
    
    qInfo() << "已加载" << models.size() << "个模型，默认:" << defaultModelId;
}

QString AgentChatWidget::modelConfigPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/resources/models.yaml");
    }
    return QDir(dir).filePath(QStringLiteral("models.yaml"));
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
        m_sessionHistories[m_currentSessionRow] = toSave;
        m_sessionIoHistories[m_currentSessionRow] = ioHistoryForRow(m_currentSessionRow);
    }
    if (m_chatWidget) {
        m_chatWidget->setEmptyStateVisible(false);
        clearChatMessages();
    }
    updateSendingStateForCurrentRow();
    if (m_chatListWidget) {
        int newRow = m_chatListWidget->addChatItem(tr("新对话"), QString(), QString(), QColor(Qt::gray), 0);
        setCurrentAgentForRow(newRow);
        if (m_currentAgent)
            m_currentAgent->clearHistory();
        m_sessionHistories[newRow] = QJsonArray();
        m_sessionIoHistories[newRow] = QJsonArray();
        if (m_currentAgent)
            m_sessionUuids[newRow] = m_currentAgent->config().uuid;
        m_streamStates.remove(newRow);
        QAbstractItemModel *model = m_chatListWidget->listView()->model();
        if (model && model->rowCount() > 0) {
            QModelIndex last = model->index(model->rowCount() - 1, 0);
            if (last.isValid())
                m_chatListWidget->listView()->setCurrentIndex(last);
        }
        updateHistoryDisplay();
        updateSendingStateForCurrentRow();
        saveSessionsToDisk();
    }
}

void AgentChatWidget::handleChatRowRemoved(int row, bool listAlreadyRemoved)
{
    if (!m_chatListWidget || !m_chatWidget)
        return;
    QStandardItemModel *src = m_chatListWidget->listView()->standardModel();
    if (!src)
        return;
    if (row < 0)
        return;

    if (!listAlreadyRemoved) {
        if (row >= src->rowCount())
            return;
        if (!m_chatListWidget->removeChatItem(row))
            return;
    }

    if (isRowStreaming(row)) {
        if (LLMAgent* streamingAgent = agentForRow(row)) {
            streamingAgent->abort();
        }
    }

    m_sessionHistories.remove(row);
    m_sessionIoHistories.remove(row);
    m_sessionUuids.remove(row);
    m_streamStates.remove(row);
    QHash<int, QJsonArray> reindexed;
    for (auto it = m_sessionHistories.begin(); it != m_sessionHistories.end(); ++it) {
        const int oldRow = it.key();
        const int newRow = oldRow > row ? (oldRow - 1) : oldRow;
        reindexed.insert(newRow, it.value());
    }
    m_sessionHistories = reindexed;
    QHash<int, QJsonArray> reindexedIo;
    for (auto it = m_sessionIoHistories.begin(); it != m_sessionIoHistories.end(); ++it) {
        const int oldRow = it.key();
        const int newRow = oldRow > row ? (oldRow - 1) : oldRow;
        reindexedIo.insert(newRow, it.value());
    }
    m_sessionIoHistories = reindexedIo;
    QHash<int, QString> reindexedUuids;
    for (auto it = m_sessionUuids.begin(); it != m_sessionUuids.end(); ++it) {
        const int oldRow = it.key();
        const int newRow = oldRow > row ? (oldRow - 1) : oldRow;
        reindexedUuids.insert(newRow, it.value());
    }
    m_sessionUuids = reindexedUuids;
    QHash<int, StreamState> reindexedStreams;
    for (auto it = m_streamStates.begin(); it != m_streamStates.end(); ++it) {
        const int oldRow = it.key();
        const int newRow = oldRow > row ? (oldRow - 1) : oldRow;
        reindexedStreams.insert(newRow, it.value());
    }
    m_streamStates = reindexedStreams;
    removeAgentForRow(row);
    reindexAgentsAfterRemoval(row);

    m_currentAgent = nullptr;
    m_currentSessionRow = -1;
    m_chatListWidget->listView()->clearSelection();
    m_chatListWidget->listView()->setCurrentIndex(QModelIndex());
    clearChatMessages();
    m_chatWidget->setEmptyStateVisible(true);
    updateHistoryDisplay();
    updateSendingStateForCurrentRow();
    saveSessionsToDisk();
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
    m_sessionHistories[m_currentSessionRow] = toSave;
    m_sessionIoHistories[m_currentSessionRow] = ioHistoryForRow(m_currentSessionRow);

    setCurrentAgentForRow(row);
    QJsonArray h = historyForRow(row);
    if (m_chatWidget)
        m_chatWidget->setEmptyStateVisible(false);
    restoreChatFromHistory(h);
    updateHistoryDisplayFrom(ioHistoryForRow(row)); // 右侧历史面板始终显示当前看的会话
    StreamState &state = streamStateForRow(row);
    if (state.isStreaming) {
        state.hasPendingMessage = !state.buffer.isEmpty();
    } else {
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
    }
    updateSendingStateForCurrentRow();
    saveSessionsToDisk();
}

void AgentChatWidget::onChatItemRemoved(int row)
{
    handleChatRowRemoved(row, true);
}

void AgentChatWidget::onChatItemRenamed(int row, const QString &name)
{
    LLMAgent* agent = m_sessionAgents.value(row, nullptr);
    if (agent) {
        LLMConfig cfg = agent->config();
        cfg.userName = name.trimmed();
        agent->setConfig(cfg);
    }
    saveSessionsToDisk();
}

void AgentChatWidget::onRemoveCurrentChatRequested()
{
    handleChatRowRemoved(m_currentSessionRow, false);
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
    const QString assistantName = agentDisplayNameForRow(m_currentSessionRow);
    for (const QJsonValue& v : history) {
        QJsonObject o = v.toObject();
        QString role = o["role"].toString();
        QString content = o["content"].toString();
        if (content.isEmpty())
            continue;
        if (role == QLatin1String("tool"))
            continue; // 对话框不展示工具消息
        bool isMine = (role == QLatin1String("user"));
        m_chatWidget->addMessage(makeMessageParams(content, isMine, isMine ? QStringLiteral("Me") : assistantName));
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
    if (!src)
        return;
    QJsonObject root;
    root.insert(QStringLiteral("currentRow"), m_currentSessionRow);
    QJsonArray arr;
    for (int r = 0; r < src->rowCount(); ++r) {
        QModelIndex idx = src->index(r, 0);
        QJsonObject s;
        const QString uuid = ensureSessionUuid(r);
        if (!uuid.isEmpty())
            s.insert(QStringLiteral("uuid"), uuid);
        s.insert(QStringLiteral("name"), idx.data(ChatListNameRole).toString());
        s.insert(QStringLiteral("message"), idx.data(ChatListMessageRole).toString());
        s.insert(QStringLiteral("time"), idx.data(ChatListTimeRole).toString());
        QJsonArray hist = historyForRow(r);
        s.insert(QStringLiteral("history"), hist);
        QJsonArray ioHist = ioHistoryForRow(r);
        s.insert(QStringLiteral("io_history"), ioHist);
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
    if (arr.isEmpty()) {
        m_chatListWidget->clearChats();
        m_sessionHistories.clear();
        m_sessionIoHistories.clear();
        m_sessionUuids.clear();
        for (LLMAgent* agent : m_sessionAgents) {
            if (agent)
                agent->deleteLater();
        }
        m_sessionAgents.clear();
        m_streamStates.clear();
        m_currentAgent = nullptr;
        m_currentSessionRow = -1;
        m_chatWidget->clearMessages();
        m_chatWidget->setEmptyStateVisible(true, tr("暂无会话，请新建对话。"));
        updateHistoryDisplay();
        return true;
    }
    int currentRow = root[QStringLiteral("currentRow")].toInt(0);
    m_chatListWidget->clearChats();
    m_sessionHistories.clear();
    m_sessionIoHistories.clear();
    m_sessionUuids.clear();
    for (LLMAgent* agent : m_sessionAgents) {
        if (agent)
            agent->deleteLater();
    }
    m_sessionAgents.clear();
    m_currentAgent = nullptr;
    for (const QJsonValue& v : arr) {
        QJsonObject s = v.toObject();
        QString uuid = s[QStringLiteral("uuid")].toString().trimmed();
        if (uuid.isEmpty())
            uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString name = s[QStringLiteral("name")].toString();
        if (name.isEmpty())
            name = tr("新对话");
        m_chatListWidget->addChatItem(name,
                                      s[QStringLiteral("message")].toString(),
                                      s[QStringLiteral("time")].toString(),
                                      QColor(Qt::gray), 0);
        const int row = m_sessionHistories.size();
        m_sessionUuids[row] = uuid;
        QJsonArray history = s[QStringLiteral("history")].toArray();
        m_sessionHistories[row] = history;
        QJsonArray ioHistory = s[QStringLiteral("io_history")].toArray();
        m_sessionIoHistories[row] = ioHistory;
        if (LLMAgent* agent = ensureAgentForRow(row)) {
            agent->setHistory(history);
            agent->setIoHistory(ioHistory);
        }
    }
    int n = m_sessionHistories.size();
    setCurrentAgentForRow(qBound(0, currentRow, n - 1));
    QJsonArray h = historyForRow(m_currentSessionRow);
    m_chatWidget->setEmptyStateVisible(false);
    restoreChatFromHistory(h);
    m_streamStates.clear();
    updateSendingStateForCurrentRow();
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
    QString yamlPath = modelConfigPath();
    QString defaultModelId = ModelConfigLoader::getDefaultModelId(yamlPath);
    
    QVariantMap initial;
    if (!defaultModelId.isEmpty()) {
        ModelConfig existingConfig = ModelConfigLoader::getModelConfig(yamlPath, defaultModelId, false);
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
        modelConfig.baseUrl = config.value("baseUrl").toString().trimmed();
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
                    QMessageBox::warning(this, tr("读取失败"),
                        tr("无法从系统密钥库读取：%1").arg(error.isEmpty() ? tr("未知错误") : error));
                    return;
                }
            } else if (isEnvVarReference(apiKeyInput)) {
                apiKeyStored = apiKeyInput;
                QString varName;
                if (extractEnvVarName(apiKeyInput, &varName)) {
                    apiKeyRuntime = QProcessEnvironment::systemEnvironment().value(varName);
                }
                if (apiKeyRuntime.isEmpty()) {
                    QMessageBox::warning(this, tr("环境变量未设置"),
                        tr("未读取到 %1，请先设置环境变量后再导入。").arg(apiKeyInput));
                    return;
                }
            } else {
                keychainId = KeychainHelper::entryIdForModel(modelConfig.provider, modelConfig.modelId);
                QString error;
                if (!KeychainHelper::writePasswordSync(keychainId, apiKeyInput, &error)) {
                    QMessageBox::warning(this, tr("保存失败"),
                        tr("无法写入系统密钥库：%1").arg(error.isEmpty() ? tr("未知错误") : error));
                    return;
                }
                apiKeyStored = KeychainHelper::makeKeyRef(keychainId);
                apiKeyRuntime = apiKeyInput;
            }
        }
        modelConfig.apiKey = apiKeyRuntime;
        modelConfig.authType = "Bearer";
        modelConfig.temperature = 0.7;
        modelConfig.maxTokens = 4096;
        modelConfig.timeoutMs = 180000;
        modelConfig.capabilities << Capability::TextGeneration << Capability::ToolCalling;
        modelConfig.toolCalling = true;
        
        // 默认系统提示词
        modelConfig.systemPrompt = tr("你是一个专业的 Qt 高级开发工程师，精通 C++、Qt 框架和跨平台开发。");
        
        // 保存到 YAML
        ModelConfig saveConfig = modelConfig;
        saveConfig.apiKey = apiKeyStored;
        ModelConfigLoader::addOrUpdateModel(yamlPath, saveConfig);
        ModelConfigLoader::setDefaultModelId(yamlPath, modelConfig.modelId);
        
        // 注册到 Factory
        m_modelFactory->registerModelConfig(modelConfig);
        
        // 切换 Agent 配置
        LLMConfig agentConfig;
        {
            ModelFactory::ParsedModelId parsed = ModelFactory::parseModelKey(modelConfig.modelId);
            agentConfig.model = parsed.model;
            agentConfig.customModelId = parsed.customModelId;
        }
        agentConfig.systemPrompt = modelConfig.systemPrompt;
        agentConfig.userName = tr("TM Agent");
        m_defaultAgentConfig = agentConfig;
        applyConfigToAllAgents();
        
        dlg->accept();
        QMessageBox::information(this, tr("已导入"), 
            tr("已从「%1」导入配置并保存到 %2")
                .arg(config.value("providerName").toString(),
                     QDir::toNativeSeparators(yamlPath)));
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

    StreamState &state = streamStateForRow(m_currentSessionRow);
    state.buffer.clear();
    state.hasPendingMessage = false;
    state.lastMsgIsTool = false;
    state.isStreaming = true;
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
    updateHistoryDisplay();
}

void AgentChatWidget::onAbortClicked()
{
    qDebug() << "------------------------------------------";
    qDebug() << "AgentChatWidget: [Signal Received] Stop requested by User UI";

    // 中断并回滚，获取被回滚的用户消息
    const int row = m_currentSessionRow;
    LLMAgent* targetAgent = (row >= 0) ? agentForRow(row) : nullptr;
    if (!targetAgent)
        targetAgent = m_currentAgent;
    const bool wasStreaming = isRowStreaming(row);
    QString rolledBackUserMsg = targetAgent ? targetAgent->abortAndRollback() : QString();

    if (m_chatWidget && row == m_currentSessionRow && wasStreaming) {
        m_chatWidget->addMessage(makeMessageParams("[已手动中断]", false, "System"));

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
    if (row >= 0) {
        StreamState &state = streamStateForRow(row);
        state.isStreaming = false;
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
    }
    if (row == m_currentSessionRow)
        updateHistoryDisplay();
    updateSendingStateForCurrentRow();
}

void AgentChatWidget::onVoiceStartRequested()
{
    if (m_chatWidget) {
        m_chatWidget->addMessage(makeMessageParams("[语音输入功能暂未接入]", false, "System"));
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
        return;
    StreamState &state = streamStateForRow(streamRow);
    state.isStreaming = true;
    state.buffer += data;
    // 仅当正在回复的会话就是当前显示的会话时，才更新聊天区域
    if (streamRow != m_currentSessionRow) {
        return;
    }
    m_chatWidget->setSendingState(true);
    if (!state.hasPendingMessage) {
        m_chatWidget->addMessage(makeMessageParams("", false, agentDisplayNameForRow(streamRow)));
        state.hasPendingMessage = true;
        state.lastMsgIsTool = false;
    }
    m_chatWidget->streamOutput(data);
}

void AgentChatWidget::onToolCallsStarted()
{
    if (!m_chatWidget)
        return;
    LLMAgent* agent = qobject_cast<LLMAgent*>(sender());
    int streamRow = agent ? m_sessionAgents.key(agent, -1) : -1;
    if (streamRow < 0)
        return;
    StreamState &state = streamStateForRow(streamRow);
    if (state.hasPendingMessage) {
        state.hasPendingMessage = false;
        state.buffer.clear();
    }
    state.lastMsgIsTool = false;
    if (streamRow == m_currentSessionRow) {
        if (agent)
            m_sessionIoHistories[streamRow] = agent->getIoHistory();
        updateHistoryDisplay();
    }
}

void AgentChatWidget::onFinished(const QString& fullContent)
{
    LLMAgent* agent = qobject_cast<LLMAgent*>(sender());
    const int forRow = agent ? m_sessionAgents.key(agent, -1) : -1;
    bool hadPending = false;
    if (forRow >= 0) {
        StreamState &state = streamStateForRow(forRow);
        hadPending = state.hasPendingMessage;
        state.isStreaming = false;
        state.hasPendingMessage = false;
        state.buffer.clear();
        state.lastMsgIsTool = false;
    }
    updateSendingStateForCurrentRow();

    if (forRow < 0)
        return;
    // 把本次回复记到所属会话里（Agent 里已是该会话历史 + 本条，可直接取）
    if (agent) {
        m_sessionHistories[forRow] = agent->getHistory();
        m_sessionIoHistories[forRow] = agent->getIoHistory();
    } else {
        m_sessionHistories[forRow] = historyForRow(forRow);
        m_sessionIoHistories[forRow] = ioHistoryForRow(forRow);
    }
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
        if (hadPending)
            m_chatWidget->removeLastMessage();
        if (!fullContent.isEmpty())
            m_chatWidget->addMessage(makeMessageParams(fullContent, false, agentDisplayNameForRow(forRow)));
        updateHistoryDisplay();
    }
}

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
    updateHistoryDisplayFrom(ioHistoryForRow(m_currentSessionRow));
}

void AgentChatWidget::onClearHistoryClicked()
{
    if (m_currentAgent)
        m_currentAgent->clearHistory();
    if (m_currentSessionRow >= 0)
        m_sessionIoHistories[m_currentSessionRow] = QJsonArray();
    if (m_currentSessionRow >= 0)
        m_sessionHistories[m_currentSessionRow] = QJsonArray();
    m_historyDisplay->clear();
    m_historyLabel->setText("请求/响应历史 (共 0 次)");
    if (m_chatWidget) {
        m_chatWidget->addMessage(makeMessageParams("[对话历史已清空]", false, "System"));
    }
}

void AgentChatWidget::onErrorOccurred(const QString& errorMsg)
{
    LLMAgent* agent = qobject_cast<LLMAgent*>(sender());
    const int forRow = agent ? m_sessionAgents.key(agent, -1) : -1;
    if (forRow >= 0) {
        StreamState &state = streamStateForRow(forRow);
        state.isStreaming = false;
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
        if (agent)
            m_sessionIoHistories[forRow] = agent->getIoHistory();
    }
    updateSendingStateForCurrentRow();

    if (m_chatWidget && forRow == m_currentSessionRow) {
        m_chatWidget->addMessage(makeMessageParams(QString("❌ 错误: %1").arg(errorMsg), false, "System"));
        updateHistoryDisplay();
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
    if (row < 0)
        return;
    if (row >= 0 && row != m_currentSessionRow)
        return;
    StreamState &state = streamStateForRow(row);

    // 对话框不展示工具调用相关消息
    state.lastMsgIsTool = false;
    return;

    if (m_isDebugMode) {
        if (event.status == "started") {
            m_chatWidget->addMessage(makeMessageParams(QString("⚡ 正在执行工具: %1").arg(event.toolName), false, "Tool"));
            state.lastMsgIsTool = true;
        } else if (event.status == "progress") {
            if (state.lastMsgIsTool)
                m_chatWidget->removeLastMessage();
            m_chatWidget->addMessage(makeMessageParams(QString("⏳ %1: %2").arg(event.toolName, event.formattedResult), false, "Tool"));
            state.lastMsgIsTool = true;
        } else if (event.status == "completed") {
            if (state.lastMsgIsTool)
                m_chatWidget->removeLastMessage();
            QString icon = event.success ? "✅" : "❌";
            m_chatWidget->addMessage(makeMessageParams(
                QString("%1 %2 完成: %3").arg(icon, event.toolName, event.formattedResult),
                false,
                "Tool"));
            state.lastMsgIsTool = true; // Completed is still a tool message, but maybe final one
        }
        return;
    }

    if (event.status == "progress") {
        if (state.lastMsgIsTool)
            m_chatWidget->removeLastMessage();
        m_chatWidget->addMessage(makeMessageParams(QString("⏳ %1: %2").arg(event.toolName, event.formattedResult), false, "Tool"));
        state.lastMsgIsTool = true;
        return;
    }

    if (event.status == "completed") {
        if (event.success) {
            // 成功时，如果上一条是进度信息，则移除它（保持界面清爽）
            if (state.lastMsgIsTool) {
                m_chatWidget->removeLastMessage();
                state.lastMsgIsTool = false;
            }
        } else {
            // 失败时，保留错误提示
            if (state.lastMsgIsTool)
                m_chatWidget->removeLastMessage();
            m_chatWidget->addMessage(makeMessageParams(QString("❌ %1 执行失败").arg(event.toolName), false, "Tool"));
            state.lastMsgIsTool = true;
        }
    } else if (event.status == "started") {
        state.lastMsgIsTool = false; // 重置，为后续 progress 做准备
    }
}
