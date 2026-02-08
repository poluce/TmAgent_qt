#include "ChatService.h"
#include "AgentRuntime.h"
#include "core/agent/LLMAgent.h"
#include "core/agent/ToolDispatcher.h"
#include "core/agent/McpToolProvider.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/utils/ModelConfigLoader.h"
#include "newCore/ModelFactory.h"
#include "newCore/LLMTypes.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>

ChatService::ChatService(QObject* parent)
    : QObject(parent)
{
}

ChatService::~ChatService()
{
    saveSessionsToDisk();
}

void ChatService::initialize()
{
    m_identityManager = IdentityManager::instance();
    m_sessionManager = SessionManager::instance();
    m_modelFactory = ModelFactory::instance();

    m_toolDispatcher = ToolDispatcher::instance();
    m_toolDispatcher->registerDefaultTools();

    m_mcpProvider = new McpToolProvider(m_toolDispatcher);
    m_toolDispatcher->registerProvider(m_mcpProvider, "mcp");
    applyMcpConfig(loadMcpConfigSpecs());

    // 确保用户 Identity 存在
    m_identityManager->userIdentity();

    loadConfig();
}

void ChatService::sendUserMessage(const QString& sessionId, const QString& text)
{
    AgentRuntime* runtime = ensureRuntimeForSession(sessionId);
    if (!runtime)
        return;

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
        state.isStreaming = true;
    }

    runtime->sendMessage(sessionId, text);
}

void ChatService::abortCurrent(const QString& sessionId)
{
    AgentRuntime* runtime = m_runtimes.value(sessionId, nullptr);
    if (runtime)
        runtime->abort();

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.isStreaming = false;
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
    }
}

QString ChatService::abortAndRollback(const QString& sessionId)
{
    AgentRuntime* runtime = m_runtimes.value(sessionId, nullptr);
    QString rolledBack;
    if (runtime)
        rolledBack = runtime->abortAndRollback();

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.isStreaming = false;
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
    }
    return rolledBack;
}

Session* ChatService::createNewSession(const QString& agentName)
{
    // 保存当前 Session 的历史
    if (!m_currentSessionId.isEmpty()) {
        AgentRuntime* currentRuntime = m_runtimes.value(m_currentSessionId, nullptr);
        Session* currentSession = m_sessionManager->findById(m_currentSessionId);
        if (currentRuntime && currentSession) {
            currentSession->setLlmHistory(currentRuntime->getHistory());
            currentSession->setIoHistory(currentRuntime->getIoHistory());
        }
    }

    QString userId = m_identityManager->userIdentity()->id();

    // 创建 Agent Identity
    auto* profile = new IdentityProfile();
    profile->setLlmConfig(m_defaultAgentConfig);
    profile->setSystemPrompt(m_defaultAgentConfig.systemPrompt);

    QString name = agentName.isEmpty() ? QStringLiteral("TM Agent") : agentName;
    Identity* agentIdentity = m_identityManager->createAgent(name, profile);

    // 创建 Private Session
    Session* session = m_sessionManager->createPrivateSession(userId, agentIdentity->id());
    session->setTitle(name);

    // 创建 AgentRuntime
    AgentRuntime* runtime = ensureRuntimeForSession(session->id());
    if (runtime) {
        runtime->clearHistory();
    }

    m_currentSessionId = session->id();
    emit sessionCreated(session->id());
    saveSessionsToDisk();
    return session;
}

void ChatService::removeSession(const QString& sessionId)
{
    // 中止流式输出
    AgentRuntime* runtime = m_runtimes.take(sessionId);
    if (runtime) {
        if (runtime->isStreaming())
            runtime->abort();
        runtime->deleteLater();
    }

    m_sessionManager->removeSession(sessionId);

    if (m_currentSessionId == sessionId) {
        m_currentSessionId.clear();
    }

    emit sessionRemoved(sessionId);
    saveSessionsToDisk();
}

void ChatService::switchSession(const QString& sessionId)
{
    if (sessionId == m_currentSessionId)
        return;

    // 保存当前 Session 的历史
    if (!m_currentSessionId.isEmpty()) {
        AgentRuntime* currentRuntime = m_runtimes.value(m_currentSessionId, nullptr);
        Session* currentSession = m_sessionManager->findById(m_currentSessionId);
        if (currentRuntime && currentSession) {
            currentSession->setLlmHistory(currentRuntime->getHistory());
            currentSession->setIoHistory(currentRuntime->getIoHistory());
        }
    }

    m_currentSessionId = sessionId;

    // 加载新 Session 的历史到 Runtime
    AgentRuntime* runtime = ensureRuntimeForSession(sessionId);
    if (runtime) {
        Session* session = m_sessionManager->findById(sessionId);
        if (session) {
            runtime->setHistory(session->llmHistory());
            runtime->setIoHistory(session->ioHistory());
        }
    }
}

QString ChatService::currentSessionId() const { return m_currentSessionId; }

AgentRuntime* ChatService::runtimeForSession(const QString& sessionId) const
{
    return m_runtimes.value(sessionId, nullptr);
}

AgentRuntime* ChatService::ensureRuntimeForSession(const QString& sessionId)
{
    if (AgentRuntime* existing = m_runtimes.value(sessionId, nullptr))
        return existing;

    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return nullptr;

    // 找到 Session 中的 Agent 参与者
    Identity* agentIdentity = nullptr;
    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_identityManager->findById(pid);
        if (identity && identity->isAgent()) {
            agentIdentity = identity;
            break;
        }
    }

    if (!agentIdentity) {
        // 如果没有 Agent 参与者，创建一个默认的
        auto* profile = new IdentityProfile();
        profile->setLlmConfig(m_defaultAgentConfig);
        profile->setSystemPrompt(m_defaultAgentConfig.systemPrompt);
        agentIdentity = m_identityManager->createAgent(
            session->title().isEmpty() ? QStringLiteral("TM Agent") : session->title(),
            profile);
        session->addParticipant(agentIdentity->id());
    }

    auto* runtime = new AgentRuntime(agentIdentity, this);
    runtime->setModelFactory(m_modelFactory);
    runtime->setToolDispatcher(m_toolDispatcher);

    // 应用配置
    LLMConfig cfg = m_defaultAgentConfig;
    cfg.userName = agentIdentity->name();
    cfg.uuid = agentIdentity->id();
    if (agentIdentity->profile()) {
        LLMConfig profileCfg = agentIdentity->profile()->llmConfig();
        if (profileCfg.isValid()) {
            cfg.model = profileCfg.model;
            cfg.customModelId = profileCfg.customModelId;
        }
        if (!agentIdentity->profile()->systemPrompt().isEmpty())
            cfg.systemPrompt = agentIdentity->profile()->systemPrompt();
    }
    runtime->setConfig(cfg);

    // 加载 Session 的历史
    runtime->setHistory(session->llmHistory());
    runtime->setIoHistory(session->ioHistory());

    connectRuntimeSignals(runtime);
    m_runtimes.insert(sessionId, runtime);
    return runtime;
}

void ChatService::setDefaultAgentConfig(const LLMConfig& config)
{
    m_defaultAgentConfig = config;
}

LLMConfig ChatService::defaultAgentConfig() const { return m_defaultAgentConfig; }

void ChatService::applyConfigToAllRuntimes()
{
    for (auto it = m_runtimes.begin(); it != m_runtimes.end(); ++it) {
        AgentRuntime* runtime = it.value();
        if (!runtime)
            continue;
        LLMConfig cfg = m_defaultAgentConfig;
        if (runtime->identity()) {
            cfg.userName = runtime->identity()->name();
            cfg.uuid = runtime->identity()->id();
        }
        runtime->setConfig(cfg);
    }
}

void ChatService::applyToolDispatcherToAllRuntimes()
{
    if (!m_toolDispatcher)
        return;
    for (AgentRuntime* runtime : m_runtimes) {
        if (runtime)
            runtime->setToolDispatcher(m_toolDispatcher);
    }
}

bool ChatService::isSessionStreaming(const QString& sessionId) const
{
    Session* session = m_sessionManager->findById(sessionId);
    return session && session->isStreaming();
}

QString ChatService::agentDisplayNameForSession(const QString& sessionId) const
{
    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return QStringLiteral("TM Agent");

    // 找到 Agent 参与者的名称
    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_identityManager->findById(pid);
        if (identity && identity->isAgent())
            return identity->name();
    }

    if (!session->title().isEmpty())
        return session->title();

    return QStringLiteral("TM Agent");
}

ModelFactory* ChatService::modelFactory() const { return m_modelFactory; }
ToolDispatcher* ChatService::toolDispatcher() const { return m_toolDispatcher; }
McpToolProvider* ChatService::mcpProvider() const { return m_mcpProvider; }

void ChatService::applyMcpConfig(const QStringList& specs)
{
    if (!m_mcpProvider)
        return;

    m_mcpProvider->clearServers();
    for (const QString& spec : specs) {
        if (!m_mcpProvider->addServerFromSpec(spec)) {
            qWarning() << "MCP server spec 无效:" << spec;
        }
    }

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

QStringList ChatService::loadMcpConfigSpecs() const
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

bool ChatService::saveMcpConfigSpecs(const QStringList& specs) const
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

QString ChatService::mcpConfigPath() const
{
    QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
    return dir + QStringLiteral("/mcp_servers.json");
}

QString ChatService::modelConfigPath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/resources/models.yaml");
    }
    return QDir(dir).filePath(QStringLiteral("models.yaml"));
}

void ChatService::loadConfig()
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

    QVector<ModelConfig> models = ModelConfigLoader::loadFromFile(yamlPath, true);
    if (models.isEmpty()) {
        emit configLoaded();
        return;
    }

    for (const ModelConfig& config : models) {
        m_modelFactory->registerModelConfig(config);
    }

    QString defaultModelId = ModelConfigLoader::getDefaultModelId(yamlPath);
    if (defaultModelId.isEmpty()) {
        defaultModelId = models.first().modelId;
    }

    ModelConfig defaultConfig = ModelConfigLoader::getModelConfig(yamlPath, defaultModelId, true);

    LLMConfig agentConfig;
    {
        ModelFactory::ParsedModelId parsed = ModelFactory::parseModelKey(defaultModelId);
        agentConfig.model = parsed.model;
        agentConfig.customModelId = parsed.customModelId;
    }
    agentConfig.systemPrompt = defaultConfig.systemPrompt;
    agentConfig.userName = QStringLiteral("TM Agent");
    m_defaultAgentConfig = agentConfig;
    applyConfigToAllRuntimes();

    qInfo() << "已加载" << models.size() << "个模型，默认:" << defaultModelId;
    emit configLoaded();
}

QString ChatService::sessionsFilePath() const
{
    QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
    return dir + QStringLiteral("/chat_sessions.json");
}

void ChatService::saveSessionsToDisk()
{
    QJsonObject root;
    root.insert(QStringLiteral("currentSessionId"), m_currentSessionId);

    QJsonArray arr;
    QList<Session*> sessions = m_sessionManager->allSessions();
    for (Session* session : sessions) {
        QJsonObject s;
        s.insert(QStringLiteral("uuid"), session->id());
        s.insert(QStringLiteral("title"), session->title());

        // 获取最新历史（优先从 Runtime 获取）
        AgentRuntime* runtime = m_runtimes.value(session->id(), nullptr);
        QJsonArray history;
        QJsonArray ioHistory;
        if (runtime) {
            history = runtime->getHistory();
            ioHistory = runtime->getIoHistory();
        } else {
            history = session->llmHistory();
            ioHistory = session->ioHistory();
        }

        // 如果正在流式输出，追加未完成的 buffer
        if (session->isStreaming() && !session->streamState().buffer.isEmpty()) {
            QJsonObject part;
            part.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            part.insert(QStringLiteral("content"), session->streamState().buffer);
            history.append(part);
        }

        s.insert(QStringLiteral("history"), history);
        s.insert(QStringLiteral("io_history"), ioHistory);

        // 保存参与者信息
        QJsonArray participants;
        for (const QString& pid : session->participantIds())
            participants.append(pid);
        s.insert(QStringLiteral("participants"), participants);
        s.insert(QStringLiteral("ownerId"), session->ownerId());

        // 保存 Agent 名称（用于恢复时显示）
        QString agentName;
        for (const QString& pid : session->participantIds()) {
            Identity* identity = m_identityManager->findById(pid);
            if (identity && identity->isAgent()) {
                agentName = identity->name();
                break;
            }
        }
        s.insert(QStringLiteral("agentName"), agentName);

        arr.append(s);
    }
    root.insert(QStringLiteral("sessions"), arr);

    QString path = sessionsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QFile::WriteOnly | QFile::Text))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool ChatService::loadSessionsFromDisk()
{
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

    // 清理现有 Runtime
    for (AgentRuntime* runtime : m_runtimes)
        runtime->deleteLater();
    m_runtimes.clear();

    // 清理现有 Session（SessionManager 会处理）
    for (Session* session : m_sessionManager->allSessions())
        m_sessionManager->removeSession(session->id());

    if (arr.isEmpty()) {
        m_currentSessionId.clear();
        return true;
    }

    QString userId = m_identityManager->userIdentity()->id();

    for (const QJsonValue& v : arr) {
        QJsonObject s = v.toObject();
        QString uuid = s[QStringLiteral("uuid")].toString().trimmed();
        if (uuid.isEmpty())
            uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);

        QString title = s[QStringLiteral("title")].toString();
        if (title.isEmpty())
            title = s[QStringLiteral("agentName")].toString();
        if (title.isEmpty())
            title = QObject::tr("新对话");

        // 创建 Agent Identity
        auto* profile = new IdentityProfile();
        profile->setLlmConfig(m_defaultAgentConfig);
        profile->setSystemPrompt(m_defaultAgentConfig.systemPrompt);
        Identity* agentIdentity = m_identityManager->createAgent(title, profile);

        // 创建 Session
        Session* session = m_sessionManager->createPrivateSession(userId, agentIdentity->id());
        const QString oldId = session->id();
        session->setId(uuid);
        m_sessionManager->replaceSessionId(oldId, uuid);
        session->setTitle(title);

        // 加载历史
        QJsonArray history = s[QStringLiteral("history")].toArray();
        QJsonArray ioHistory = s[QStringLiteral("io_history")].toArray();
        session->setLlmHistory(history);
        session->setIoHistory(ioHistory);

        // 创建 Runtime 并加载历史
        AgentRuntime* runtime = ensureRuntimeForSession(session->id());
        if (runtime) {
            runtime->setHistory(history);
            runtime->setIoHistory(ioHistory);
        }
    }

    QString savedSessionId = root[QStringLiteral("currentSessionId")].toString();
    if (!savedSessionId.isEmpty() && m_sessionManager->findById(savedSessionId)) {
        m_currentSessionId = savedSessionId;
    } else {
        // 回退到第一个 Session
        QList<Session*> sessions = m_sessionManager->allSessions();
        if (!sessions.isEmpty())
            m_currentSessionId = sessions.first()->id();
    }

    return true;
}

void ChatService::connectRuntimeSignals(AgentRuntime* runtime)
{
    if (!runtime)
        return;
    connect(runtime, &AgentRuntime::streamDataReceived, this, &ChatService::streamDataReceived);
    connect(runtime, &AgentRuntime::finished, this, &ChatService::finished);
    connect(runtime, &AgentRuntime::errorOccurred, this, &ChatService::errorOccurred);
    connect(runtime, &AgentRuntime::toolCallsStarted, this, &ChatService::toolCallsStarted);
    connect(runtime, &AgentRuntime::toolEvent, this, &ChatService::toolEvent);
}
