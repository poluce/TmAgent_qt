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
#include "core/persistence/ChatPersistenceService.h"
#include "core/service/ChatStateRepository.h"
#include "core/utils/ModelConfigLoader.h"
#include "core/utils/DefaultPrompts.h"
#include "newCore/ModelFactory.h"
#include "newCore/LLMTypes.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSet>
#include <QUuid>

namespace {
QJsonObject toolEventToJson(const ToolExecutionEvent& event)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("toolName"), event.toolName);
    obj.insert(QStringLiteral("toolId"), event.toolId);
    obj.insert(QStringLiteral("status"), event.status);
    obj.insert(QStringLiteral("success"), event.success);
    obj.insert(QStringLiteral("data"), event.data);
    obj.insert(QStringLiteral("rawResult"), event.rawResult);
    obj.insert(QStringLiteral("formattedResult"), event.formattedResult);
    return obj;
}

QStringList collectToolNames(ToolDispatcher* dispatcher)
{
    QStringList names;
    if (!dispatcher)
        return names;

    const QList<Tool> tools = dispatcher->getAllToolSchemas();
    for (const Tool& tool : tools) {
        const QString name = tool.name.trimmed();
        if (!name.isEmpty())
            names.append(name);
    }
    names.removeDuplicates();
    return names;
}

bool envFlagEnabled(const char* key)
{
    const QString raw =
        QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(key)).trimmed().toLower();
    return raw == QLatin1String("1")
           || raw == QLatin1String("true")
           || raw == QLatin1String("yes")
           || raw == QLatin1String("on");
}

} // namespace

ChatService::ChatService(QObject* parent)
    : QObject(parent)
    , m_persistence(new ChatPersistenceService())
    , m_stateRepository(new ChatStateRepository())
    , m_logVerboseStreamEvents(envFlagEnabled("TMAGENT_LOG_STREAM_EVENTS_VERBOSE"))
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
    if (m_stateRepository) {
        m_stateRepository->setDependencies(
            m_identityManager,
            m_sessionManager,
            m_toolDispatcher,
            m_persistence.get());
    }
    applyMcpConfig(loadMcpConfigSpecs());

    if (m_sessionManager) {
        connect(m_sessionManager,
                &SessionManager::messagePosted,
                this,
                &ChatService::appendSessionMessageToDisk,
                Qt::UniqueConnection);
    }

    // 确保用户 Identity 存在
    m_identityManager->userIdentity();

    loadConfig();
}

QString ChatService::enqueueUserMessage(const QString& sessionId, const QString& text, const QString& clientMessageId)
{
    const QString userId = m_identityManager ? m_identityManager->userIdentity()->id() : QString();
    return enqueueUserMessageAs(userId, sessionId, text, clientMessageId);
}

QString ChatService::enqueueUserMessageAs(const QString& actorIdentityId,
                                          const QString& sessionId,
                                          const QString& text,
                                          const QString& clientMessageId)
{
    if (!canIdentitySendMessage(actorIdentityId, sessionId)) {
        qWarning() << "[ChatService] 拒绝发送消息，actor 无权限:" << actorIdentityId
                   << "session:" << sessionId;
        return QString();
    }

    const QString prompt = text.trimmed();
    if (prompt.isEmpty())
        return QString();

    Session* session = m_sessionManager ? m_sessionManager->findById(sessionId) : nullptr;
    if (!session)
        return QString();

    Identity* actor = m_identityManager ? m_identityManager->findById(actorIdentityId) : nullptr;
    if (!actor)
        return QString();
    const QString actorId = actor->id().trimmed();
    if (actorId.isEmpty())
        return QString();

    ensurePipeline(sessionId);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    TurnTask* mergeTarget = nullptr;
    if (TurnTask* tail = m_turnManager.queuedTail(sessionId)) {
        const QString tailActorId = tail->actorIdentityId.trimmed();
        const int tailMergedCount = qMax(1, tail->mergedMessageCount);
        const bool sameActor = !tailActorId.isEmpty() && tailActorId == actorId;
        const bool withinWindow =
            tail->enqueuedAtMs > 0 &&
            nowMs >= tail->enqueuedAtMs &&
            (nowMs - tail->enqueuedAtMs) <= kQueueMergeWindowMs;
        const bool withinMergeCount = tailMergedCount < kQueueMergeMaxMergedMessages;
        const bool withinMergedSize =
            (tail->userContent.size() + prompt.size() + 32) <= kQueueMergeMaxChars;
        if (sameActor && withinWindow && withinMergeCount && withinMergedSize)
            mergeTarget = tail;
    }

    const int queueDepthBeforeEnqueue = m_turnManager.totalDepth(sessionId);
    if (!mergeTarget && queueDepthBeforeEnqueue >= kHardQueueDepth) {
        QJsonObject extra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("queue_overflow"));
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        extra.insert(QStringLiteral("queueHardLimit"), kHardQueueDepth);
        emitPipelineEvent(QStringLiteral("turn_rejected"), sessionId, nullptr,
                          QString(), QStringLiteral("queue overflow"), extra);
        return QString();
    }
    if (!mergeTarget && queueDepthBeforeEnqueue >= kSoftQueueDepth) {
        QJsonObject extra;
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        extra.insert(QStringLiteral("queueSoftLimit"), kSoftQueueDepth);
        emitPipelineEvent(QStringLiteral("queue_backpressure"), sessionId, nullptr,
                          QString(), QString(), extra);
    }

    QString requestTraceId = mergeTarget ? mergeTarget->requestTraceId : QString();
    if (requestTraceId.trimmed().isEmpty())
        requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString turnId = mergeTarget ? mergeTarget->turnId : QString();
    if (turnId.trimmed().isEmpty())
        turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (mergeTarget) {
        mergeTarget->requestTraceId = requestTraceId;
        mergeTarget->turnId = turnId;
    }

    TurnTask turn;
    turn.requestTraceId = requestTraceId;
    turn.turnId = turnId;
    turn.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    turn.actorIdentityId = actorId;
    turn.enqueuedAtMs = nowMs;
    turn.mergedMessageCount = 1;
    turn.clientMessageId = clientMessageId.trimmed();
    turn.userContent = prompt;

    // Message 成为会话主干数据：用户消息先写入 Session，再进入执行流水线。
    Message userMsg = Message::createText(sessionId, actorId, prompt);
    userMsg.traceId = requestTraceId;
    userMsg.turnId = turnId;
    userMsg.status = Message::Status::Completed;
    m_sessionManager->postMessage(sessionId, userMsg);

    if (mergeTarget) {
        mergeTarget->enqueuedAtMs = nowMs;
        mergeTarget->mergedMessageCount = qMax(1, mergeTarget->mergedMessageCount) + 1;
        if (!turn.clientMessageId.isEmpty())
            mergeTarget->clientMessageId = turn.clientMessageId;
        mergeTarget->userContent.append(QStringLiteral("\n\n[补充消息]\n"));
        mergeTarget->userContent.append(prompt);

        QJsonObject extra;
        extra.insert(QStringLiteral("mergedIntoTurnId"), mergeTarget->turnId);
        extra.insert(QStringLiteral("mergedMessageCount"), mergeTarget->mergedMessageCount);
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        emitPipelineEvent(QStringLiteral("turn_merged"), sessionId, mergeTarget,
                          QString(), QString(), extra);
        return mergeTarget->turnId;
    }

    // 多轮连续发消息：只入队，不打断当前正在执行的 turn。
    // 当前 turn 完成后，按队列顺序执行后续消息。
    m_turnManager.enqueueTurn(sessionId, turn);

    emitPipelineEvent(QStringLiteral("turn_queued"), sessionId, &turn);
    tryStartNextTurn(sessionId);
    return turn.turnId;
}

void ChatService::sendUserMessage(const QString& sessionId, const QString& text)
{
    const QString userId = m_identityManager ? m_identityManager->userIdentity()->id() : QString();
    sendUserMessageAs(userId, sessionId, text);
}

void ChatService::sendUserMessageAs(const QString& actorIdentityId,
                                    const QString& sessionId,
                                    const QString& text)
{
    enqueueUserMessageAs(actorIdentityId, sessionId, text);
}

void ChatService::abortCurrent(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !m_turnManager.hasActiveTurn(sessionId)) {
        resetSessionStreamState(sessionId);
        return;
    }

    if (TurnTask* active = m_turnManager.activeTurn(sessionId))
        flushPendingDeltaLog(sessionId, pipeline, active, true);

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime)
        runtime->abort();

    TurnTask cancelled;
    if (!m_turnManager.clearActiveTurn(sessionId, &cancelled))
        return;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);
    QJsonObject extra;
    extra.insert(QStringLiteral("reason"), QStringLiteral("user_stop"));
    emitPipelineEvent(QStringLiteral("turn_cancelled"), sessionId, &cancelled,
                      QString(), QString(), extra);
    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
}

QString ChatService::abortAndRollback(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !m_turnManager.hasActiveTurn(sessionId)) {
        resetSessionStreamState(sessionId);
        return QString();
    }

    if (TurnTask* active = m_turnManager.activeTurn(sessionId))
        flushPendingDeltaLog(sessionId, pipeline, active, true);

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    QString rolledBack;
    if (runtime)
        rolledBack = runtime->abortAndRollback();

    TurnTask cancelled;
    if (!m_turnManager.clearActiveTurn(sessionId, &cancelled))
        return rolledBack;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);

    if (rolledBack.isEmpty())
        rolledBack = cancelled.userContent;

    QJsonObject extra;
    extra.insert(QStringLiteral("reason"), QStringLiteral("user_stop"));
    extra.insert(QStringLiteral("rolledBackUserMessage"), rolledBack);
    emitPipelineEvent(QStringLiteral("turn_cancelled"), sessionId, &cancelled, QString(), QString(), extra);
    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
    return rolledBack;
}

Session* ChatService::createNewSession(const QString& agentName)
{
    QString userId = m_identityManager->userIdentity()->id();

    // 创建 Agent Identity
    auto* profile = new IdentityProfile();
    profile->setLlmConfig(m_defaultAgentConfig);
    profile->setSystemPrompt(m_defaultAgentConfig.systemPrompt);
    profile->setAllowedTools(collectToolNames(m_toolDispatcher));

    QString name = agentName.isEmpty() ? QStringLiteral("TM Agent") : agentName;
    Identity* agentIdentity = m_identityManager->createAgent(name, profile);

    // 创建 Private Session
    Session* session = m_sessionManager->createPrivateSession(userId, agentIdentity->id());
    session->setTitle(name);

    m_currentSessionId = session->id();
    emit sessionCreated(session->id());
    saveSessionsToDisk();
    return session;
}

Session* ChatService::createSessionForIdentity(const QString& identityId, const QString& title)
{
    const QString userId = m_identityManager ? m_identityManager->userIdentity()->id() : QString();
    return createSessionForIdentityAs(userId, identityId, title);
}

Session* ChatService::createSessionForIdentityAs(const QString& actorIdentityId,
                                                 const QString& identityId,
                                                 const QString& title)
{
    if (!canIdentityManageSessions(actorIdentityId)) {
        qWarning() << "[ChatService] 拒绝创建会话，actor 无权限:" << actorIdentityId
                   << "target:" << identityId;
        return nullptr;
    }

    Identity* identity = m_identityManager->findById(identityId);
    if (!identity)
        return nullptr;

    // 用户视角走原逻辑
    if (identity->isUser())
        return createNewSession(title);

    // Agent 视角：复用已有 Agent Identity，创建新 Private Session
    QString userId = m_identityManager->userIdentity()->id();
    Session* session = m_sessionManager->createPrivateSession(userId, identityId);
    session->setTitle(title.isEmpty() ? identity->name() : title);

    emit sessionCreated(session->id());
    saveSessionsToDisk();
    return session;
}

QList<Session*> ChatService::sessionsForIdentity(const QString& identityId) const
{
    return m_sessionManager->sessionsForIdentity(identityId);
}

void ChatService::removeSession(const QString& sessionId)
{
    const QString userId = m_identityManager ? m_identityManager->userIdentity()->id() : QString();
    removeSessionAs(userId, sessionId);
}

bool ChatService::removeSessionAs(const QString& actorIdentityId, const QString& sessionId)
{
    if (!canIdentityManageSessions(actorIdentityId)) {
        qWarning() << "[ChatService] 拒绝删除会话，actor 无权限:" << actorIdentityId
                   << "session:" << sessionId;
        return false;
    }

    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return false;

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime && runtime->isStreaming() && m_agentActiveSession.value(agentId) == sessionId) {
        runtime->abort();
        m_agentActiveSession.remove(agentId);
    }

    m_turnManager.removePipeline(sessionId);

    m_sessionManager->removeSession(sessionId);
    if (!agentId.isEmpty()) {
        tryStartNextTurnForAgent(agentId);
        releaseRuntimeIfUnused(agentId);
    }

    if (m_currentSessionId == sessionId) {
        m_currentSessionId.clear();
    }

    emit sessionRemoved(sessionId);
    saveSessionsToDisk();
    return true;
}

void ChatService::switchSession(const QString& sessionId)
{
    if (sessionId == m_currentSessionId)
        return;
    m_currentSessionId = sessionId;

    // 同步 Runtime 的会话上下文（仅在未流式时切换，避免串流路由错位）。
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime && !runtime->isStreaming())
        runtime->switchToSession(sessionId);
}

QString ChatService::currentSessionId() const { return m_currentSessionId; }

AgentRuntime* ChatService::runtimeForSession(const QString& sessionId) const
{
    const QString agentId = agentIdentityIdForSession(sessionId);
    if (agentId.isEmpty())
        return nullptr;
    return m_runtimes.value(agentId, nullptr);
}

AgentRuntime* ChatService::ensureRuntimeForSession(const QString& sessionId)
{
    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return nullptr;

    Identity* agentIdentity = findOrCreateAgentIdentity(session);
    AgentRuntime* runtime = ensureRuntimeForAgent(agentIdentity);
    if (!runtime)
        return nullptr;

    // 仅在 Runtime 空闲或已绑定当前会话时装载历史，避免覆盖其他会话正在运行的上下文。
    const QString activeSessionId = m_agentActiveSession.value(agentIdentity->id());
    if (activeSessionId.isEmpty() || activeSessionId == sessionId) {
        runtime->switchToSession(sessionId);
        runtime->setHistory(buildRuntimeHistoryFromMessages(session));
    }
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
        runtime->setConfig(composeConfigForIdentity(runtime->identity()));
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
    if (m_turnManager.hasActiveTurn(sessionId))
        return true;

    Session* session = m_sessionManager->findById(sessionId);
    return session && session->isStreaming();
}

int ChatService::pendingTurnCount(const QString& sessionId) const
{
    return m_turnManager.queuedTurnCount(sessionId);
}

QString ChatService::activeRunId(const QString& sessionId) const
{
    const TurnTask* active = m_turnManager.activeTurn(sessionId);
    if (!active)
        return QString();
    return active->runId;
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

bool ChatService::canIdentityManageSessions(const QString& identityId) const
{
    return isUserIdentity(identityId);
}

bool ChatService::canIdentitySendMessage(const QString& identityId, const QString& sessionId) const
{
    if (!isUserIdentity(identityId))
        return false;

    if (sessionId.isEmpty())
        return true;

    return m_sessionManager && m_sessionManager->findById(sessionId) != nullptr;
}

bool ChatService::canIdentityManageGlobalConfig(const QString& identityId) const
{
    return isUserIdentity(identityId);
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
    return m_persistence ? m_persistence->loadMcpConfigSpecs() : QStringList();
}

bool ChatService::saveMcpConfigSpecs(const QStringList& specs) const
{
    return m_persistence && m_persistence->saveMcpConfigSpecs(specs);
}

QString ChatService::mcpConfigPath() const
{
    return m_persistence ? m_persistence->mcpConfigPath() : QString();
}

QString ChatService::modelConfigPath() const
{
    return m_persistence ? m_persistence->modelConfigPath() : QString();
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
    const QString legacyQtPrompt = QStringLiteral("你是一个专业的 Qt 高级开发工程师，精通 C++、Qt 框架和跨平台开发。");
    const QString legacyGenericPrompt = QStringLiteral("你是一个专业的 AI 助手。");
    if (defaultConfig.systemPrompt.trimmed().isEmpty()
        || defaultConfig.systemPrompt == legacyQtPrompt
        || defaultConfig.systemPrompt == legacyGenericPrompt) {
        defaultConfig.systemPrompt = DefaultPrompts::codingAssistantSystemPrompt();
    }

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

void ChatService::appendSessionMessageToDisk(const QString& sessionId, const Message& msg)
{
    if (!m_persistence || sessionId.trimmed().isEmpty() || !msg.isValid())
        return;
    if (!m_persistence->appendSessionMessage(sessionId, m_persistence->messageToJson(msg))) {
        qWarning() << "[ChatService] 消息追加写入失败，sessionId=" << sessionId
                   << "messageId=" << msg.id;
        return;
    }

    Session* session = m_sessionManager ? m_sessionManager->findById(sessionId) : nullptr;
    if (session)
        m_lastSavedMessageCounts.insert(sessionId, session->messageCount());
}

void ChatService::saveSessionsToDisk()
{
    if (!m_stateRepository)
        return;
    m_lastSavedMessageCounts = m_stateRepository->saveState(
        m_currentSessionId,
        m_lastSavedMessageCounts,
        [this](const QString& sid) -> const SessionPipeline* {
            return findPipeline(sid);
        });
}

bool ChatService::loadSessionsFromDisk()
{
    if (!m_stateRepository)
        return false;

    // 清理现有 Runtime
    for (AgentRuntime* runtime : m_runtimes)
        runtime->deleteLater();
    m_runtimes.clear();
    m_turnManager.clear();
    m_agentActiveSession.clear();
    m_lastSavedMessageCounts.clear();

    const ChatStateRepository::LoadResult loaded = m_stateRepository->loadState(m_defaultAgentConfig);
    if (!loaded.success)
        return false;

    m_lastSavedMessageCounts = loaded.savedMessageCounts;
    m_currentSessionId = loaded.currentSessionId;

    for (auto it = loaded.pendingTurnsBySession.constBegin();
         it != loaded.pendingTurnsBySession.constEnd();
         ++it) {
        SessionPipeline& pipeline = ensurePipeline(it.key());
        for (const TurnTask& turn : it.value())
            pipeline.queue.append(turn);
        tryStartNextTurn(it.key());
    }

    return true;
}

SessionPipeline& ChatService::ensurePipeline(const QString& sessionId)
{
    return m_turnManager.ensurePipeline(sessionId);
}

SessionPipeline* ChatService::findPipeline(const QString& sessionId)
{
    return m_turnManager.findPipeline(sessionId);
}

const SessionPipeline* ChatService::findPipeline(const QString& sessionId) const
{
    return m_turnManager.findPipeline(sessionId);
}

QString ChatService::agentIdentityIdForSession(const QString& sessionId) const
{
    if (!m_sessionManager || !m_identityManager || sessionId.trimmed().isEmpty())
        return QString();

    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return QString();

    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_identityManager->findById(pid);
        if (identity && identity->isAgent())
            return identity->id();
    }
    return QString();
}

Identity* ChatService::findOrCreateAgentIdentity(Session* session)
{
    if (!session || !m_identityManager)
        return nullptr;

    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_identityManager->findById(pid);
        if (identity && identity->isAgent())
            return identity;
    }

    auto* profile = new IdentityProfile();
    profile->setLlmConfig(m_defaultAgentConfig);
    profile->setSystemPrompt(m_defaultAgentConfig.systemPrompt);
    profile->setAllowedTools(collectToolNames(m_toolDispatcher));
    Identity* agentIdentity = m_identityManager->createAgent(
        session->title().isEmpty() ? QStringLiteral("TM Agent") : session->title(),
        profile);
    session->addParticipant(agentIdentity->id());
    return agentIdentity;
}

LLMConfig ChatService::composeConfigForIdentity(Identity* identity) const
{
    LLMConfig cfg = m_defaultAgentConfig;
    if (!identity)
        return cfg;

    cfg.userName = identity->name();
    cfg.uuid = identity->id();

    if (!identity->profile())
        return cfg;

    const LLMConfig profileCfg = identity->profile()->llmConfig();
    if (profileCfg.isValid()) {
        cfg.model = profileCfg.model;
        cfg.customModelId = profileCfg.customModelId;
    }
    if (!identity->profile()->systemPrompt().trimmed().isEmpty())
        cfg.systemPrompt = identity->profile()->systemPrompt().trimmed();
    return cfg;
}

QJsonArray ChatService::buildRuntimeHistoryFromMessages(Session* session) const
{
    QJsonArray history;
    if (!session)
        return history;

    QSet<QString> validToolCallIds;
    const QList<Message> messages = session->allMessages();
    for (const Message& msg : messages) {
        if (msg.status == Message::Status::Cancelled
            || msg.status == Message::Status::Interrupted
            || msg.status == Message::Status::Error) {
            continue;
        }

        const QString content = msg.content.text;
        const QString trimmedContent = content.trimmed();

        if (msg.content.type == MessageContent::Type::System
            || msg.senderId == QLatin1String("system")) {
            if (trimmedContent.isEmpty())
                continue;
            QJsonObject item;
            item.insert(QStringLiteral("role"), QStringLiteral("system"));
            item.insert(QStringLiteral("content"), content);
            history.append(item);
            continue;
        }

        if (msg.content.type == MessageContent::Type::ToolCall) {
            QJsonObject item;
            item.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            if (!trimmedContent.isEmpty())
                item.insert(QStringLiteral("content"), content);

            QJsonArray toolCalls = msg.content.payload.value(QStringLiteral("tool_calls")).toArray();
            if (toolCalls.isEmpty()) {
                QString toolName = msg.content.payload.value(QStringLiteral("tool_name")).toString().trimmed();
                if (toolName.isEmpty())
                    toolName = trimmedContent;
                if (!toolName.isEmpty()) {
                    QJsonObject args = msg.content.payload.value(QStringLiteral("arguments")).toObject();
                    if (args.isEmpty())
                        args = msg.content.payload;
                    args.remove(QStringLiteral("tool_calls"));
                    args.remove(QStringLiteral("tool_name"));
                    args.remove(QStringLiteral("tool_call_id"));
                    args.remove(QStringLiteral("arguments"));

                    QJsonObject functionObj;
                    functionObj.insert(QStringLiteral("name"), toolName);
                    functionObj.insert(
                        QStringLiteral("arguments"),
                        QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));

                    QJsonObject toolCallObj;
                    toolCallObj.insert(
                        QStringLiteral("id"),
                        msg.content.payload.value(QStringLiteral("tool_call_id"))
                            .toString()
                            .trimmed()
                            .isEmpty()
                            ? msg.id
                            : msg.content.payload.value(QStringLiteral("tool_call_id")).toString().trimmed());
                    toolCallObj.insert(QStringLiteral("type"), QStringLiteral("function"));
                    toolCallObj.insert(QStringLiteral("function"), functionObj);
                    toolCalls.append(toolCallObj);
                }
            }

            if (!toolCalls.isEmpty())
                item.insert(QStringLiteral("tool_calls"), toolCalls);
            for (const QJsonValue& v : toolCalls) {
                const QString toolCallId = v.toObject().value(QStringLiteral("id")).toString().trimmed();
                if (!toolCallId.isEmpty())
                    validToolCallIds.insert(toolCallId);
            }
            if (item.contains(QStringLiteral("content")) || item.contains(QStringLiteral("tool_calls")))
                history.append(item);
            continue;
        }

        if (msg.content.type == MessageContent::Type::ToolResult) {
            QString toolContent = content;
            if (toolContent.trimmed().isEmpty())
                toolContent = msg.content.payload.value(QStringLiteral("raw_result")).toString();
            if (toolContent.trimmed().isEmpty())
                continue;

            QString toolCallId = msg.content.payload.value(QStringLiteral("tool_call_id")).toString().trimmed();
            if (toolCallId.isEmpty())
                toolCallId = msg.content.payload.value(QStringLiteral("id")).toString().trimmed();
            if (toolCallId.isEmpty())
                continue;
            if (!validToolCallIds.contains(toolCallId))
                continue;

            QJsonObject item;
            item.insert(QStringLiteral("role"), QStringLiteral("tool"));
            item.insert(QStringLiteral("tool_call_id"), toolCallId);
            item.insert(QStringLiteral("content"), toolContent);
            history.append(item);
            continue;
        }

        if (trimmedContent.isEmpty())
            continue;

        QJsonObject item;
        Identity* sender = m_identityManager ? m_identityManager->findById(msg.senderId) : nullptr;
        const bool isUser = sender && sender->isUser();
        item.insert(QStringLiteral("role"), isUser ? QStringLiteral("user") : QStringLiteral("assistant"));
        item.insert(QStringLiteral("content"), content);
        history.append(item);
    }
    return history;
}

AgentRuntime* ChatService::ensureRuntimeForAgent(Identity* agentIdentity)
{
    if (!agentIdentity || !agentIdentity->isAgent())
        return nullptr;

    const QString agentId = agentIdentity->id();
    if (AgentRuntime* existing = m_runtimes.value(agentId, nullptr))
        return existing;

    auto* runtime = new AgentRuntime(agentIdentity, this);
    runtime->setModelFactory(m_modelFactory);
    runtime->setToolDispatcher(m_toolDispatcher);
    runtime->setConfig(composeConfigForIdentity(agentIdentity));
    connectRuntimeSignals(runtime);
    m_runtimes.insert(agentId, runtime);
    return runtime;
}

void ChatService::releaseRuntimeIfUnused(const QString& agentIdentityId)
{
    if (agentIdentityId.trimmed().isEmpty())
        return;

    if (m_sessionManager && !m_sessionManager->sessionsForIdentity(agentIdentityId).isEmpty())
        return;

    AgentRuntime* runtime = m_runtimes.take(agentIdentityId);
    if (runtime) {
        if (runtime->isStreaming())
            runtime->abort();
        runtime->deleteLater();
    }
    m_agentActiveSession.remove(agentIdentityId);
}

void ChatService::tryStartNextTurnForAgent(const QString& agentIdentityId)
{
    if (agentIdentityId.trimmed().isEmpty())
        return;

    if (!m_agentActiveSession.value(agentIdentityId).isEmpty())
        return;

    const QStringList sessionIds = m_turnManager.sessionIds();
    for (const QString& sid : sessionIds) {
        if (agentIdentityIdForSession(sid) != agentIdentityId)
            continue;
        tryStartNextTurn(sid);
        if (!m_agentActiveSession.value(agentIdentityId).isEmpty())
            break;
    }
}

void ChatService::resetSessionStreamState(const QString& sessionId)
{
    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return;
    Session::StreamState& state = session->streamState();
    state.isStreaming = false;
    state.buffer.clear();
    state.hasPendingMessage = false;
    state.lastMsgIsTool = false;
}

void ChatService::flushPendingDeltaLog(const QString& sessionId,
                                       SessionPipeline* pipeline,
                                       const TurnTask* turn,
                                       bool force)
{
    if (m_logVerboseStreamEvents || !pipeline || pipeline->pendingDeltaLog.isEmpty())
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int charCount = pipeline->pendingDeltaLog.size();
    const int chunkCount = pipeline->pendingDeltaChunks;
    const qint64 spanMs =
        pipeline->pendingDeltaStartedAtMs > 0 ? (nowMs - pipeline->pendingDeltaStartedAtMs) : 0;

    if (!force) {
        const bool byChars = charCount >= kDeltaBatchFlushChars;
        const bool byChunks = chunkCount >= kDeltaBatchFlushChunks;
        const bool byInterval =
            pipeline->lastDeltaFlushedAtMs <= 0
                ? (spanMs >= kDeltaBatchFlushIntervalMs)
                : (nowMs - pipeline->lastDeltaFlushedAtMs >= kDeltaBatchFlushIntervalMs);
        if (!byChars && !byChunks && !byInterval)
            return;
    }

    QJsonObject extra;
    extra.insert(QStringLiteral("chunkCount"), chunkCount);
    extra.insert(QStringLiteral("charCount"), charCount);
    extra.insert(QStringLiteral("batched"), true);
    if (spanMs > 0)
        extra.insert(QStringLiteral("spanMs"), static_cast<double>(spanMs));

    emitPipelineEvent(QStringLiteral("turn_delta_batch"),
                      sessionId,
                      turn,
                      pipeline->pendingDeltaLog,
                      QString(),
                      extra,
                      true);

    pipeline->pendingDeltaLog.clear();
    pipeline->pendingDeltaChunks = 0;
    pipeline->pendingDeltaStartedAtMs = 0;
    pipeline->lastDeltaFlushedAtMs = nowMs;
}

bool ChatService::appendEventLog(const QJsonObject& event) const
{
    return m_persistence && m_persistence->appendEventLog(event);
}

void ChatService::emitPipelineEvent(const QString& type,
                                    const QString& sessionId,
                                    const TurnTask* turn,
                                    const QString& delta,
                                    const QString& error,
                                    const QJsonObject& extra,
                                    bool persistToDisk)
{
    SessionPipeline* pipeline = findPipeline(sessionId);

    QJsonObject event;
    event.insert(QStringLiteral("event_schema_version"), 1);
    event.insert(QStringLiteral("type"), type);
    event.insert(QStringLiteral("sessionId"), sessionId);
    event.insert(QStringLiteral("session_id"), sessionId);
    event.insert(QStringLiteral("timestamp"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (pipeline) {
        event.insert(QStringLiteral("seq"), static_cast<qint64>(++pipeline->seq));
        event.insert(QStringLiteral("queueDepth"), pipeline->queue.size());
        event.insert(QStringLiteral("hasActiveRun"), pipeline->hasActiveTurn);
    }

    if (turn) {
        if (!turn->requestTraceId.isEmpty())
            event.insert(QStringLiteral("trace_id"), turn->requestTraceId);
        event.insert(QStringLiteral("turnId"), turn->turnId);
        event.insert(QStringLiteral("runId"), turn->runId);
        event.insert(QStringLiteral("turn_id"), turn->turnId);
        event.insert(QStringLiteral("run_id"), turn->runId);
        if (!turn->actorIdentityId.isEmpty())
            event.insert(QStringLiteral("actorIdentityId"), turn->actorIdentityId);
        if (turn->enqueuedAtMs > 0)
            event.insert(QStringLiteral("enqueuedAtMs"), static_cast<double>(turn->enqueuedAtMs));
        if (turn->mergedMessageCount > 1)
            event.insert(QStringLiteral("mergedMessageCount"), turn->mergedMessageCount);
        if (!turn->clientMessageId.isEmpty())
            event.insert(QStringLiteral("clientMessageId"), turn->clientMessageId);
    }
    if (!delta.isEmpty())
        event.insert(QStringLiteral("delta"), delta);
    if (!error.isEmpty())
        event.insert(QStringLiteral("error"), error);

    for (auto it = extra.begin(); it != extra.end(); ++it)
        event.insert(it.key(), it.value());

    emit conversationEvent(event);
    if (persistToDisk && !appendEventLog(event)) {
        const QString logPath = m_persistence
            ? m_persistence->eventsCurrentLogPath()
            : QStringLiteral("<persistence-unavailable>");
        qWarning() << "[ChatService] 事件日志写入失败：" << logPath;
    }
}

void ChatService::tryStartNextTurn(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline)
        return;
    if (m_turnManager.hasActiveTurn(sessionId)
        || m_turnManager.queuedTurnCount(sessionId) <= 0) {
        return;
    }

    AgentRuntime* runtime = ensureRuntimeForSession(sessionId);
    if (!runtime)
        return;
    const QString agentId = runtime->identityId().trimmed();
    if (agentId.isEmpty())
        return;

    const QString activeSessionId = m_agentActiveSession.value(agentId);
    if (!activeSessionId.isEmpty() && activeSessionId != sessionId)
        return; // 同一 Agent 的 Runtime 正在处理另一个会话

    TurnTask startedTurn;
    if (!m_turnManager.startNextTurn(sessionId, &startedTurn))
        return;
    if (startedTurn.requestTraceId.isEmpty()) {
        startedTurn.requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (TurnTask* active = m_turnManager.activeTurn(sessionId))
            active->requestTraceId = startedTurn.requestTraceId;
    }
    m_agentActiveSession.insert(agentId, sessionId);

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
        state.isStreaming = true;
    }

    emitPipelineEvent(QStringLiteral("turn_started"), sessionId, &startedTurn);
    runtime->sendMessage(sessionId, startedTurn.userContent);
}

void ChatService::finalizeTurn(const QString& sessionId, TurnTask* outTurn)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (pipeline && activeTurn)
        flushPendingDeltaLog(sessionId, pipeline, activeTurn, true);

    m_turnManager.clearActiveTurn(sessionId, outTurn);

    const QString agentId = agentIdentityIdForSession(sessionId);
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);

    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
}

void ChatService::onRuntimeStreamData(const QString& sessionId, const QString& data)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (!pipeline || !activeTurn)
        return;

    activeTurn->assistantContent.append(data);

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.append(data);
        state.isStreaming = true;
    }

    emit streamDataReceived(sessionId, data);
    emitPipelineEvent(QStringLiteral("turn_delta"),
                      sessionId,
                      activeTurn,
                      data,
                      QString(),
                      QJsonObject(),
                      m_logVerboseStreamEvents);

    if (!m_logVerboseStreamEvents && !data.isEmpty()) {
        if (pipeline->pendingDeltaLog.isEmpty())
            pipeline->pendingDeltaStartedAtMs = QDateTime::currentMSecsSinceEpoch();
        pipeline->pendingDeltaLog.append(data);
        ++pipeline->pendingDeltaChunks;
        flushPendingDeltaLog(sessionId, pipeline, activeTurn, false);
    }
}

void ChatService::onRuntimeFinished(const QString& sessionId, const QString& fullContent)
{
    TurnTask finishedTurn;
    finalizeTurn(sessionId, &finishedTurn);
    if (!fullContent.isEmpty())
        finishedTurn.assistantContent = fullContent;

    const QString agentId = agentIdentityIdForSession(sessionId);
    if (!finishedTurn.assistantContent.trimmed().isEmpty() && !agentId.isEmpty()) {
        Message assistantMsg = Message::createText(sessionId, agentId, finishedTurn.assistantContent);
        assistantMsg.traceId = finishedTurn.requestTraceId;
        assistantMsg.turnId = finishedTurn.turnId;
        assistantMsg.status = Message::Status::Completed;
        m_sessionManager->postMessage(sessionId, assistantMsg);
    }

    QJsonObject extra;
    extra.insert(QStringLiteral("fullContent"), finishedTurn.assistantContent);
    emit finished(sessionId, finishedTurn.assistantContent);
    emitPipelineEvent(QStringLiteral("turn_completed"), sessionId, &finishedTurn,
                      QString(), QString(), extra);
}

void ChatService::onRuntimeError(const QString& sessionId, const QString& errorMsg)
{
    TurnTask failedTurn;
    finalizeTurn(sessionId, &failedTurn);

    emit errorOccurred(sessionId, errorMsg);
    emitPipelineEvent(QStringLiteral("turn_failed"), sessionId, &failedTurn,
                      QString(), errorMsg);
}

void ChatService::onRuntimeToolCallsStarted(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (!pipeline || !activeTurn)
        return;

    flushPendingDeltaLog(sessionId, pipeline, activeTurn, true);

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.clear();
        state.lastMsgIsTool = true;
    }

    emit toolCallsStarted(sessionId);
    emitPipelineEvent(QStringLiteral("turn_tool_calls_started"), sessionId, activeTurn);
}

void ChatService::onRuntimeToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (!pipeline || !activeTurn)
        return;

    const QString agentId = agentIdentityIdForSession(sessionId);
    if (m_sessionManager && !agentId.isEmpty()) {
        const QString toolId = event.toolId.trimmed();
        const QString toolName = event.toolName.trimmed();
        if (event.status == QLatin1String("started")) {
            if (toolId.isEmpty()) {
                qWarning() << "[ChatService] 跳过无效 tool_call 事件：缺少 toolId，session=" << sessionId
                           << "toolName=" << toolName;
            } else {
                Message toolCallMsg = Message::createToolCall(sessionId, agentId, QString(), QJsonObject());
                toolCallMsg.content.text.clear(); // 不在聊天区展示，仅用于上下文重建
                toolCallMsg.traceId = activeTurn->requestTraceId;
                toolCallMsg.turnId = activeTurn->turnId;
                toolCallMsg.status = Message::Status::Completed;
                toolCallMsg.content.payload.insert(QStringLiteral("tool_name"), toolName);
                toolCallMsg.content.payload.insert(QStringLiteral("tool_call_id"), toolId);
                toolCallMsg.content.payload.insert(QStringLiteral("arguments"), event.data);
                m_sessionManager->postMessage(sessionId, toolCallMsg);
            }
        } else if (event.status == QLatin1String("completed")) {
            const bool isPseudoToolEvent = (toolName == QLatin1String("tool_loop_guard"));
            if (toolId.isEmpty() || isPseudoToolEvent) {
                qWarning() << "[ChatService] 跳过无效 tool_result 事件：session=" << sessionId
                           << "toolName=" << toolName
                           << "toolId=" << toolId;
            } else {
                Message toolResultMsg = Message::createToolResult(sessionId, agentId, toolId, QString());
                toolResultMsg.content.text.clear(); // 避免污染聊天气泡
                toolResultMsg.traceId = activeTurn->requestTraceId;
                toolResultMsg.turnId = activeTurn->turnId;
                toolResultMsg.status = Message::Status::Completed;
                toolResultMsg.content.payload.insert(QStringLiteral("tool_name"), toolName);
                toolResultMsg.content.payload.insert(QStringLiteral("success"), event.success);
                toolResultMsg.content.payload.insert(QStringLiteral("raw_result"), event.rawResult);
                toolResultMsg.content.payload.insert(QStringLiteral("formatted_result"), event.formattedResult);
                m_sessionManager->postMessage(sessionId, toolResultMsg);
            }
        }
    }

    emit toolEvent(sessionId, event);
    QJsonObject extra;
    extra.insert(QStringLiteral("toolEvent"), toolEventToJson(event));
    emitPipelineEvent(QStringLiteral("turn_tool_event"), sessionId, activeTurn,
                      QString(), QString(), extra);
}

void ChatService::connectRuntimeSignals(AgentRuntime* runtime)
{
    if (!runtime)
        return;
    connect(runtime, &AgentRuntime::streamDataReceived, this, &ChatService::onRuntimeStreamData);
    connect(runtime, &AgentRuntime::finished, this, &ChatService::onRuntimeFinished);
    connect(runtime, &AgentRuntime::errorOccurred, this, &ChatService::onRuntimeError);
    connect(runtime, &AgentRuntime::toolCallsStarted, this, &ChatService::onRuntimeToolCallsStarted);
    connect(runtime, &AgentRuntime::toolEvent, this, &ChatService::onRuntimeToolEvent);
}

bool ChatService::isUserIdentity(const QString& identityId) const
{
    if (!m_identityManager || identityId.trimmed().isEmpty())
        return false;
    Identity* identity = m_identityManager->findById(identityId);
    return identity && identity->isUser();
}

void ChatService::saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId)
{
    if (!m_persistence)
        return;
    m_persistence->saveTabState(openAgentIds, activeIdentityId);
}

ChatService::TabState ChatService::loadTabState() const
{
    TabState state;
    if (!m_persistence)
        return state;
    const ChatPersistenceService::TabState persistedState = m_persistence->loadTabState();
    state.openAgentIds = persistedState.openAgentIds;
    state.activeIdentityId = persistedState.activeIdentityId;
    return state;
}
