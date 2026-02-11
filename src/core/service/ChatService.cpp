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
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
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

bool envFlagEnabled(const char* key)
{
    const QString raw =
        QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(key)).trimmed().toLower();
    return raw == QLatin1String("1")
           || raw == QLatin1String("true")
           || raw == QLatin1String("yes")
           || raw == QLatin1String("on");
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
    case Message::Status::Streaming: return QStringLiteral("streaming");
    case Message::Status::Completed: return QStringLiteral("completed");
    case Message::Status::Cancelled: return QStringLiteral("cancelled");
    case Message::Status::Interrupted: return QStringLiteral("interrupted");
    case Message::Status::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

Message::Status messageStatusFromString(const QString& status)
{
    if (status == QLatin1String("pending")) return Message::Status::Pending;
    if (status == QLatin1String("streaming")) return Message::Status::Streaming;
    if (status == QLatin1String("completed")) return Message::Status::Completed;
    if (status == QLatin1String("cancelled")) return Message::Status::Cancelled;
    if (status == QLatin1String("interrupted")) return Message::Status::Interrupted;
    if (status == QLatin1String("error")) return Message::Status::Error;
    return Message::Status::Completed;
}

QJsonObject messageToJson(const Message& msg)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), msg.id);
    obj.insert(QStringLiteral("sessionId"), msg.sessionId);
    if (!msg.traceId.isEmpty())
        obj.insert(QStringLiteral("traceId"), msg.traceId);
    if (!msg.turnId.isEmpty())
        obj.insert(QStringLiteral("turnId"), msg.turnId);
    if (msg.seq > 0)
        obj.insert(QStringLiteral("seq"), static_cast<qint64>(msg.seq));
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
    msg.traceId = obj.value(QStringLiteral("traceId")).toString().trimmed();
    msg.turnId = obj.value(QStringLiteral("turnId")).toString().trimmed();
    msg.seq = static_cast<qint64>(obj.value(QStringLiteral("seq")).toDouble(0));

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

bool ensureParentDir(const QString& filePath)
{
    return QDir().mkpath(QFileInfo(filePath).absolutePath());
}

bool writeJsonDocumentFile(const QString& filePath, const QJsonDocument& doc)
{
    if (!ensureParentDir(filePath))
        return false;

    QSaveFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;

    if (file.write(doc.toJson(QJsonDocument::Indented)) < 0)
        return false;
    return file.commit();
}

bool writeJsonObjectFile(const QString& filePath, const QJsonObject& obj)
{
    return writeJsonDocumentFile(filePath, QJsonDocument(obj));
}

bool writeJsonArrayFile(const QString& filePath, const QJsonArray& arr)
{
    return writeJsonDocumentFile(filePath, QJsonDocument(arr));
}

bool writeJsonLinesFile(const QString& filePath, const QJsonArray& lines)
{
    if (!ensureParentDir(filePath))
        return false;

    QSaveFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;

    for (const QJsonValue& value : lines) {
        if (!value.isObject())
            continue;
        const QByteArray line = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
        if (file.write(line) < 0)
            return false;
        if (file.write("\n") < 0)
            return false;
    }
    return file.commit();
}

bool appendJsonLineFile(const QString& filePath, const QJsonObject& line)
{
    if (!ensureParentDir(filePath))
        return false;

    QFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Append | QFile::Text))
        return false;

    const QByteArray payload = QJsonDocument(line).toJson(QJsonDocument::Compact);
    if (file.write(payload) < 0)
        return false;
    if (file.write("\n") < 0)
        return false;
    file.flush();
    return true;
}

QJsonObject readJsonObjectFile(const QString& filePath, bool* ok = nullptr)
{
    if (ok)
        *ok = false;
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QJsonObject();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    if (ok)
        *ok = true;
    return doc.object();
}

QJsonArray readJsonArrayFile(const QString& filePath, bool* ok = nullptr)
{
    if (ok)
        *ok = false;
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QJsonArray();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return QJsonArray();
    if (ok)
        *ok = true;
    return doc.array();
}

QJsonArray readJsonLinesFile(const QString& filePath, bool* ok = nullptr)
{
    if (ok)
        *ok = false;

    QJsonArray result;
    QFile file(filePath);
    if (!file.exists()) {
        if (ok)
            *ok = true;
        return result;
    }
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return result;

    const QByteArray raw = file.readAll();
    file.close();
    const QList<QByteArray> lines = raw.split('\n');

    bool parsedAny = false;
    bool hasNonEmptyLine = false;
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        hasNonEmptyLine = true;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "[ChatService] 跳过无效 JSONL 行" << filePath << "error="
                       << err.errorString();
            continue;
        }
        result.append(doc.object());
        parsedAny = true;
    }

    if (ok)
        *ok = !hasNonEmptyLine || parsedAny;
    return result;
}

} // namespace

ChatService::ChatService(QObject* parent)
    : QObject(parent)
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

    SessionPipeline& pipeline = ensurePipeline(sessionId);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    TurnTask* mergeTarget = nullptr;
    if (!pipeline.queue.isEmpty()) {
        TurnTask& tail = pipeline.queue.last();
        const QString tailActorId = tail.actorIdentityId.trimmed();
        const int tailMergedCount = qMax(1, tail.mergedMessageCount);
        const bool sameActor = !tailActorId.isEmpty() && tailActorId == actorId;
        const bool withinWindow =
            tail.enqueuedAtMs > 0 &&
            nowMs >= tail.enqueuedAtMs &&
            (nowMs - tail.enqueuedAtMs) <= kQueueMergeWindowMs;
        const bool withinMergeCount = tailMergedCount < kQueueMergeMaxMergedMessages;
        const bool withinMergedSize =
            (tail.userContent.size() + prompt.size() + 32) <= kQueueMergeMaxChars;
        if (sameActor && withinWindow && withinMergeCount && withinMergedSize)
            mergeTarget = &tail;
    }

    const int queueDepthBeforeEnqueue = pipeline.queue.size() + (pipeline.hasActiveTurn ? 1 : 0);
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

    flushPendingDeltaLog(sessionId, pipeline, &pipeline->activeTurn, true);

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
    if (!pipeline || !pipeline->hasActiveTurn) {
        resetSessionStreamState(sessionId);
        return QString();
    }

    flushPendingDeltaLog(sessionId, pipeline, &pipeline->activeTurn, true);

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

    return writeJsonObjectFile(mcpConfigPath(), root);
}

QString ChatService::mcpConfigPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("mcp_servers.json"));
}

QString ChatService::modelConfigPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("models.yaml"));
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
    // 兼容旧接口命名：当前返回 app_state 路径（会话与 tab 状态）
    return appStatePath();
}

QString ChatService::dataRootPath() const
{
    return QDir::home().filePath(QStringLiteral(".tmagent"));
}

QString ChatService::configDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("config"));
}

QString ChatService::appStatePath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("app_state.json"));
}

QString ChatService::manifestPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("manifest.json"));
}

QString ChatService::identitiesDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("identities"));
}

QString ChatService::agentsDirPath() const
{
    return QDir(identitiesDirPath()).filePath(QStringLiteral("agents"));
}

QString ChatService::userIdentityPath() const
{
    return QDir(identitiesDirPath()).filePath(QStringLiteral("user.json"));
}

QString ChatService::agentProfilePath(const QString& agentId) const
{
    return QDir(QDir(agentsDirPath()).filePath(agentId)).filePath(QStringLiteral("profile.json"));
}

QString ChatService::sessionsDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("sessions"));
}

QString ChatService::sessionsIndexPath() const
{
    return QDir(sessionsDirPath()).filePath(QStringLiteral("index.json"));
}

QString ChatService::logsDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("logs"));
}

QString ChatService::eventsCurrentLogPath() const
{
    return QDir(logsDirPath()).filePath(QStringLiteral("events-current.jsonl"));
}

QString ChatService::sessionDataDirPath(const QString& sessionId) const
{
    return QDir(QDir(sessionsDirPath()).filePath(QStringLiteral("data"))).filePath(sessionId);
}

QString ChatService::sessionMetaPath(const QString& sessionId) const
{
    return QDir(sessionDataDirPath(sessionId)).filePath(QStringLiteral("meta.json"));
}

QString ChatService::sessionMessagesPath(const QString& sessionId) const
{
    return QDir(sessionDataDirPath(sessionId)).filePath(QStringLiteral("messages.jsonl"));
}

QString ChatService::sessionPendingTurnsPath(const QString& sessionId) const
{
    return QDir(sessionDataDirPath(sessionId)).filePath(QStringLiteral("pending_turns.json"));
}

void ChatService::rotateEventLogIfNeeded() const
{
    static const qint64 kMaxEventLogBytes = 50LL * 1024 * 1024;
    static const int kMaxRetentionDays = 14;
    static const qint64 kCheckIntervalMs = 30000;
    static qint64 s_lastCheckMs = 0;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (s_lastCheckMs > 0 && (nowMs - s_lastCheckMs) < kCheckIntervalMs)
        return;
    s_lastCheckMs = nowMs;

    const QString currentPath = eventsCurrentLogPath();
    QFileInfo info(currentPath);
    if (info.exists() && info.size() >= kMaxEventLogBytes) {
        const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
        const QString archivedPath =
            QDir(logsDirPath()).filePath(QStringLiteral("events-%1.jsonl").arg(stamp));
        QFile::rename(currentPath, archivedPath);
    }

    QDir dir(logsDirPath());
    const QFileInfoList files =
        dir.entryInfoList(QStringList() << QStringLiteral("events-*.jsonl"), QDir::Files, QDir::Time);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    for (const QFileInfo& file : files) {
        const qint64 ageDays = file.lastModified().toUTC().daysTo(nowUtc);
        if (ageDays > kMaxRetentionDays)
            QFile::remove(file.absoluteFilePath());
    }
}

void ChatService::appendSessionMessageToDisk(const QString& sessionId, const Message& msg)
{
    if (sessionId.trimmed().isEmpty() || !msg.isValid())
        return;
    if (!appendJsonLineFile(sessionMessagesPath(sessionId), messageToJson(msg))) {
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
    const QString nowIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    QDir().mkpath(dataRootPath());
    QDir().mkpath(configDirPath());
    QDir().mkpath(identitiesDirPath());
    QDir().mkpath(agentsDirPath());
    QDir().mkpath(logsDirPath());
    QDir().mkpath(QDir(sessionsDirPath()).filePath(QStringLiteral("data")));

    {
        bool manifestOk = false;
        QJsonObject manifest = readJsonObjectFile(manifestPath(), &manifestOk);
        if (!manifestOk)
            manifest = QJsonObject();
        manifest.insert(QStringLiteral("schemaVersion"), 3);
        if (!manifest.contains(QStringLiteral("createdAt")))
            manifest.insert(QStringLiteral("createdAt"), nowIso);
        manifest.insert(QStringLiteral("lastWrittenAt"), nowIso);
        writeJsonObjectFile(manifestPath(), manifest);
    }

    {
        bool appStateOk = false;
        QJsonObject appState = readJsonObjectFile(appStatePath(), &appStateOk);
        if (!appStateOk)
            appState = QJsonObject();
        appState.insert(QStringLiteral("schemaVersion"), 3);
        appState.insert(QStringLiteral("currentSessionId"), m_currentSessionId);
        writeJsonObjectFile(appStatePath(), appState);
    }

    QStringList activeAgentIds;
    if (m_identityManager) {
        Identity* user = m_identityManager->userIdentity();
        if (user) {
            QJsonObject userObj;
            userObj.insert(QStringLiteral("id"), user->id());
            userObj.insert(QStringLiteral("type"), QStringLiteral("user"));
            userObj.insert(QStringLiteral("name"), user->name());
            userObj.insert(QStringLiteral("avatar"), user->avatar());
            writeJsonObjectFile(userIdentityPath(), userObj);
        }

        QDir().mkpath(agentsDirPath());

        const QList<Identity*> identities = m_identityManager->allIdentities();
        for (Identity* identity : identities) {
            if (!identity || !identity->isAgent())
                continue;
            activeAgentIds.append(identity->id());

            QJsonObject profileObj;
            profileObj.insert(QStringLiteral("id"), identity->id());
            profileObj.insert(QStringLiteral("type"), QStringLiteral("agent"));
            profileObj.insert(QStringLiteral("name"), identity->name());
            profileObj.insert(QStringLiteral("avatar"), identity->avatar());
            profileObj.insert(QStringLiteral("profile"), identityProfileToJson(identity->profile()));
            writeJsonObjectFile(agentProfilePath(identity->id()), profileObj);
        }

        activeAgentIds.removeDuplicates();
        QDir agentsDir(agentsDirPath());
        const QStringList persistedAgentDirs = agentsDir.entryList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& dirName : persistedAgentDirs) {
            if (activeAgentIds.contains(dirName))
                continue;
            QDir(agentsDir.filePath(dirName)).removeRecursively();
        }
    }

    const QString sessionDataRoot = QDir(sessionsDirPath()).filePath(QStringLiteral("data"));
    QDir().mkpath(sessionDataRoot);

    QStringList activeSessionIds;
    QJsonArray indexSessions;
    const QList<Session*> sessions = m_sessionManager->allSessions();
    for (Session* session : sessions) {
        if (!session)
            continue;
        activeSessionIds.append(session->id());

        QJsonObject metaObj;
        metaObj.insert(QStringLiteral("id"), session->id());
        metaObj.insert(QStringLiteral("type"),
                       session->type() == Session::SessionType::Group
                           ? QStringLiteral("group")
                           : QStringLiteral("private"));
        metaObj.insert(QStringLiteral("title"), session->title());
        metaObj.insert(QStringLiteral("ownerId"), session->ownerId());
        metaObj.insert(QStringLiteral("participants"), stringListToJson(session->participantIds()));
        metaObj.insert(QStringLiteral("createdAt"), session->createdAt().toString(Qt::ISODateWithMs));
        metaObj.insert(QStringLiteral("lastActiveAt"), session->lastActiveAt().toString(Qt::ISODateWithMs));
        metaObj.insert(QStringLiteral("messageCount"), session->messageCount());
        writeJsonObjectFile(sessionMetaPath(session->id()), metaObj);

        const QString sid = session->id();
        const QString messagesPath = sessionMessagesPath(sid);
        const int currentMessageCount = session->messageCount();
        const bool messageFileExists = QFileInfo::exists(messagesPath);
        const bool needsMessageRewrite =
            !messageFileExists || m_lastSavedMessageCounts.value(sid, -1) != currentMessageCount;
        if (needsMessageRewrite) {
            QJsonArray messagesArr;
            const QList<Message> messages = session->allMessages();
            for (const Message& msg : messages)
                messagesArr.append(messageToJson(msg));
            writeJsonLinesFile(messagesPath, messagesArr);
            m_lastSavedMessageCounts.insert(sid, currentMessageCount);
        }
        QFile::remove(QDir(sessionDataDirPath(sid)).filePath(QStringLiteral("io_history.json")));

        QJsonArray pendingTurns;
        if (const SessionPipeline* pipeline = findPipeline(session->id())) {
            if (pipeline->hasActiveTurn) {
                QJsonObject active;
                active.insert(QStringLiteral("state"), QStringLiteral("running"));
                active.insert(QStringLiteral("requestTraceId"), pipeline->activeTurn.requestTraceId);
                active.insert(QStringLiteral("turnId"), pipeline->activeTurn.turnId);
                active.insert(QStringLiteral("runId"), pipeline->activeTurn.runId);
                active.insert(QStringLiteral("actorIdentityId"), pipeline->activeTurn.actorIdentityId);
                active.insert(QStringLiteral("enqueuedAtMs"),
                              static_cast<double>(pipeline->activeTurn.enqueuedAtMs));
                active.insert(QStringLiteral("mergedMessageCount"),
                              qMax(1, pipeline->activeTurn.mergedMessageCount));
                active.insert(QStringLiteral("clientMessageId"), pipeline->activeTurn.clientMessageId);
                active.insert(QStringLiteral("user"), pipeline->activeTurn.userContent);
                pendingTurns.append(active);
            }
            for (const TurnTask& turn : pipeline->queue) {
                QJsonObject queued;
                queued.insert(QStringLiteral("state"), QStringLiteral("queued"));
                queued.insert(QStringLiteral("requestTraceId"), turn.requestTraceId);
                queued.insert(QStringLiteral("turnId"), turn.turnId);
                queued.insert(QStringLiteral("runId"), turn.runId);
                queued.insert(QStringLiteral("actorIdentityId"), turn.actorIdentityId);
                queued.insert(QStringLiteral("enqueuedAtMs"), static_cast<double>(turn.enqueuedAtMs));
                queued.insert(QStringLiteral("mergedMessageCount"), qMax(1, turn.mergedMessageCount));
                queued.insert(QStringLiteral("clientMessageId"), turn.clientMessageId);
                queued.insert(QStringLiteral("user"), turn.userContent);
                pendingTurns.append(queued);
            }
        }
        if (!pendingTurns.isEmpty()) {
            QJsonObject pendingObj;
            pendingObj.insert(QStringLiteral("turns"), pendingTurns);
            writeJsonObjectFile(sessionPendingTurnsPath(session->id()), pendingObj);
        } else {
            QFile::remove(sessionPendingTurnsPath(session->id()));
        }

        QJsonObject indexItem;
        indexItem.insert(QStringLiteral("id"), session->id());
        indexItem.insert(QStringLiteral("type"),
                         session->type() == Session::SessionType::Group
                             ? QStringLiteral("group")
                             : QStringLiteral("private"));
        indexItem.insert(QStringLiteral("title"), session->title());
        indexItem.insert(QStringLiteral("participants"), stringListToJson(session->participantIds()));
        indexItem.insert(QStringLiteral("lastActiveAt"), session->lastActiveAt().toString(Qt::ISODateWithMs));
        indexItem.insert(QStringLiteral("messageCount"), session->messageCount());
        indexSessions.append(indexItem);
    }

    activeSessionIds.removeDuplicates();
    QDir sessionDataDir(sessionDataRoot);
    const QStringList persistedSessionDirs = sessionDataDir.entryList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& dirName : persistedSessionDirs) {
        if (activeSessionIds.contains(dirName))
            continue;
        QDir(sessionDataDir.filePath(dirName)).removeRecursively();
    }
    for (auto it = m_lastSavedMessageCounts.begin(); it != m_lastSavedMessageCounts.end();) {
        if (activeSessionIds.contains(it.key()))
            ++it;
        else
            it = m_lastSavedMessageCounts.erase(it);
    }

    QJsonObject indexRoot;
    indexRoot.insert(QStringLiteral("version"), 1);
    indexRoot.insert(QStringLiteral("updatedAt"), nowIso);
    indexRoot.insert(QStringLiteral("sessions"), indexSessions);
    writeJsonObjectFile(sessionsIndexPath(), indexRoot);
}

bool ChatService::loadSessionsFromDisk()
{
    bool manifestOk = false;
    const QJsonObject manifest = readJsonObjectFile(manifestPath(), &manifestOk);
    if (!manifestOk)
        return false;

    const int schemaVersion = manifest.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion != 3) {
        qWarning() << "[ChatService] 不支持的数据目录版本，已跳过加载。expected=3 actual="
                   << schemaVersion;
        return false;
    }

    const QJsonObject appState = readJsonObjectFile(appStatePath());

    // 清理现有 Runtime
    for (AgentRuntime* runtime : m_runtimes)
        runtime->deleteLater();
    m_runtimes.clear();
    m_turnManager.clear();
    m_agentActiveSession.clear();
    m_lastSavedMessageCounts.clear();

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
    if (userIdentity) {
        // 避免 user.json 缺失时沿用旧内存状态，统一回到默认用户名。
        userIdentity->setName(QStringLiteral("Me"));
    }

    // oldId -> newId（Identity::id 不可写，加载后统一做 remap）
    QHash<QString, QString> identityIdMap;
    if (!userId.isEmpty())
        identityIdMap.insert(userId, userId);

    // 先恢复用户（单例）
    bool userOk = false;
    const QJsonObject userObj = readJsonObjectFile(userIdentityPath(), &userOk);
    if (userOk && userIdentity) {
        const QString oldUserId = userObj.value(QStringLiteral("id")).toString().trimmed();
        if (!oldUserId.isEmpty())
            identityIdMap.insert(oldUserId, userId);

        const QString userName = userObj.value(QStringLiteral("name")).toString().trimmed();
        if (!userName.isEmpty())
            userIdentity->setName(userName);

        const QString userAvatar = userObj.value(QStringLiteral("avatar")).toString().trimmed();
        if (!userAvatar.isEmpty())
            userIdentity->setAvatar(userAvatar);
    }

    // 再恢复 Agent Identity
    QDir agentsDir(agentsDirPath());
    const QStringList agentDirs = agentsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                      QDir::Name);
    for (const QString& agentDirName : agentDirs) {
        bool profileOk = false;
        const QJsonObject item = readJsonObjectFile(agentProfilePath(agentDirName), &profileOk);
        if (!profileOk)
            continue;
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
        if (!agentDirName.trimmed().isEmpty())
            identityIdMap.insert(agentDirName.trimmed(), agent->id());
    }

    bool sessionsIndexOk = false;
    const QJsonObject sessionsIndex = readJsonObjectFile(sessionsIndexPath(), &sessionsIndexOk);
    QJsonArray sessionsArr;
    if (sessionsIndexOk)
        sessionsArr = sessionsIndex.value(QStringLiteral("sessions")).toArray();

    for (const QJsonValue& value : sessionsArr) {
        const QJsonObject indexItem = value.toObject();

        const QString sessionId = indexItem.value(QStringLiteral("id")).toString().trimmed();
        if (sessionId.isEmpty()) {
            qWarning() << "[ChatService] 跳过无效会话项：缺少 id。";
            continue;
        }

        bool metaOk = false;
        const QJsonObject metaObj = readJsonObjectFile(sessionMetaPath(sessionId), &metaOk);
        const QJsonObject s = metaOk ? metaObj : indexItem;

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

        bool messagesOk = false;
        const QJsonArray messagesArr = readJsonLinesFile(sessionMessagesPath(sessionId), &messagesOk);
        if (!messagesOk)
            qWarning() << "[ChatService] 会话消息读取失败，sessionId=" << sessionId;
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
        m_lastSavedMessageCounts.insert(session->id(), session->messageCount());

        // 恢复未完成 turn（running 会回退为 queued 重新执行）
        bool pendingOk = false;
        const QJsonObject pendingObj = readJsonObjectFile(sessionPendingTurnsPath(sessionId), &pendingOk);
        const QJsonArray pendingTurns = pendingOk
            ? pendingObj.value(QStringLiteral("turns")).toArray()
            : QJsonArray();
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
                turn.requestTraceId = turnObj.value(QStringLiteral("requestTraceId")).toString().trimmed();
                if (turn.requestTraceId.isEmpty())
                    turn.requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                turn.turnId = turnObj.value(QStringLiteral("turnId")).toString().trimmed();
                if (turn.turnId.isEmpty())
                    turn.turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                turn.runId = turnObj.value(QStringLiteral("runId")).toString().trimmed();
                if (turn.runId.isEmpty())
                    turn.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                turn.actorIdentityId =
                    turnObj.value(QStringLiteral("actorIdentityId")).toString().trimmed();
                if (turn.actorIdentityId.isEmpty())
                    turn.actorIdentityId = userId;
                turn.enqueuedAtMs =
                    static_cast<qint64>(turnObj.value(QStringLiteral("enqueuedAtMs")).toDouble(0));
                turn.mergedMessageCount =
                    qMax(1, turnObj.value(QStringLiteral("mergedMessageCount")).toInt(1));
                turn.clientMessageId = turnObj.value(QStringLiteral("clientMessageId")).toString().trimmed();
                turn.userContent = userContent;
                pipeline.queue.append(turn);
            }
            tryStartNextTurn(session->id());
        }
    }

    QString savedSessionId = appState.value(QStringLiteral("currentSessionId")).toString().trimmed();
    if (!savedSessionId.isEmpty() && m_sessionManager->findById(savedSessionId)) {
        m_currentSessionId = savedSessionId;
    } else {
        const QList<Session*> sessions = m_sessionManager->allSessions();
        m_currentSessionId = sessions.isEmpty() ? QString() : sessions.first()->id();
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
                toolCallId = msg.id;

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
    rotateEventLogIfNeeded();
    return appendJsonLineFile(eventsCurrentLogPath(), event);
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
        qWarning() << "[ChatService] 事件日志写入失败：" << eventsCurrentLogPath();
    }
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
    if (pipeline->activeTurn.requestTraceId.isEmpty())
        pipeline->activeTurn.requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pipeline->hasActiveTurn = true;
    pipeline->pendingDeltaLog.clear();
    pipeline->pendingDeltaChunks = 0;
    pipeline->pendingDeltaStartedAtMs = 0;
    pipeline->lastDeltaFlushedAtMs = 0;
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
    emitPipelineEvent(QStringLiteral("turn_delta"),
                      sessionId,
                      &pipeline->activeTurn,
                      data,
                      QString(),
                      QJsonObject(),
                      m_logVerboseStreamEvents);

    if (!m_logVerboseStreamEvents && !data.isEmpty()) {
        if (pipeline->pendingDeltaLog.isEmpty())
            pipeline->pendingDeltaStartedAtMs = QDateTime::currentMSecsSinceEpoch();
        pipeline->pendingDeltaLog.append(data);
        ++pipeline->pendingDeltaChunks;
        flushPendingDeltaLog(sessionId, pipeline, &pipeline->activeTurn, false);
    }
}

void ChatService::onRuntimeFinished(const QString& sessionId, const QString& fullContent)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !pipeline->hasActiveTurn)
        return;

    flushPendingDeltaLog(sessionId, pipeline, &pipeline->activeTurn, true);

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

    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
}

void ChatService::onRuntimeError(const QString& sessionId, const QString& errorMsg)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !pipeline->hasActiveTurn)
        return;

    flushPendingDeltaLog(sessionId, pipeline, &pipeline->activeTurn, true);

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

    flushPendingDeltaLog(sessionId, pipeline, &pipeline->activeTurn, true);

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

    const QString agentId = agentIdentityIdForSession(sessionId);
    if (m_sessionManager && !agentId.isEmpty()) {
        if (event.status == QLatin1String("started")) {
            Message toolCallMsg = Message::createToolCall(sessionId, agentId, QString(), QJsonObject());
            toolCallMsg.content.text.clear(); // 不在聊天区展示，仅用于上下文重建
            toolCallMsg.traceId = pipeline->activeTurn.requestTraceId;
            toolCallMsg.turnId = pipeline->activeTurn.turnId;
            toolCallMsg.status = Message::Status::Completed;
            toolCallMsg.content.payload.insert(QStringLiteral("tool_name"), event.toolName);
            toolCallMsg.content.payload.insert(QStringLiteral("tool_call_id"), event.toolId);
            toolCallMsg.content.payload.insert(QStringLiteral("arguments"), event.data);
            m_sessionManager->postMessage(sessionId, toolCallMsg);
        } else if (event.status == QLatin1String("completed")) {
            Message toolResultMsg = Message::createToolResult(sessionId, agentId, event.toolId, QString());
            toolResultMsg.content.text.clear(); // 避免污染聊天气泡
            toolResultMsg.traceId = pipeline->activeTurn.requestTraceId;
            toolResultMsg.turnId = pipeline->activeTurn.turnId;
            toolResultMsg.status = Message::Status::Completed;
            toolResultMsg.content.payload.insert(QStringLiteral("tool_name"), event.toolName);
            toolResultMsg.content.payload.insert(QStringLiteral("success"), event.success);
            toolResultMsg.content.payload.insert(QStringLiteral("raw_result"), event.rawResult);
            toolResultMsg.content.payload.insert(QStringLiteral("formatted_result"), event.formattedResult);
            m_sessionManager->postMessage(sessionId, toolResultMsg);
        }
    }

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
    bool appStateOk = false;
    QJsonObject appState = readJsonObjectFile(appStatePath(), &appStateOk);
    if (!appStateOk)
        appState = QJsonObject();

    QJsonObject tabState;
    QJsonArray agentIds;
    QStringList normalizedAgentIds;
    for (const QString& id : openAgentIds) {
        const QString trimmed = id.trimmed();
        if (trimmed.isEmpty() || normalizedAgentIds.contains(trimmed))
            continue;
        normalizedAgentIds.append(trimmed);
    }
    for (const QString& id : normalizedAgentIds)
        agentIds.append(id);
    tabState.insert(QStringLiteral("openAgentIds"), agentIds);
    tabState.insert(QStringLiteral("activeIdentityId"), activeIdentityId);
    appState.insert(QStringLiteral("schemaVersion"), 3);
    appState.insert(QStringLiteral("tabState"), tabState);
    writeJsonObjectFile(appStatePath(), appState);
}

ChatService::TabState ChatService::loadTabState() const
{
    TabState state;
    bool appStateOk = false;
    const QJsonObject appState = readJsonObjectFile(appStatePath(), &appStateOk);
    if (!appStateOk)
        return state;

    QJsonObject tabObj = appState.value(QStringLiteral("tabState")).toObject();
    QJsonArray arr = tabObj.value(QStringLiteral("openAgentIds")).toArray();
    for (const QJsonValue& v : arr) {
        const QString id = v.toString().trimmed();
        if (!id.isEmpty())
            state.openAgentIds.append(id);
    }
    state.openAgentIds.removeDuplicates();
    state.activeIdentityId = tabObj.value(QStringLiteral("activeIdentityId")).toString().trimmed();
    return state;
}
