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
#include "core/utils/DefaultPrompts.h"
#include "newCore/ModelFactory.h"
#include "newCore/LLMTypes.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
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

QString messageTypeToString(MessageContent::Type type)
{
    switch (type) {
    case MessageContent::Type::Text: return QStringLiteral("text");
    case MessageContent::Type::ToolCall: return QStringLiteral("tool_call");
    case MessageContent::Type::ToolResult: return QStringLiteral("tool_result");
    case MessageContent::Type::System: return QStringLiteral("system");
    case MessageContent::Type::File: return QStringLiteral("file");
    }
    return QStringLiteral("text");
}

MessageContent::Type messageTypeFromString(const QString& type)
{
    if (type == QLatin1String("tool_call")) return MessageContent::Type::ToolCall;
    if (type == QLatin1String("tool_result")) return MessageContent::Type::ToolResult;
    if (type == QLatin1String("system")) return MessageContent::Type::System;
    if (type == QLatin1String("file")) return MessageContent::Type::File;
    return MessageContent::Type::Text;
}

QString messageStatusToString(Message::Status status)
{
    switch (status) {
    case Message::Status::Pending: return QStringLiteral("pending");
    case Message::Status::Error: return QStringLiteral("error");
    case Message::Status::Sent:
    default:
        return QStringLiteral("sent");
    }
}

Message::Status messageStatusFromString(const QString& status)
{
    if (status == QLatin1String("pending")) return Message::Status::Pending;
    if (status == QLatin1String("error")) return Message::Status::Error;
    return Message::Status::Sent;
}

QJsonObject messageToJson(const Message& msg)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), msg.id);
    obj.insert(QStringLiteral("sessionId"), msg.sessionId);
    obj.insert(QStringLiteral("senderId"), msg.senderId);

    QJsonArray mentions;
    for (const QString& mention : msg.mentions)
        mentions.append(mention);
    obj.insert(QStringLiteral("mentions"), mentions);

    QJsonObject content;
    content.insert(QStringLiteral("type"), messageTypeToString(msg.content.type));
    content.insert(QStringLiteral("text"), msg.content.text);
    content.insert(QStringLiteral("payload"), msg.content.payload);
    obj.insert(QStringLiteral("content"), content);

    obj.insert(QStringLiteral("timestamp"), msg.timestamp.toString(Qt::ISODateWithMs));
    obj.insert(QStringLiteral("status"), messageStatusToString(msg.status));
    return obj;
}

Message messageFromJson(const QJsonObject& obj, const QString& fallbackSessionId)
{
    Message msg;
    msg.id = obj.value(QStringLiteral("id")).toString().trimmed();
    if (msg.id.isEmpty())
        msg.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    msg.sessionId = obj.value(QStringLiteral("sessionId")).toString().trimmed();
    if (msg.sessionId.isEmpty())
        msg.sessionId = fallbackSessionId;

    msg.senderId = obj.value(QStringLiteral("senderId")).toString().trimmed();
    QJsonArray mentions = obj.value(QStringLiteral("mentions")).toArray();
    for (const QJsonValue& v : mentions) {
        const QString mentionId = v.toString().trimmed();
        if (!mentionId.isEmpty())
            msg.mentions.append(mentionId);
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    msg.content.type = messageTypeFromString(contentObj.value(QStringLiteral("type")).toString().trimmed());
    msg.content.text = contentObj.value(QStringLiteral("text")).toString();
    msg.content.payload = contentObj.value(QStringLiteral("payload")).toObject();

    msg.timestamp = QDateTime::fromString(
        obj.value(QStringLiteral("timestamp")).toString().trimmed(),
        Qt::ISODateWithMs);
    if (!msg.timestamp.isValid())
        msg.timestamp = QDateTime::currentDateTime();

    msg.status = messageStatusFromString(obj.value(QStringLiteral("status")).toString().trimmed());
    return msg;
}

QJsonArray stringListToJson(const QStringList& values)
{
    QJsonArray arr;
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty())
            arr.append(trimmed);
    }
    return arr;
}

QStringList stringListFromJson(const QJsonValue& value)
{
    QStringList result;
    const QJsonArray arr = value.toArray();
    for (const QJsonValue& item : arr) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            result.append(text);
    }
    result.removeDuplicates();
    return result;
}

QJsonObject identityProfileToJson(const IdentityProfile* profile)
{
    QJsonObject obj;
    if (!profile)
        return obj;

    obj.insert(QStringLiteral("description"), profile->description());
    obj.insert(QStringLiteral("systemPrompt"), profile->systemPrompt());
    obj.insert(QStringLiteral("allowedTools"), stringListToJson(profile->allowedTools()));
    obj.insert(QStringLiteral("recursionDepth"), profile->recursionDepth());

    const LLMConfig cfg = profile->llmConfig();
    obj.insert(QStringLiteral("modelId"),
               ModelFactory::resolveModelKey(cfg.model, cfg.customModelId));
    return obj;
}

IdentityProfile* identityProfileFromJson(const QJsonObject& obj,
                                         const LLMConfig& fallbackConfig)
{
    auto* profile = new IdentityProfile();

    LLMConfig cfg = fallbackConfig;
    const QString modelId = obj.value(QStringLiteral("modelId")).toString().trimmed();
    if (!modelId.isEmpty()) {
        const ModelFactory::ParsedModelId parsed = ModelFactory::parseModelKey(modelId);
        if (parsed.model != ModelId::Unknown) {
            cfg.model = parsed.model;
            cfg.customModelId = parsed.customModelId;
        }
    }

    QString systemPrompt = obj.value(QStringLiteral("systemPrompt")).toString().trimmed();
    if (systemPrompt.isEmpty())
        systemPrompt = fallbackConfig.systemPrompt;
    cfg.systemPrompt = systemPrompt;

    profile->setLlmConfig(cfg);
    if (!systemPrompt.isEmpty())
        profile->setSystemPrompt(systemPrompt);

    profile->setDescription(obj.value(QStringLiteral("description")).toString().trimmed());
    profile->setAllowedTools(stringListFromJson(obj.value(QStringLiteral("allowedTools"))));
    profile->setRecursionDepth(obj.value(QStringLiteral("recursionDepth")).toInt(3));
    return profile;
}

QString remapIdentityId(const QString& oldId,
                        const QHash<QString, QString>& identityIdMap)
{
    const QString trimmed = oldId.trimmed();
    if (trimmed.isEmpty())
        return QString();
    return identityIdMap.value(trimmed, trimmed);
}

} // namespace

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

    SessionPipeline& pipeline = ensurePipeline(sessionId);
    const bool hasActiveTurn = pipeline.hasActiveTurn;
    if (hasActiveTurn) {
        const QString agentId = agentIdentityIdForSession(sessionId);
        if (AgentRuntime* runtime = runtimeForSession(sessionId))
            runtime->abort();

        const TurnTask cancelled = pipeline.activeTurn;
        pipeline.activeTurn = TurnTask();
        pipeline.hasActiveTurn = false;
        if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
            m_agentActiveSession.remove(agentId);
        resetSessionStreamState(sessionId);

        QJsonObject extra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("interrupted_by_new_message"));
        extra.insert(QStringLiteral("interruptedByIdentityId"), actorIdentityId);
        emitPipelineEvent(QStringLiteral("turn_cancelled"), sessionId, &cancelled,
                          QString(), QString(), extra);
    }

    // Message 成为会话主干数据：用户消息先写入 Session，再进入执行流水线。
    m_sessionManager->postMessage(sessionId, Message::createText(sessionId, actorIdentityId, prompt));

    TurnTask turn;
    turn.turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    turn.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    turn.clientMessageId = clientMessageId.trimmed();
    turn.userContent = prompt;
    if (hasActiveTurn)
        pipeline.queue.prepend(turn);
    else
        pipeline.queue.append(turn);

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
    if (!pipeline || !pipeline->hasActiveTurn) {
        resetSessionStreamState(sessionId);
        return;
    }

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime)
        runtime->abort();

    const TurnTask cancelled = pipeline->activeTurn;
    pipeline->activeTurn = TurnTask();
    pipeline->hasActiveTurn = false;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);
    emitPipelineEvent(QStringLiteral("turn_cancelled"), sessionId, &cancelled);
    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
}

QString ChatService::abortAndRollback(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !pipeline->hasActiveTurn) {
        resetSessionStreamState(sessionId);
        return QString();
    }

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    QString rolledBack;
    if (runtime)
        rolledBack = runtime->abortAndRollback();

    const TurnTask cancelled = pipeline->activeTurn;
    pipeline->activeTurn = TurnTask();
    pipeline->hasActiveTurn = false;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);

    if (rolledBack.isEmpty())
        rolledBack = cancelled.userContent;

    QJsonObject extra;
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

    m_pipelines.remove(sessionId);

    m_sessionManager->removeSession(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
    if (!agentId.isEmpty())
        releaseRuntimeIfUnused(agentId);

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
        runtime->setHistory(buildRuntimeHistoryFromMessages(session));
        runtime->setIoHistory(session->ioHistory());
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
    if (const SessionPipeline* pipeline = findPipeline(sessionId))
        return pipeline->hasActiveTurn;

    Session* session = m_sessionManager->findById(sessionId);
    return session && session->isStreaming();
}

int ChatService::pendingTurnCount(const QString& sessionId) const
{
    const SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline)
        return 0;
    return pipeline->queue.size();
}

QString ChatService::activeRunId(const QString& sessionId) const
{
    const SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !pipeline->hasActiveTurn)
        return QString();
    return pipeline->activeTurn.runId;
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

QString ChatService::sessionsFilePath() const
{
    QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/resources");
    return dir + QStringLiteral("/chat_sessions.json");
}

void ChatService::saveSessionsToDisk()
{
    const QString path = sessionsFilePath();

    // 保留其他模块写入的根字段（如 tabState）
    QJsonObject previousRoot;
    QFile in(path);
    if (in.open(QFile::ReadOnly | QFile::Text)) {
        QJsonDocument doc = QJsonDocument::fromJson(in.readAll());
        if (doc.isObject())
            previousRoot = doc.object();
        in.close();
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 2);
    root.insert(QStringLiteral("currentSessionId"), m_currentSessionId);
    if (previousRoot.contains(QStringLiteral("tabState")))
        root.insert(QStringLiteral("tabState"), previousRoot.value(QStringLiteral("tabState")));

    // Identity 主模型持久化
    QJsonArray identitiesArr;
    if (m_identityManager) {
        m_identityManager->userIdentity(); // 确保用户 Identity 存在
        const QList<Identity*> identities = m_identityManager->allIdentities();
        for (Identity* identity : identities) {
            if (!identity)
                continue;
            QJsonObject item;
            item.insert(QStringLiteral("id"), identity->id());
            item.insert(QStringLiteral("type"),
                        identity->isUser() ? QStringLiteral("user")
                                           : QStringLiteral("agent"));
            item.insert(QStringLiteral("name"), identity->name());
            item.insert(QStringLiteral("avatar"), identity->avatar());
            if (identity->isAgent())
                item.insert(QStringLiteral("profile"), identityProfileToJson(identity->profile()));
            identitiesArr.append(item);
        }
    }
    root.insert(QStringLiteral("identities"), identitiesArr);

    // Session 主模型持久化
    QJsonArray sessionsArr;
    const QList<Session*> sessions = m_sessionManager->allSessions();
    for (Session* session : sessions) {
        if (!session)
            continue;

        QJsonObject s;
        s.insert(QStringLiteral("id"), session->id());
        s.insert(QStringLiteral("type"),
                 session->type() == Session::SessionType::Group
                     ? QStringLiteral("group")
                     : QStringLiteral("private"));
        s.insert(QStringLiteral("title"), session->title());
        s.insert(QStringLiteral("ownerId"), session->ownerId());
        s.insert(QStringLiteral("participants"), stringListToJson(session->participantIds()));
        s.insert(QStringLiteral("io_history"), session->ioHistory());

        QJsonArray messagesArr;
        const QList<Message> messages = session->allMessages();
        for (const Message& msg : messages)
            messagesArr.append(messageToJson(msg));
        s.insert(QStringLiteral("messages"), messagesArr);

        if (const SessionPipeline* pipeline = findPipeline(session->id())) {
            QJsonArray pendingTurns;
            if (pipeline->hasActiveTurn) {
                QJsonObject active;
                active.insert(QStringLiteral("state"), QStringLiteral("running"));
                active.insert(QStringLiteral("turnId"), pipeline->activeTurn.turnId);
                active.insert(QStringLiteral("runId"), pipeline->activeTurn.runId);
                active.insert(QStringLiteral("clientMessageId"), pipeline->activeTurn.clientMessageId);
                active.insert(QStringLiteral("user"), pipeline->activeTurn.userContent);
                pendingTurns.append(active);
            }
            for (const TurnTask& turn : pipeline->queue) {
                QJsonObject queued;
                queued.insert(QStringLiteral("state"), QStringLiteral("queued"));
                queued.insert(QStringLiteral("turnId"), turn.turnId);
                queued.insert(QStringLiteral("runId"), turn.runId);
                queued.insert(QStringLiteral("clientMessageId"), turn.clientMessageId);
                queued.insert(QStringLiteral("user"), turn.userContent);
                pendingTurns.append(queued);
            }
            if (!pendingTurns.isEmpty())
                s.insert(QStringLiteral("pending_turns"), pendingTurns);
        }

        sessionsArr.append(s);
    }
    root.insert(QStringLiteral("sessions"), sessionsArr);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    if (out.open(QFile::WriteOnly | QFile::Text))
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
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
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion != 2) {
        qWarning() << "[ChatService] 不支持的会话文件版本，已跳过加载。expected=2 actual="
                   << schemaVersion;
        return false;
    }

    if (!root.value(QStringLiteral("identities")).isArray()
        || !root.value(QStringLiteral("sessions")).isArray()) {
        qWarning() << "[ChatService] 会话文件结构无效（缺少 identities/sessions 数组），已跳过加载。";
        return false;
    }

    // 清理现有 Runtime
    for (AgentRuntime* runtime : m_runtimes)
        runtime->deleteLater();
    m_runtimes.clear();
    m_pipelines.clear();
    m_agentActiveSession.clear();

    // 清理现有 Session（SessionManager 会处理）
    for (Session* session : m_sessionManager->allSessions())
        m_sessionManager->removeSession(session->id());

    // 清理现有 Agent Identity（用户 Identity 保留）
    const QList<Identity*> existingAgents = m_identityManager->allAgents();
    for (Identity* agent : existingAgents) {
        if (agent)
            m_identityManager->removeAgent(agent->id());
    }

    Identity* userIdentity = m_identityManager->userIdentity();
    const QString userId = userIdentity ? userIdentity->id() : QString();

    const QJsonArray sessionsArr = root.value(QStringLiteral("sessions")).toArray();
    if (sessionsArr.isEmpty()) {
        m_currentSessionId.clear();
        return true;
    }

    const QJsonArray identitiesArr = root.value(QStringLiteral("identities")).toArray();

    // oldId -> newId（用于消息 sender / participant 重映射）
    QHash<QString, QString> identityIdMap;
    if (!userId.isEmpty())
        identityIdMap.insert(userId, userId);

    // 先恢复用户（单例）
    for (const QJsonValue& value : identitiesArr) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("type")).toString().trimmed() != QLatin1String("user"))
            continue;

        const QString oldUserId = item.value(QStringLiteral("id")).toString().trimmed();
        if (!oldUserId.isEmpty())
            identityIdMap.insert(oldUserId, userId);

        const QString userName = item.value(QStringLiteral("name")).toString().trimmed();
        if (!userName.isEmpty())
            userIdentity->setName(userName);

        const QString userAvatar = item.value(QStringLiteral("avatar")).toString().trimmed();
        if (!userAvatar.isEmpty())
            userIdentity->setAvatar(userAvatar);
        break;
    }

    // 再恢复 Agent Identity
    for (const QJsonValue& value : identitiesArr) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("type")).toString().trimmed() != QLatin1String("agent"))
            continue;

        const QString oldAgentId = item.value(QStringLiteral("id")).toString().trimmed();
        const QString agentName = item.value(QStringLiteral("name")).toString().trimmed();
        const QString avatar = item.value(QStringLiteral("avatar")).toString().trimmed();

        IdentityProfile* profile = identityProfileFromJson(
            item.value(QStringLiteral("profile")).toObject(),
            m_defaultAgentConfig);
        if (profile->allowedTools().isEmpty())
            profile->setAllowedTools(collectToolNames(m_toolDispatcher));

        Identity* agent = m_identityManager->createAgent(
            agentName.isEmpty() ? QStringLiteral("TM Agent") : agentName,
            profile);
        if (!avatar.isEmpty())
            agent->setAvatar(avatar);
        if (!oldAgentId.isEmpty())
            identityIdMap.insert(oldAgentId, agent->id());
    }

    for (const QJsonValue& value : sessionsArr) {
        const QJsonObject s = value.toObject();

        const QString sessionId = s.value(QStringLiteral("id")).toString().trimmed();
        if (sessionId.isEmpty()) {
            qWarning() << "[ChatService] 跳过无效会话项：缺少 id。";
            continue;
        }

        const QString type = s.value(QStringLiteral("type")).toString().trimmed();
        if (type != QLatin1String("private") && type != QLatin1String("group")) {
            qWarning() << "[ChatService] 跳过无效会话项：未知 type =" << type;
            continue;
        }
        const QString title = s.value(QStringLiteral("title")).toString();

        QString ownerId = remapIdentityId(s.value(QStringLiteral("ownerId")).toString(), identityIdMap);
        QStringList participants = stringListFromJson(s.value(QStringLiteral("participants")));
        for (QString& pid : participants)
            pid = remapIdentityId(pid, identityIdMap);
        participants.removeAll(QString());
        participants.removeDuplicates();

        Session* session = nullptr;
        if (type == QLatin1String("group")) {
            if (ownerId.isEmpty() && !participants.isEmpty())
                ownerId = participants.first();
            if (ownerId.isEmpty())
                ownerId = userId;
            if (!participants.contains(ownerId))
                participants.prepend(ownerId);
            session = m_sessionManager->createGroupSession(ownerId, participants, title);
        } else {
            QString pA;
            QString pB;
            if (!ownerId.isEmpty() && participants.contains(ownerId)) {
                pA = ownerId;
                for (const QString& pid : participants) {
                    if (pid != ownerId) {
                        pB = pid;
                        break;
                    }
                }
            } else {
                pA = participants.value(0);
                pB = participants.value(1);
            }

            if (pA.isEmpty())
                pA = userId;
            if (pB.isEmpty()) {
                auto* profile = new IdentityProfile();
                profile->setLlmConfig(m_defaultAgentConfig);
                profile->setSystemPrompt(m_defaultAgentConfig.systemPrompt);
                profile->setAllowedTools(collectToolNames(m_toolDispatcher));
                Identity* agent = m_identityManager->createAgent(
                    title.isEmpty() ? QStringLiteral("TM Agent") : title,
                    profile);
                pB = agent->id();
            }

            session = m_sessionManager->createPrivateSession(pA, pB);
            session->setTitle(title);
        }

        if (!session)
            continue;

        const QString generatedId = session->id();
        session->setId(sessionId);
        m_sessionManager->replaceSessionId(generatedId, sessionId);
        if (!title.isEmpty())
            session->setTitle(title);

        session->setIoHistory(s.value(QStringLiteral("io_history")).toArray());

        const QJsonArray messagesArr = s.value(QStringLiteral("messages")).toArray();
        for (const QJsonValue& mv : messagesArr) {
            Message msg = messageFromJson(mv.toObject(), session->id());
            msg.sessionId = session->id();
            msg.senderId = remapIdentityId(msg.senderId, identityIdMap);
            for (QString& mention : msg.mentions)
                mention = remapIdentityId(mention, identityIdMap);
            msg.mentions.removeAll(QString());
            msg.mentions.removeDuplicates();
            if (msg.isValid())
                session->addMessage(msg);
        }

        // 恢复未完成 turn（running 会回退为 queued 重新执行）
        const QJsonArray pendingTurns = s.value(QStringLiteral("pending_turns")).toArray();
        if (!pendingTurns.isEmpty()) {
            SessionPipeline& pipeline = ensurePipeline(session->id());
            for (const QJsonValue& item : pendingTurns) {
                const QJsonObject turnObj = item.toObject();
                const QString state = turnObj.value(QStringLiteral("state")).toString().trimmed();
                if (state == QLatin1String("running"))
                    continue; // 运行中 turn 视为中断，不自动重放

                const QString userContent = turnObj.value(QStringLiteral("user")).toString().trimmed();
                if (userContent.isEmpty())
                    continue;

                TurnTask turn;
                turn.turnId = turnObj.value(QStringLiteral("turnId")).toString().trimmed();
                if (turn.turnId.isEmpty())
                    turn.turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                turn.runId = turnObj.value(QStringLiteral("runId")).toString().trimmed();
                if (turn.runId.isEmpty())
                    turn.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                turn.clientMessageId = turnObj.value(QStringLiteral("clientMessageId")).toString().trimmed();
                turn.userContent = userContent;
                pipeline.queue.append(turn);
            }
            tryStartNextTurn(session->id());
        }
    }

    QString savedSessionId = root.value(QStringLiteral("currentSessionId")).toString().trimmed();
    if (!savedSessionId.isEmpty() && m_sessionManager->findById(savedSessionId)) {
        m_currentSessionId = savedSessionId;
    } else {
        const QList<Session*> sessions = m_sessionManager->allSessions();
        m_currentSessionId = sessions.isEmpty() ? QString() : sessions.first()->id();
    }

    return true;
}

ChatService::SessionPipeline& ChatService::ensurePipeline(const QString& sessionId)
{
    return m_pipelines[sessionId];
}

ChatService::SessionPipeline* ChatService::findPipeline(const QString& sessionId)
{
    auto it = m_pipelines.find(sessionId);
    if (it == m_pipelines.end())
        return nullptr;
    return &it.value();
}

const ChatService::SessionPipeline* ChatService::findPipeline(const QString& sessionId) const
{
    auto it = m_pipelines.constFind(sessionId);
    if (it == m_pipelines.constEnd())
        return nullptr;
    return &it.value();
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

    const QList<Message> messages = session->allMessages();
    for (const Message& msg : messages) {
        const QString content = msg.content.text;
        if (content.trimmed().isEmpty())
            continue;

        QJsonObject item;
        if (msg.content.type == MessageContent::Type::System
            || msg.senderId == QLatin1String("system")) {
            item.insert(QStringLiteral("role"), QStringLiteral("system"));
            item.insert(QStringLiteral("content"), content);
            history.append(item);
            continue;
        }

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

    const QStringList sessionIds = m_pipelines.keys();
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

void ChatService::emitPipelineEvent(const QString& type,
                                    const QString& sessionId,
                                    const TurnTask* turn,
                                    const QString& delta,
                                    const QString& error,
                                    const QJsonObject& extra)
{
    SessionPipeline* pipeline = findPipeline(sessionId);

    QJsonObject event;
    event.insert(QStringLiteral("type"), type);
    event.insert(QStringLiteral("sessionId"), sessionId);
    event.insert(QStringLiteral("timestamp"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (pipeline) {
        event.insert(QStringLiteral("seq"), static_cast<qint64>(++pipeline->seq));
        event.insert(QStringLiteral("queueDepth"), pipeline->queue.size());
        event.insert(QStringLiteral("hasActiveRun"), pipeline->hasActiveTurn);
    }

    if (turn) {
        event.insert(QStringLiteral("turnId"), turn->turnId);
        event.insert(QStringLiteral("runId"), turn->runId);
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
}

void ChatService::tryStartNextTurn(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || pipeline->hasActiveTurn || pipeline->queue.isEmpty())
        return;

    AgentRuntime* runtime = ensureRuntimeForSession(sessionId);
    if (!runtime)
        return;
    const QString agentId = runtime->identityId().trimmed();
    if (agentId.isEmpty())
        return;

    const QString activeSessionId = m_agentActiveSession.value(agentId);
    if (!activeSessionId.isEmpty() && activeSessionId != sessionId)
        return; // 同一 Agent 的 Runtime 正在处理另一个会话

    pipeline->activeTurn = pipeline->queue.takeFirst();
    pipeline->hasActiveTurn = true;
    m_agentActiveSession.insert(agentId, sessionId);

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
        state.isStreaming = true;
    }

    emitPipelineEvent(QStringLiteral("turn_started"), sessionId, &pipeline->activeTurn);
    runtime->sendMessage(sessionId, pipeline->activeTurn.userContent);
}

void ChatService::onRuntimeStreamData(const QString& sessionId, const QString& data)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !pipeline->hasActiveTurn)
        return;

    pipeline->activeTurn.assistantContent.append(data);

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.append(data);
        state.isStreaming = true;
    }

    emit streamDataReceived(sessionId, data);
    emitPipelineEvent(QStringLiteral("turn_delta"), sessionId, &pipeline->activeTurn, data);
}

void ChatService::onRuntimeFinished(const QString& sessionId, const QString& fullContent)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !pipeline->hasActiveTurn)
        return;

    TurnTask finishedTurn = pipeline->activeTurn;
    if (!fullContent.isEmpty())
        finishedTurn.assistantContent = fullContent;

    const QString agentId = agentIdentityIdForSession(sessionId);
    pipeline->activeTurn = TurnTask();
    pipeline->hasActiveTurn = false;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);

    if (!finishedTurn.assistantContent.trimmed().isEmpty() && !agentId.isEmpty()) {
        m_sessionManager->postMessage(
            sessionId,
            Message::createText(sessionId, agentId, finishedTurn.assistantContent));
    }

    QJsonObject extra;
    extra.insert(QStringLiteral("fullContent"), finishedTurn.assistantContent);
    emit finished(sessionId, finishedTurn.assistantContent);
    emitPipelineEvent(QStringLiteral("turn_completed"), sessionId, &finishedTurn,
                      QString(), QString(), extra);

    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
}

void ChatService::onRuntimeError(const QString& sessionId, const QString& errorMsg)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !pipeline->hasActiveTurn)
        return;

    const QString agentId = agentIdentityIdForSession(sessionId);
    TurnTask failedTurn = pipeline->activeTurn;
    pipeline->activeTurn = TurnTask();
    pipeline->hasActiveTurn = false;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);

    emit errorOccurred(sessionId, errorMsg);
    emitPipelineEvent(QStringLiteral("turn_failed"), sessionId, &failedTurn,
                      QString(), errorMsg);

    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
}

void ChatService::onRuntimeToolCallsStarted(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !pipeline->hasActiveTurn)
        return;

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.clear();
        state.lastMsgIsTool = true;
    }

    emit toolCallsStarted(sessionId);
    emitPipelineEvent(QStringLiteral("turn_tool_calls_started"), sessionId, &pipeline->activeTurn);
}

void ChatService::onRuntimeToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !pipeline->hasActiveTurn)
        return;

    emit toolEvent(sessionId, event);
    QJsonObject extra;
    extra.insert(QStringLiteral("toolEvent"), toolEventToJson(event));
    emitPipelineEvent(QStringLiteral("turn_tool_event"), sessionId, &pipeline->activeTurn,
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
    // 读取现有文件，追加 tabState 字段
    QFile f(sessionsFilePath());
    QJsonObject root;
    if (f.open(QFile::ReadOnly | QFile::Text)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject())
            root = doc.object();
        f.close();
    }

    QJsonObject tabState;
    QJsonArray agentIds;
    for (const QString& id : openAgentIds)
        agentIds.append(id);
    tabState.insert(QStringLiteral("openAgentIds"), agentIds);
    tabState.insert(QStringLiteral("activeIdentityId"), activeIdentityId);
    root.insert(QStringLiteral("tabState"), tabState);

    QDir().mkpath(QFileInfo(sessionsFilePath()).absolutePath());
    QFile out(sessionsFilePath());
    if (out.open(QFile::WriteOnly | QFile::Text))
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

ChatService::TabState ChatService::loadTabState() const
{
    TabState state;
    QFile f(sessionsFilePath());
    if (!f.open(QFile::ReadOnly | QFile::Text))
        return state;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return state;

    QJsonObject tabObj = doc.object().value(QStringLiteral("tabState")).toObject();
    QJsonArray arr = tabObj.value(QStringLiteral("openAgentIds")).toArray();
    for (const QJsonValue& v : arr)
        state.openAgentIds.append(v.toString());
    state.activeIdentityId = tabObj.value(QStringLiteral("activeIdentityId")).toString();
    return state;
}
