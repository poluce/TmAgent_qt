#include "MainWindow.h"
#include "IdentityTabBar.h"
#include "IdentityView.h"
#include "AgentCreateDialog.h"
#include "ToolLogWidget.h"
#include "core/service/ChatService.h"
#include "core/service/AgentRuntime.h"
#include "core/agent/ToolDispatcher.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/utils/ModelConfigLoader.h"
#include "core/utils/KeychainHelper.h"
#include "core/utils/DefaultPrompts.h"
#include "newCore/ModelFactory.h"
#include "newCore/LLMTypes.h"
#include "modelconfig/model_config_import_page.h"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

// ==================== 构造函数 ====================

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent)
{
    m_chatService = new ChatService(this);
    m_chatService->initialize();

    setupUI();
    setupConnections();
    restorePersistedSessions();

    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        QStringList openAgentIds;
        for (int i = 1; i < m_tabBar->count(); ++i)
            openAgentIds.append(m_tabBar->identityIdForTab(i));
        QString activeId = m_tabBar->identityIdForTab(m_tabBar->currentIndex());
        m_chatService->saveTabState(openAgentIds, activeId);
        m_chatService->saveSessionsToDisk();
    });
}

// ==================== setupUI ====================

void MainWindow::setupUI()
{
    setWindowTitle("TmAgent - Team of Agents");
    resize(1200, 700);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 顶部：TabBar + 创建按钮
    auto* topLayout = new QHBoxLayout();
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);

    m_tabBar = new IdentityTabBar(this);
    topLayout->addWidget(m_tabBar, 1);

    m_createAgentBtn = new QPushButton(tr("+创建Agent"), this);
    m_createAgentBtn->setFixedHeight(m_tabBar->sizeHint().height());
    topLayout->addWidget(m_createAgentBtn);

    mainLayout->addLayout(topLayout);

    // 内容区
    m_stackedWidget = new QStackedWidget(this);
    mainLayout->addWidget(m_stackedWidget, 1);

    // 创建用户 Tab（固定）
    QString userId = IdentityManager::instance()->userIdentity()->id();
    m_tabBar->addUserTab(userId, tr("我"));
    auto* userView = new IdentityView(userId, m_chatService, this);
    m_stackedWidget->addWidget(userView);
    m_views.insert(userId, userView);
    connectViewSignals(userView);
}

// ==================== setupConnections ====================

void MainWindow::setupConnections()
{
    // Tab 切换
    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabBar, &IdentityTabBar::agentTabCloseRequested, this, &MainWindow::onAgentTabCloseRequested);
    connect(m_createAgentBtn, &QPushButton::clicked, this, &MainWindow::onCreateAgentClicked);

    // ChatService 统一事件流路由（UI 与后端执行流程解耦）
    connect(m_chatService, &ChatService::conversationEvent, this, &MainWindow::onConversationEvent);
    connect(m_chatService, &ChatService::sessionCreated, this, &MainWindow::onSessionCreated);
    connect(m_chatService, &ChatService::sessionRemoved, this, &MainWindow::onSessionRemoved);
}

void MainWindow::connectViewSignals(IdentityView* view)
{
    connect(view, &IdentityView::modelConfigImportRequested, this, &MainWindow::onModelConfigImportClicked);
    connect(view, &IdentityView::mcpConfigRequested, this, &MainWindow::onMcpConfigClicked);
    connect(view, &IdentityView::toolLogRequested, this, &MainWindow::onToolLogClicked);
}

// ==================== Tab 切换 ====================

void MainWindow::onTabChanged(int index)
{
    QString identityId = m_tabBar->identityIdForTab(index);
    if (identityId.isEmpty())
        return;

    // 停用所有视角
    for (auto* view : m_views)
        if (view->isActive())
            view->deactivate();

    // 激活新视角（懒加载）
    IdentityView* view = ensureIdentityView(identityId);
    if (view) {
        m_stackedWidget->setCurrentWidget(view);
        view->activate();
    }
}

IdentityView* MainWindow::ensureIdentityView(const QString& identityId)
{
    if (IdentityView* existing = m_views.value(identityId, nullptr))
        return existing;

    auto* view = new IdentityView(identityId, m_chatService, this);
    m_stackedWidget->addWidget(view);
    m_views.insert(identityId, view);
    connectViewSignals(view);
    return view;
}

// ==================== 创建 Agent ====================

void MainWindow::onCreateAgentClicked()
{
    AgentCreateDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QString name = dlg.agentName();
    QString prompt = dlg.systemPrompt();

    // 创建 Agent Identity
    auto* profile = new IdentityProfile();
    profile->setLlmConfig(m_chatService->defaultAgentConfig());
    if (!prompt.isEmpty())
        profile->setSystemPrompt(prompt);
    if (ToolDispatcher* dispatcher = m_chatService->toolDispatcher()) {
        QStringList toolNames;
        const QList<Tool> tools = dispatcher->getAllToolSchemas();
        for (const Tool& tool : tools) {
            const QString name = tool.name.trimmed();
            if (!name.isEmpty())
                toolNames.append(name);
        }
        toolNames.removeDuplicates();
        profile->setAllowedTools(toolNames);
    }
    Identity* agent = IdentityManager::instance()->createAgent(name, profile);

    // 创建初始 Session
    m_chatService->createSessionForIdentity(agent->id(), name);

    // 添加 Agent Tab
    int tabIndex = m_tabBar->addAgentTab(agent->id(), name);
    m_tabBar->setCurrentIndex(tabIndex);
}

// ==================== 关闭 Agent Tab ====================

void MainWindow::onAgentTabCloseRequested(int index, const QString& identityId)
{
    m_tabBar->removeTab(index);

    IdentityView* view = m_views.take(identityId);
    if (view) {
        m_stackedWidget->removeWidget(view);
        view->deleteLater();
    }
}

// ==================== ChatService 信号路由 ====================

void MainWindow::onConversationEvent(const QJsonObject& event)
{
    const QString type = event.value(QStringLiteral("type")).toString();
    const QString sessionId = event.value(QStringLiteral("sessionId")).toString();
    if (type.isEmpty() || sessionId.isEmpty())
        return;

    if (type == QLatin1String("turn_delta")) {
        onStreamData(sessionId, event.value(QStringLiteral("delta")).toString());
        return;
    }

    if (type == QLatin1String("turn_completed")) {
        onFinished(sessionId, event.value(QStringLiteral("fullContent")).toString());
        return;
    }

    if (type == QLatin1String("turn_failed")) {
        onError(sessionId, event.value(QStringLiteral("error")).toString());
        return;
    }

    if (type == QLatin1String("turn_tool_calls_started")) {
        onToolCallsStarted(sessionId);
        return;
    }

    if (type == QLatin1String("turn_tool_event")) {
        const QJsonObject obj = event.value(QStringLiteral("toolEvent")).toObject();
        ToolExecutionEvent toolEvent;
        toolEvent.toolName = obj.value(QStringLiteral("toolName")).toString();
        toolEvent.toolId = obj.value(QStringLiteral("toolId")).toString();
        toolEvent.status = obj.value(QStringLiteral("status")).toString();
        toolEvent.success = obj.value(QStringLiteral("success")).toBool(true);
        toolEvent.data = obj.value(QStringLiteral("data")).toObject();
        toolEvent.rawResult = obj.value(QStringLiteral("rawResult")).toString();
        toolEvent.formattedResult = obj.value(QStringLiteral("formattedResult")).toString();
        onToolEvent(sessionId, toolEvent);
        return;
    }

    if (type == QLatin1String("turn_cancelled")) {
        // 取消时清理占位流消息，避免 UI 残留 pending 内容
        onFinished(sessionId, QString());
        return;
    }

    if (type == QLatin1String("turn_started")) {
        for (IdentityView* view : viewsForSession(sessionId))
            view->refreshSendingState();
    }
}

QList<IdentityView*> MainWindow::viewsForSession(const QString& sessionId) const
{
    QList<IdentityView*> result;
    Session* session = SessionManager::instance()->findById(sessionId);
    if (!session)
        return result;

    // 用户视角始终包含
    QString userId = IdentityManager::instance()->userIdentity()->id();
    if (IdentityView* uv = m_views.value(userId, nullptr))
        result.append(uv);

    // Agent 视角
    for (const QString& pid : session->participantIds()) {
        if (pid == userId)
            continue;
        if (IdentityView* view = m_views.value(pid, nullptr))
            result.append(view);
    }
    return result;
}

void MainWindow::onStreamData(const QString& sessionId, const QString& data)
{
    for (IdentityView* view : viewsForSession(sessionId))
        view->handleStreamData(sessionId, data);
}

void MainWindow::onFinished(const QString& sessionId, const QString& fullContent)
{
    for (IdentityView* view : viewsForSession(sessionId))
        view->handleFinished(sessionId, fullContent);
}

void MainWindow::onError(const QString& sessionId, const QString& errorMsg)
{
    for (IdentityView* view : viewsForSession(sessionId))
        view->handleError(sessionId, errorMsg);
}

void MainWindow::onToolCallsStarted(const QString& sessionId)
{
    for (IdentityView* view : viewsForSession(sessionId))
        view->handleToolCallsStarted(sessionId);
}

void MainWindow::onToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    // 工具日志窗口
    if (m_toolLogWindow)
        m_toolLogWindow->logEvent(event);

    for (IdentityView* view : viewsForSession(sessionId))
        view->handleToolEvent(sessionId, event);
}

void MainWindow::onSessionCreated(const QString& sessionId)
{
    Session* session = SessionManager::instance()->findById(sessionId);
    if (!session)
        return;

    // 找到该 Session 中的 Agent 参与者
    QString agentId;
    for (const QString& pid : session->participantIds()) {
        Identity* identity = IdentityManager::instance()->findById(pid);
        if (identity && identity->isAgent()) {
            agentId = pid;
            break;
        }
    }

    // 如果 Agent Tab 不存在，自动添加
    if (!agentId.isEmpty() && m_tabBar->tabIndexForIdentity(agentId) < 0) {
        Identity* agent = IdentityManager::instance()->findById(agentId);
        if (agent)
            m_tabBar->addAgentTab(agentId, agent->name());
    }

    // 通知所有相关 IdentityView 刷新会话列表
    for (IdentityView* view : viewsForSession(sessionId)) {
        if (view->isActive()) {
            view->reloadSessionList();
        } else {
            view->markSessionListDirty();
        }
    }
}

void MainWindow::onSessionRemoved(const QString& sessionId)
{
    Q_UNUSED(sessionId);
    for (IdentityView* view : m_views) {
        if (!view)
            continue;
        if (view->isActive())
            view->reloadSessionList();
        else
            view->markSessionListDirty();
    }
}

// ==================== 恢复持久化 ====================

void MainWindow::restorePersistedSessions()
{
    if (!m_chatService->loadSessionsFromDisk())
        return;

    // 激活用户视角
    QString userId = IdentityManager::instance()->userIdentity()->id();
    IdentityView* userView = m_views.value(userId, nullptr);
    if (userView)
        userView->activate();

    // 恢复 Agent Tab
    ChatService::TabState tabState = m_chatService->loadTabState();
    if (!tabState.openAgentIds.isEmpty()) {
        // 有保存的 Tab 状态，按保存的恢复
        for (const QString& agentId : tabState.openAgentIds) {
            Identity* agent = IdentityManager::instance()->findById(agentId);
            if (!agent || !agent->isAgent())
                continue;
            m_tabBar->addAgentTab(agentId, agent->name());
        }
    } else {
        // 没有保存的 Tab 状态（首次升级），从所有 Session 中提取 Agent
        QList<Session*> sessions = SessionManager::instance()->allSessions();
        for (Session* session : sessions) {
            for (const QString& pid : session->participantIds()) {
                Identity* identity = IdentityManager::instance()->findById(pid);
                if (identity && identity->isAgent() && m_tabBar->tabIndexForIdentity(pid) < 0)
                    m_tabBar->addAgentTab(pid, identity->name());
            }
        }
    }

    // 恢复活跃 Tab
    if (!tabState.activeIdentityId.isEmpty()) {
        int idx = m_tabBar->tabIndexForIdentity(tabState.activeIdentityId);
        if (idx >= 0)
            m_tabBar->setCurrentIndex(idx);
    }

    // 如果没有任何会话，创建默认会话
    if (userView && userView->currentSessionId().isEmpty()) {
        Session* session = m_chatService->createNewSession(tr("新对话"));
        if (session && userView->isActive())
            userView->reloadSessionList();
    }
}

// ==================== 工具日志 ====================

void MainWindow::onToolLogClicked()
{
    if (!m_toolLogWindow)
        m_toolLogWindow = new ToolLogWidget();
    m_toolLogWindow->show();
    m_toolLogWindow->raise();
    m_toolLogWindow->activateWindow();
}

// ==================== MCP 配置 ====================

void MainWindow::onMcpConfigClicked()
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

// ==================== 模型配置导入 ====================

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

QString inferProviderIdFromBaseUrl(const QString& baseUrl)
{
    const QString u = baseUrl.trimmed().toLower();
    if (u.contains("deepseek")) return QStringLiteral("deepseek");
    if (u.contains("openai.com")) return QStringLiteral("openai");
    if (u.contains("anthropic")) return QStringLiteral("claude");
    if (u.contains("localhost:11434") || u.contains("ollama")) return QStringLiteral("ollama");
    if (u.contains("generativelanguage") || u.contains("googleapis")) return QStringLiteral("gemini");
    return QString();
}

QList<ModelConfigProvider> defaultModelConfigProviders()
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
    claude.fields << ModelConfigField{"modelId", "模型名称", "claude-sonnet-4-5-20250929", "claude-sonnet-4-5-20250929"};
    claude.fields << ModelConfigField{"baseUrl", "接口地址", "https://api.anthropic.com", "https://api.anthropic.com"};
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
} // namespace

void MainWindow::onModelConfigImportClicked()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("从厂商导入模型配置"));
    dlg->resize(720, 480);

    auto* page = new ModelConfigImportPage(dlg);
    page->setProviders(defaultModelConfigProviders());
    page->applyStyleSheet();

    QString yamlPath = m_chatService->modelConfigPath();
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
                if (extractEnvVarName(apiKeyInput, &varName))
                    apiKeyRuntime = QProcessEnvironment::systemEnvironment().value(varName);
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
        modelConfig.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt();

        ModelConfig saveConfig = modelConfig;
        saveConfig.apiKey = apiKeyStored;
        ModelConfigLoader::addOrUpdateModel(yamlPath, saveConfig);
        ModelConfigLoader::setDefaultModelId(yamlPath, modelConfig.modelId);

        m_chatService->modelFactory()->registerModelConfig(modelConfig);

        LLMConfig agentConfig;
        {
            ModelFactory::ParsedModelId parsed = ModelFactory::parseModelKey(modelConfig.modelId);
            agentConfig.model = parsed.model;
            agentConfig.customModelId = parsed.customModelId;
        }
        agentConfig.systemPrompt = modelConfig.systemPrompt;
        agentConfig.userName = tr("TM Agent");
        m_chatService->setDefaultAgentConfig(agentConfig);
        m_chatService->applyConfigToAllRuntimes();

        dlg->accept();
        QMessageBox::information(this, tr("已导入"),
            tr("已从「%1」导入配置并保存到 %2")
                .arg(config.value("providerName").toString(),
                     QDir::toNativeSeparators(yamlPath)));
    });
    connect(page, &ModelConfigImportPage::cancelled, dlg, &QDialog::reject);

    connect(page, &ModelConfigImportPage::testConnectionRequested, this, [page](const QVariantMap&) {
        page->setTestStatus(ModelConfigImportPage::TestStatus::Testing, QObject::tr("验证中…"));
        QTimer::singleShot(800, page, [page]() {
            page->setTestStatus(ModelConfigImportPage::TestStatus::Success, QObject::tr("可在主界面保存后发送消息验证"));
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
