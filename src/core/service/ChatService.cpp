#include "ChatService.h"
#include "AgentPulse.h"
#include "AgentPulseRegistry.h"
#include "AgentRuntime.h"
#include "ChatCoordinatorFactory.h"
#include "ChatCoordinatorSupport.h"
#include "ConfigService.h"
#include "CodexTeammateBackend.h"
#include "TeammateManager.h"
#include "BackgroundTaskCoordinator.h"
#include "ConversationDispatchCoordinator.h"
#include "ConversationEnqueueCoordinator.h"
#include "ConversationStreamCoordinator.h"
#include "TurnCompletionCoordinator.h"
#include "ToolEventCoordinator.h"
#include "HeartbeatDispatchCoordinator.h"
#include "HeartbeatPromptBuilder.h"
#include "HeartbeatSnapshotCoordinator.h"
#include "HeartbeatStateStore.h"
#include "HealthMonitor.h"
#include "HeartbeatReplyUtils.h"
#include "HeartbeatService.h"
#include "MemoryMaintenanceService.h"
#include "MemoryToolWriteService.h"
#include "RuntimeManager.h"
#include "SchedulerService.h"
#include "TaskStateService.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/agent/LLMAgent.h"
#include "core/agent/McpToolProvider.h"
#include "core/agent/ToolDispatcher.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/memory/MemoryManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "ChatStateRepository.h"
#include "MessageRouter.h"
#include "PrimarySessionResolver.h"
#include "core/utils/DefaultPrompts.h"
#include "core/utils/ModelConfigLoader.h"
#include "core/tools/MemoryTool.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace {
using ChatCoordinatorSupport::buildDelegateRecoveryReply;
using ChatCoordinatorSupport::delegateToolKey;
using ChatCoordinatorSupport::estimateHistoryChars;
using ChatCoordinatorSupport::isBackgroundHeartbeatClientMessageId;
using ChatCoordinatorSupport::isHeartbeatClientMessageId;
using ChatCoordinatorSupport::isManualHeartbeatClientMessageId;
using ChatCoordinatorSupport::isTransientUpstreamError;
using ChatCoordinatorSupport::pulseStateToString;
using ChatCoordinatorSupport::sanitizePersistedToolArguments;
using ChatCoordinatorSupport::sanitizePersistedToolEventData;
using ChatCoordinatorSupport::sanitizePersistedToolRawResult;
using ChatCoordinatorSupport::taskStateTextPreview;
using ChatCoordinatorSupport::toolEventToJson;

bool envFlagEnabled(const char* key)
{
    const QString raw = QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(key)).trimmed().toLower();
    return raw == QLatin1String("1")
        || raw == QLatin1String("true")
        || raw == QLatin1String("yes")
        || raw == QLatin1String("on");
}

bool shouldMirrorEventToIoHistory(const QString& type)
{
    if (type.startsWith(QStringLiteral("memory.")) || type.startsWith(QStringLiteral("delegate.")))
        return true;
    return type == QLatin1String("turn_started")
        || type == QLatin1String("turn_dispatch_prepare")
        || type == QLatin1String("turn_dispatch_config_applied")
        || type == QLatin1String("turn_dispatch_sent")
        || type == QLatin1String("turn_tool_calls_started")
        || type == QLatin1String("turn_completed")
        || type == QLatin1String("turn_failed")
        || type == QLatin1String("turn_recovered")
        || type == QLatin1String("context.compacted")
        || type == QLatin1String("task_state.updated");
}

QString toolNamesFromToolCalls(const QJsonArray& toolCalls)
{
    QStringList names;
    for (const QJsonValue& value : toolCalls) {
        const QJsonObject toolCall = value.toObject();
        const QString name = toolCall.value(QStringLiteral("function"))
                                 .toObject()
                                 .value(QStringLiteral("name"))
                                 .toString()
                                 .trimmed();
        if (!name.isEmpty())
            names.append(name);
    }
    names.removeDuplicates();
    return names.join(QStringLiteral(", "));
}

QString historySnippetForSummary(const QJsonObject& msg, int maxChars)
{
    const QString role = msg.value(QStringLiteral("role")).toString();
    if (role == QLatin1String("tool")) {
        const QString toolId = msg.value(QStringLiteral("tool_call_id")).toString().trimmed();
        return QStringLiteral("- [tool] result for %1")
            .arg(toolId.isEmpty() ? QStringLiteral("unknown-tool-call") : toolId);
    }

    if (role == QLatin1String("assistant") && msg.contains(QStringLiteral("tool_calls"))) {
        const QString names = toolNamesFromToolCalls(msg.value(QStringLiteral("tool_calls")).toArray());
        return QStringLiteral("- [assistant] requested tools: %1")
            .arg(names.isEmpty() ? QStringLiteral("unknown") : names);
    }

    QString content = msg.value(QStringLiteral("content")).toString();
    content.replace(QLatin1Char('\r'), QLatin1Char(' '));
    content.replace(QLatin1Char('\n'), QLatin1Char(' '));
    content = content.simplified();
    if (content.isEmpty())
        return QString();
    if (maxChars > 0 && content.size() > maxChars)
        content = content.left(maxChars) + QStringLiteral("...");
    return QStringLiteral("- [%1] %2").arg(role, content);
}

QJsonArray compactHistoryWithBudget(const QJsonArray& history, int maxMessages, int maxChars)
{
    if (history.isEmpty())
        return history;

    const int safeMaxMessages = qMax(2, maxMessages);
    const int safeMaxChars = qMax(2048, maxChars);
    const int originalChars = estimateHistoryChars(history);
    if (history.size() <= safeMaxMessages && originalChars <= safeMaxChars)
        return history;

    QJsonArray kept = history;
    QJsonArray removed;

    const int targetMessagesWithoutSummary = qMax(1, safeMaxMessages - 1);
    const int targetCharsWithoutSummary = qMax(1200, safeMaxChars - 1400);
    while (!kept.isEmpty()) {
        if (kept.size() <= targetMessagesWithoutSummary
            && estimateHistoryChars(kept) <= targetCharsWithoutSummary) {
            break;
        }
        removed.append(kept.takeAt(0));
    }

    while (!kept.isEmpty()
           && kept.first().toObject().value(QStringLiteral("role")).toString() == QLatin1String("tool")) {
        removed.append(kept.takeAt(0));
    }

    if (kept.isEmpty() && !history.isEmpty())
        kept.append(history.last());

    QStringList highlights;
    int highlightChars = 0;
    const int kMaxHighlights = 10;
    const int kMaxHighlightChars = 1400;
    for (const QJsonValue& value : removed) {
        if (highlights.size() >= kMaxHighlights)
            break;
        const QString snippet = historySnippetForSummary(value.toObject(), 160);
        if (snippet.isEmpty())
            continue;
        if (highlightChars + snippet.size() > kMaxHighlightChars)
            break;
        highlights.append(snippet);
        highlightChars += snippet.size();
    }

    QJsonObject summaryMsg;
    summaryMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
    QString summary = QStringLiteral(
                          "[Context Compact]\n"
                          "Earlier %1 messages were compacted to keep this turn within context budget.\n")
                          .arg(removed.size());
    if (!highlights.isEmpty()) {
        summary += QStringLiteral("Key highlights:\n");
        summary += highlights.join(QStringLiteral("\n"));
    }
    summaryMsg.insert(QStringLiteral("content"), summary.trimmed());

    QJsonArray compacted;
    compacted.append(summaryMsg);
    for (const QJsonValue& value : kept)
        compacted.append(value);

    while (compacted.size() > safeMaxMessages || estimateHistoryChars(compacted) > safeMaxChars) {
        if (compacted.size() <= 2)
            break;
        compacted.removeAt(1);
        while (compacted.size() > 1
               && compacted.at(1).toObject().value(QStringLiteral("role")).toString()
                   == QLatin1String("tool")) {
            compacted.removeAt(1);
        }
    }

    return compacted;
}

QString normalizeHeartbeatSignal(const QString& raw)
{
    const QString s = raw.trimmed().toLower();
    if (s == QLatin1String("provider") || s == QLatin1String("provider_status"))
        return QStringLiteral("provider_status");
    if (s == QLatin1String("delegate") || s == QLatin1String("delegate_jobs"))
        return QStringLiteral("delegate_jobs");
    if (s == QLatin1String("pulse") || s == QLatin1String("pulse_state"))
        return QStringLiteral("pulse_state");
    if (s == QLatin1String("scheduler") || s == QLatin1String("scheduler_jobs"))
        return QStringLiteral("scheduler_jobs");
    if (s == QLatin1String("memory") || s == QLatin1String("memory_progress"))
        return QStringLiteral("memory_progress");
    return s;
}

QStringList normalizeHeartbeatSignals(const QStringList& input)
{
    QStringList out;
    for (const QString& raw : input) {
        const QString s = normalizeHeartbeatSignal(raw);
        if (s.isEmpty())
            continue;
        if (!out.contains(s))
            out.append(s);
    }
    if (out.isEmpty()) {
        out << QStringLiteral("provider_status")
            << QStringLiteral("delegate_jobs")
            << QStringLiteral("pulse_state");
    }
    return out;
}

bool isHeartbeatPromptText(const QString& text)
{
    return text.trimmed().startsWith(QStringLiteral("【系统心跳任务】"));
}

bool isHeartbeatNoChangeReplyText(const QString& text)
{
    const QString t = text.trimmed();
    return t == QStringLiteral("当前无关键更新。")
        || t == QStringLiteral("当前无关键更新")
        || t == QStringLiteral("无关键更新。")
        || t == QStringLiteral("无关键更新");
}

} // namespace

ChatService::ChatService(QObject* parent)
    : QObject(parent)
    , m_persistence(new ChatPersistenceService())
    , m_stateRepository(new ChatStateRepository())
    , m_memoryManager(new MemoryManager(m_persistence.get()))
    , m_healthMonitor(new HealthMonitor(this))
    , m_heartbeatService(new HeartbeatService(this))
    , m_schedulerService(new SchedulerService(this))
    , m_taskStateService(new TaskStateService())
    , m_agentPulseRegistry(new AgentPulseRegistry(
          AgentPulseRegistry::Dependencies {
              this,
              &m_agentPulses,
              [](AgentPulse::State state) { return pulseStateToString(state); },
              [this](const QString& changedAgentId, const QString& stateText) {
                  QJsonObject extra;
                  extra.insert(QStringLiteral("agent_id"), changedAgentId);
                  extra.insert(QStringLiteral("state"), stateText);
                  emitPipelineEvent(QStringLiteral("pulse.state_changed"), QString(), nullptr, QString(), QString(), extra);
              },
              [this](const QString& changedAgentId) {
                  QJsonObject extra;
                  extra.insert(QStringLiteral("agent_id"), changedAgentId);
                  emitPipelineEvent(QStringLiteral("pulse.hard_timeout"), QString(), nullptr, QString(), QStringLiteral("agent_no_progress"), extra);
              }
          }))
    , m_runtimeManager(new RuntimeManager(this))
    , m_configService(new ConfigService(this))
    , m_logVerboseStreamEvents(envFlagEnabled("TMAGENT_LOG_STREAM_EVENTS_VERBOSE"))
{
    connect(m_runtimeManager, &RuntimeManager::runtimeCreated, this, &ChatService::connectRuntimeSignals);
    connect(m_configService, &ConfigService::configLoaded, this, &ChatService::configLoaded);
    if (m_heartbeatService) {
        connect(m_heartbeatService.get(), &HeartbeatService::heartbeatTriggered, this, &ChatService::onHeartbeatTriggered);
        connect(m_heartbeatService.get(), &HeartbeatService::heartbeatSkipped, this, [this](const QString& agentId, const QString& reason) {
            QJsonObject extra;
            extra.insert(QStringLiteral("agent_id"), agentId);
            extra.insert(QStringLiteral("reason"), reason);
            emitPipelineEvent(QStringLiteral("heartbeat.skipped"), QString(), nullptr, QString(), reason, extra);
        });
    }
    if (m_schedulerService) {
        connect(m_schedulerService.get(), &SchedulerService::jobFired, this, &ChatService::onScheduledJobTriggered);
    }
    // P0: 连接子 Agent 完成通知
    connect(DelegateTaskScheduler::instance(), &DelegateTaskScheduler::jobSettled, this, &ChatService::onDelegateJobSettled);
}

ChatService::~ChatService()
{
    saveSessionsToDisk();
}

void ChatService::initialize()
{
    // 初始化 SQLite 数据库（在所有服务之前）
    DatabaseManager::instance()->initialize();

    m_identityManager = IdentityManager::instance();
    m_sessionManager = SessionManager::instance();
    m_modelFactory = ModelFactory::instance();
    connect(m_modelFactory, &ModelFactory::modelCacheUpdated, this, &ChatService::modelCatalogUpdated, Qt::UniqueConnection);

    m_toolDispatcher = ToolDispatcher::instance();
    m_toolDispatcher->registerDefaultTools();
    MemoryTool::setWriteHandler([this](const QJsonObject& args) {
        return executeMemoryWriteTool(args);
    });

    m_mcpProvider = new McpToolProvider(m_toolDispatcher);
    m_toolDispatcher->registerProvider(m_mcpProvider, "mcp");
    if (m_stateRepository) {
        m_stateRepository->setDependencies(
            m_identityManager,
            m_sessionManager,
            m_toolDispatcher,
            m_persistence.get());
    }

    // 注入依赖到 RuntimeManager
    m_runtimeManager->setModelFactory(m_modelFactory);
    m_runtimeManager->setToolDispatcher(m_toolDispatcher);
    m_runtimeManager->setSessionManager(m_sessionManager);
    m_runtimeManager->setPersistence(m_persistence.get());

    // 注入依赖到 ConfigService
    m_configService->setPersistence(m_persistence.get());
    m_configService->setModelFactory(m_modelFactory);
    m_configService->setMcpProvider(m_mcpProvider);
    m_configService->setToolDispatcher(m_toolDispatcher);
    m_configService->setRuntimeManager(m_runtimeManager);

    m_configService->applyMcpConfig(m_configService->loadMcpConfigSpecs());

    // 注册队友后端
    TeammateManager::instance()->registerBackend(new CodexTeammateBackend(this));

    // 队友回复：缓存到待注入队列，静默触发助手新一轮 turn（不显示在 UI）
    connect(TeammateManager::instance(), &TeammateManager::teammateReplied, this,
        [this](const QString& teammateId, const QString& teammateName, bool success, const QString& content) {
            const QString sessionId = m_currentSessionId;
            if (sessionId.isEmpty())
                return;

            const QString statusText = success ? QStringLiteral("completed") : QStringLiteral("failed");
            QString injection = QStringLiteral("<teammate-message teammate_id=\"%1\" name=\"%2\" status=\"%3\">\n%4\n</teammate-message>")
                .arg(teammateId, teammateName, statusText,
                     content.size() > 4000
                         ? content.left(4000) + QStringLiteral("\n...[truncated, total %1 chars, showing first 4000. Use message_teammate to request remaining content]...").arg(content.size())
                         : content);

            // 静默触发助手新一轮 turn（不产生 UI 气泡，不投递 Message）
            enqueueInternalTurn(sessionId, injection,
                QStringLiteral("teammate-reply-%1").arg(teammateId));

            QJsonObject extra;
            extra.insert(QStringLiteral("teammate_id"), teammateId);
            extra.insert(QStringLiteral("teammate_name"), teammateName);
            extra.insert(QStringLiteral("success"), success);
            emitPipelineEvent(QStringLiteral("teammate.replied"), sessionId, nullptr,
                              QString(), QString(), extra, true);
        });

    if (m_sessionManager) {
        connect(m_sessionManager, &SessionManager::messagePosted, this, &ChatService::appendSessionMessageToDisk, Qt::UniqueConnection);
    }

    // 确保用户 Identity 存在
    m_identityManager->userIdentity();
    if (m_memoryManager) {
        QString memoryError;
        if (!m_memoryManager->ensureUserMemoryDocument(&memoryError) && !memoryError.isEmpty())
            qWarning() << "[ChatService] user memory init failed:" << memoryError;
    }

    m_configService->loadConfig();

    if (m_healthMonitor) {
        m_healthMonitor->setRuntimeManager(m_runtimeManager);
        m_healthMonitor->setModelFactory(m_modelFactory);
        connect(m_healthMonitor.get(), &HealthMonitor::providerDown, this, [this](const QString& configId, const QString& reason) {
            QJsonObject extra;
            extra.insert(QStringLiteral("provider_id"), configId);
            extra.insert(QStringLiteral("state"), QStringLiteral("down"));
            extra.insert(QStringLiteral("reason"), reason);
            emitPipelineEvent(QStringLiteral("infra.provider_down"), QString(), nullptr, QString(), reason, extra);
        });
        connect(m_healthMonitor.get(), &HealthMonitor::providerRecovered, this, [this](const QString& configId) {
            QJsonObject extra;
            extra.insert(QStringLiteral("provider_id"), configId);
            extra.insert(QStringLiteral("state"), QStringLiteral("recovered"));
            emitPipelineEvent(QStringLiteral("infra.provider_recovered"), QString(), nullptr, QString(), QString(), extra);
        });
        m_healthMonitor->start();
    }

    if (m_heartbeatService)
        m_heartbeatService->setPersistence(m_persistence.get());
    if (m_taskStateService)
        m_taskStateService->setPersistence(m_persistence.get());
    if (m_schedulerService) {
        m_schedulerService->setPersistence(m_persistence.get());
        m_schedulerService->start();
    }

    if (m_identityManager) {
        const QList<Identity*> agents = m_identityManager->allAgents();
        for (Identity* agent : agents) {
            if (!agent || !agent->isAgent())
                continue;
            const QString agentId = agent->id().trimmed();
            if (agentId.isEmpty())
                continue;
            ensureAgentPulse(agentId);
            if (m_heartbeatService)
                m_heartbeatService->startHeartbeat(agentId);
        }
    }

    // 启动跨进程同步轮询定时器
    if (!m_syncTimer) {
        m_syncTimer = new QTimer(this);
        m_syncTimer->setInterval(5000); // 5 秒轮询一次
        connect(m_syncTimer, &QTimer::timeout, this, &ChatService::pollExternalChanges);
        m_syncTimer->start();
        qDebug() << "[ChatService] 跨进程同步轮询已启动（间隔 5s）";
    }
}

QString ChatService::enqueueUserMessage(const QString& sessionId, const QString& text, const QString& clientMessageId)
{
    const QString userId = m_identityManager ? m_identityManager->userIdentity()->id() : QString();
    return enqueueUserMessageAs(userId, sessionId, text, clientMessageId);
}

QString ChatService::enqueueUserMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& text, const QString& clientMessageId)
{
    ChatCoordinatorFactory factory(*this);
    ConversationEnqueueCoordinator coordinator(factory.makeEnqueueDependencies(), factory.makeEnqueueLimits());
    return coordinator.enqueueUserMessageAs(actorIdentityId, sessionId, text, clientMessageId);
}

void ChatService::sendUserMessage(const QString& sessionId, const QString& text)
{
    enqueueUserMessage(sessionId, text);
}

void ChatService::sendUserMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& text)
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

    if (TurnTask* active = m_turnManager.activeTurn(sessionId)) {
        updateTaskStateForSession(
            sessionId,
            QStringLiteral("canceled"),
            active,
            QJsonObject {
                { QStringLiteral("reason"), QStringLiteral("user_stop") },
                { QStringLiteral("source_event"), QStringLiteral("turn_cancelled") },
                { QStringLiteral("summary"), taskStateTextPreview(active->userContent) },
                { QStringLiteral("current_step"), QStringLiteral("已取消当前执行") },
                { QStringLiteral("next_step"), QJsonValue::Null }
            });
    }

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
    emitPipelineEvent(QStringLiteral("turn_cancelled"), sessionId, &cancelled, QString(), QString(), extra);
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

    if (TurnTask* active = m_turnManager.activeTurn(sessionId)) {
        updateTaskStateForSession(
            sessionId,
            QStringLiteral("canceled"),
            active,
            QJsonObject {
                { QStringLiteral("reason"), QStringLiteral("user_stop") },
                { QStringLiteral("source_event"), QStringLiteral("turn_cancelled") },
                { QStringLiteral("summary"), taskStateTextPreview(active->userContent) },
                { QStringLiteral("current_step"), QStringLiteral("已取消当前执行") },
                { QStringLiteral("next_step"), QJsonValue::Null }
            });
    }

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
    const LLMConfig defaultCfg = m_runtimeManager->defaultAgentConfig();
    profile->setLlmConfig(defaultCfg);
    profile->setSystemPrompt(defaultCfg.systemPrompt);
    profile->setDelegateEnabled(true);
    profile->setAllowedTools(ChatStateRepository::collectToolNamesFrom(m_toolDispatcher));

    QString name = agentName.isEmpty() ? QStringLiteral("TM Agent") : agentName;
    Identity* agentIdentity = m_identityManager->createAgent(name, profile);
    ensureMemoryInitializedForAgent(agentIdentity);
    if (agentIdentity) {
        const QString agentId = agentIdentity->id().trimmed();
        if (!agentId.isEmpty()) {
            ensureAgentPulse(agentId);
            if (m_heartbeatService)
                m_heartbeatService->startHeartbeat(agentId);
        }
    }

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

Session* ChatService::createSessionForIdentityAs(const QString& actorIdentityId, const QString& identityId, const QString& title)
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
    ensureMemoryInitializedForAgent(identity);
    if (identity) {
        const QString agentId = identity->id().trimmed();
        if (!agentId.isEmpty()) {
            ensureAgentPulse(agentId);
            if (m_heartbeatService)
                m_heartbeatService->startHeartbeat(agentId);
        }
    }

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
    clearTaskStateForSession(sessionId);
    m_delegateStatsBySession.remove(sessionId);
    m_teammateInjections.remove(sessionId);
    clearToolProgressCacheForSession(sessionId);
    const QString keyPrefix = sessionId.trimmed() + QStringLiteral("|");
    for (auto it = m_delegateStartMsByToolKey.begin(); it != m_delegateStartMsByToolKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_delegateStartMsByToolKey.erase(it);
        else
            ++it;
    }

    m_sessionManager->removeSession(sessionId);
    if (!agentId.isEmpty()) {
        tryStartNextTurnForAgent(agentId);
        releaseRuntimeIfUnused(agentId);
    }

    if (m_currentSessionId == sessionId) {
        m_currentSessionId.clear();
    }

    if (m_persistence)
        m_persistence->removeSessionFromDb(sessionId);

    emit sessionRemoved(sessionId);
    saveSessionsToDisk();
    return true;
}

bool ChatService::removeAgentMemoryAs(const QString& actorIdentityId, const QString& agentIdentityId)
{
    if (!canIdentityManageSessions(actorIdentityId)) {
        qWarning() << "[ChatService] 拒绝删除 Agent 记忆目录，actor 无权限:" << actorIdentityId
                   << "agent:" << agentIdentityId;
        return false;
    }
    if (!m_memoryManager)
        return true;

    QString err;
    const bool ok = m_memoryManager->removeAgentMemory(agentIdentityId, &err);
    if (ok) {
        m_memoryRetainedTurnsByAgent.remove(agentIdentityId.trimmed());
        m_heartbeatRuntimeByAgent.remove(agentIdentityId.trimmed());
    }
    if (!ok)
        qWarning() << "[ChatService] 删除 Agent 记忆目录失败:" << agentIdentityId << err;
    return ok;
}

bool ChatService::rememberMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& messageId, const QString& fallbackContent, QString* error)
{
    if (error)
        error->clear();
    if (!canIdentityManageGlobalConfig(actorIdentityId)) {
        if (error)
            *error = QStringLiteral("actor has no permission");
        return false;
    }
    if (!m_memoryManager) {
        if (error)
            *error = QStringLiteral("memory manager unavailable");
        return false;
    }

    Session* session = m_sessionManager ? m_sessionManager->findById(sessionId) : nullptr;
    if (!session) {
        if (error)
            *error = QStringLiteral("session not found");
        return false;
    }

    const QString agentId = agentIdentityIdForSession(sessionId);
    if (agentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent identity not found for session");
        return false;
    }

    QString selectedText;
    QString selectedTurnId;
    QString selectedTraceId;
    const QString trimmedMessageId = messageId.trimmed();
    const QString fallback = fallbackContent.trimmed();
    const QList<Message> allMessages = session->allMessages();

    // Search messages in reverse for a text/system message matching a predicate
    auto findMessage = [&](auto&& match) -> bool {
        for (int i = allMessages.size() - 1; i >= 0; --i) {
            const Message& msg = allMessages.at(i);
            if (msg.content.type != MessageContent::Type::Text
                && msg.content.type != MessageContent::Type::System)
                continue;
            if (!match(msg))
                continue;
            selectedText = msg.content.text.trimmed();
            selectedTurnId = msg.turnId.trimmed();
            selectedTraceId = msg.traceId.trimmed();
            return true;
        }
        return false;
    };

    if (!trimmedMessageId.isEmpty())
        findMessage([&](const Message& msg) { return msg.id == trimmedMessageId; });
    if (selectedText.isEmpty() && !fallback.isEmpty())
        findMessage([&](const Message& msg) { return msg.content.text.trimmed() == fallback; });
    if (selectedText.isEmpty())
        selectedText = fallback;
    if (selectedText.isEmpty()) {
        if (error)
            *error = QStringLiteral("message content is empty");
        return false;
    }

    QString memorySummary;
    QString memoryPath;
    QJsonObject memoryMetadata;
    QString memoryError;
    const bool ok = m_memoryManager->rememberManual(agentId, sessionId, selectedTurnId, selectedTraceId, selectedText, &memorySummary, &memoryPath, &memoryMetadata, &memoryError);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    TurnTask syntheticTurn;
    const TurnTask* eventTurn = activeTurn;
    if (!eventTurn && (!selectedTurnId.isEmpty() || !selectedTraceId.isEmpty())) {
        syntheticTurn.turnId = selectedTurnId;
        syntheticTurn.requestTraceId = selectedTraceId;
        syntheticTurn.runId = QStringLiteral("manual_remember");
        syntheticTurn.actorIdentityId = actorIdentityId;
        eventTurn = &syntheticTurn;
    }
    if (!ok) {
        QJsonObject memoryExtra;
        memoryExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        memoryExtra.insert(QStringLiteral("path"), memoryPath);
        memoryExtra.insert(QStringLiteral("manualRemember"), true);
        if (!selectedTraceId.isEmpty())
            memoryExtra.insert(QStringLiteral("source_trace_id"), selectedTraceId);
        if (!selectedTurnId.isEmpty())
            memoryExtra.insert(QStringLiteral("source_turn_id"), selectedTurnId);
        emitPipelineEvent(QStringLiteral("memory.error"), sessionId, eventTurn, QString(), memoryError.isEmpty() ? QStringLiteral("manual remember failed") : memoryError, memoryExtra);
        if (error)
            *error = memoryError.isEmpty()
                ? QStringLiteral("manual remember failed")
                : memoryError;
        return false;
    }

    QJsonObject updateExtra;
    updateExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
    updateExtra.insert(QStringLiteral("summary"), memorySummary);
    updateExtra.insert(QStringLiteral("path"), memoryPath);
    updateExtra.insert(QStringLiteral("manualRemember"), true);
    if (!selectedTraceId.isEmpty())
        updateExtra.insert(QStringLiteral("source_trace_id"), selectedTraceId);
    if (!selectedTurnId.isEmpty())
        updateExtra.insert(QStringLiteral("source_turn_id"), selectedTurnId);
    for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
        updateExtra.insert(it.key(), it.value());
    emitPipelineEvent(QStringLiteral("memory.updated"), sessionId, eventTurn, QString(), QString(), updateExtra);

    const int compactedCount = memoryMetadata.value(QStringLiteral("compacted_count")).toInt();
    if (compactedCount > 0) {
        QJsonObject compactExtra;
        compactExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        compactExtra.insert(QStringLiteral("summary"), memorySummary);
        compactExtra.insert(QStringLiteral("compacted_count"), compactedCount);
        compactExtra.insert(QStringLiteral("path"), memoryMetadata.value(QStringLiteral("longMemoryPath")).toString());
        compactExtra.insert(QStringLiteral("longMemoryAdded"), memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt());
        compactExtra.insert(QStringLiteral("longMemoryDuplicate"), memoryMetadata.value(QStringLiteral("longMemoryDuplicate")).toInt());
        compactExtra.insert(QStringLiteral("manualRemember"), true);
        for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
            compactExtra.insert(it.key(), it.value());
        if (!selectedTraceId.isEmpty())
            compactExtra.insert(QStringLiteral("source_trace_id"), selectedTraceId);
        if (!selectedTurnId.isEmpty())
            compactExtra.insert(QStringLiteral("source_turn_id"), selectedTurnId);
        emitPipelineEvent(QStringLiteral("memory.compacted"), sessionId, eventTurn, QString(), QString(), compactExtra);
    }

    MemoryMaintenanceService memoryMaintenance = makeMemoryMaintenanceService();
    memoryMaintenance.refreshIndexAndEmit(
        sessionId, agentId, eventTurn, QStringLiteral("manual_remember"), memoryPath, memoryMetadata);

    return true;
}

bool ChatService::rebuildMemoryIndexAs(const QString& actorIdentityId, const QString& agentIdentityId, QJsonObject* result, QString* error)
{
    if (result)
        *result = QJsonObject();
    if (error)
        error->clear();

    if (!canIdentityManageGlobalConfig(actorIdentityId)) {
        if (error)
            *error = QStringLiteral("actor has no permission");
        return false;
    }
    if (!m_memoryManager || !m_identityManager) {
        if (error)
            *error = QStringLiteral("memory manager unavailable");
        return false;
    }

    QStringList targetAgents;
    const QString targetAgentId = agentIdentityId.trimmed();
    if (!targetAgentId.isEmpty()) {
        Identity* agent = m_identityManager->findById(targetAgentId);
        if (!agent || !agent->isAgent()) {
            if (error)
                *error = QStringLiteral("agent not found");
            return false;
        }
        targetAgents.append(agent->id());
    } else {
        const QList<Identity*> agents = m_identityManager->allAgents();
        for (Identity* agent : agents) {
            if (!agent || !agent->isAgent())
                continue;
            targetAgents.append(agent->id());
        }
    }

    if (targetAgents.isEmpty()) {
        if (result) {
            result->insert(QStringLiteral("agents_total"), 0);
            result->insert(QStringLiteral("agents_success"), 0);
            result->insert(QStringLiteral("agents_failed"), 0);
            result->insert(QStringLiteral("rows_indexed"), 0);
            result->insert(QStringLiteral("items"), QJsonArray());
        }
        return true;
    }

    int successCount = 0;
    int failedCount = 0;
    int totalRows = 0;
    QJsonArray items;
    QStringList failures;

    for (const QString& id : targetAgents) {
        QJsonObject indexMetadata;
        QString indexError;
        const bool ok = m_memoryManager->rebuildSearchIndex(id, &indexMetadata, &indexError);

        QJsonObject item;
        item.insert(QStringLiteral("agent_id"), id);
        item.insert(QStringLiteral("success"), ok);
        if (ok) {
            ++successCount;
            totalRows += indexMetadata.value(QStringLiteral("rows_indexed")).toInt();
            for (auto it = indexMetadata.constBegin(); it != indexMetadata.constEnd(); ++it)
                item.insert(it.key(), it.value());
        } else {
            ++failedCount;
            item.insert(QStringLiteral("error"), indexError);
            failures.append(QStringLiteral("%1: %2").arg(id, indexError));
        }
        items.append(item);

        QString sessionId;
        if (m_sessionManager) {
            const QList<Session*> sessions = m_sessionManager->sessionsForIdentity(id);
            if (!sessions.isEmpty())
                sessionId = sessions.first()->id();
        }
        QJsonObject eventExtra = indexMetadata;
        eventExtra.insert(QStringLiteral("agent_id"), id);
        eventExtra.insert(QStringLiteral("reason"), QStringLiteral("manual_rebuild"));
        eventExtra.insert(QStringLiteral("scope"), targetAgentId.isEmpty() ? QStringLiteral("all") : QStringLiteral("single"));
        emitPipelineEvent(ok ? QStringLiteral("memory.index.updated") : QStringLiteral("memory.index.error"), sessionId, nullptr, QString(), ok ? QString() : indexError, eventExtra);
    }

    if (result) {
        result->insert(QStringLiteral("agents_total"), targetAgents.size());
        result->insert(QStringLiteral("agents_success"), successCount);
        result->insert(QStringLiteral("agents_failed"), failedCount);
        result->insert(QStringLiteral("rows_indexed"), totalRows);
        result->insert(QStringLiteral("items"), items);
    }

    if (failedCount > 0) {
        if (error)
            *error = failures.join(QStringLiteral("; "));
        return false;
    }
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
    return m_runtimeManager->runtimeForAgent(agentId);
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
    m_runtimeManager->setDefaultAgentConfig(config);
}

void ChatService::registerModelConfig(const ModelConfig& config)
{
    if (m_modelFactory)
        m_modelFactory->registerModelConfig(config);
}

LLMConfig ChatService::defaultAgentConfig() const { return m_runtimeManager->defaultAgentConfig(); }

void ChatService::applyConfigToAllRuntimes()
{
    m_runtimeManager->applyConfigToAllRuntimes();
}

void ChatService::applyToolDispatcherToAllRuntimes()
{
    m_runtimeManager->applyToolDispatcherToAllRuntimes();
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

QJsonObject ChatService::taskStateForSession(const QString& sessionId) const
{
    return m_taskStateService
        ? m_taskStateService->stateForSession(sessionId)
        : QJsonObject();
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

QString ChatService::runtimeIdentityIdForSession(const QString& sessionId) const
{
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (!runtime)
        return QString();

    const QString runtimeIdentityId = runtime->identityId().trimmed();
    if (!runtimeIdentityId.isEmpty())
        return runtimeIdentityId;
    return runtime->identity() ? runtime->identity()->id() : QString();
}

QJsonArray ChatService::ioHistoryForSession(const QString& sessionId) const
{
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime && runtime->currentSessionId() == sessionId)
        return runtime->getIoHistory();
    return QJsonArray();
}

QString ChatService::modelDisplayName(const LLMConfig& config) const
{
    if (!config.isValid())
        return QStringLiteral("默认模型");
    if (m_modelFactory) {
        const QString modelInfo = m_modelFactory->resolveModelId(config).trimmed();
        return modelInfo.isEmpty() ? QStringLiteral("未指定模型") : modelInfo;
    }
    const QString modelInfo = config.selectedModelId.trimmed();
    return modelInfo.isEmpty() ? QStringLiteral("未指定模型") : modelInfo;
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

void ChatService::applyMcpConfig(const QStringList& specs)
{
    m_configService->applyMcpConfig(specs);
}

QStringList ChatService::loadMcpConfigSpecs() const
{
    return m_configService->loadMcpConfigSpecs();
}

bool ChatService::saveMcpConfigSpecs(const QStringList& specs) const
{
    return m_configService->saveMcpConfigSpecs(specs);
}

bool ChatService::saveToolLoopPolicyObject(const QJsonObject& raw, QString* errOut) const
{
    return m_configService->saveToolLoopPolicyObject(raw, errOut);
}

QString ChatService::mcpConfigPath() const
{
    return m_configService->mcpConfigPath();
}

QString ChatService::modelConfigPath() const
{
    return m_configService->modelConfigPath();
}

QJsonObject ChatService::defaultToolLoopPolicyObject() const
{
    return m_configService->defaultToolLoopPolicyObject();
}

QJsonObject ChatService::normalizeToolLoopPolicyObject(const QJsonObject& raw) const
{
    return m_configService->normalizeToolLoopPolicyObject(raw);
}

QJsonObject ChatService::loadToolLoopPolicyObject() const
{
    return m_configService->loadToolLoopPolicyObject();
}

QStringList ChatService::registeredModelConfigIds() const
{
    return m_modelFactory ? m_modelFactory->registeredConfigIds() : QStringList();
}

QStringList ChatService::enabledProviderInstanceIds() const
{
    return m_modelFactory ? m_modelFactory->enabledInstanceIds() : QStringList();
}

QString ChatService::displayNameForProviderInstance(const QString& instanceId) const
{
    return m_modelFactory ? m_modelFactory->displayNameForInstance(instanceId) : QString();
}

QList<AvailableModel> ChatService::cachedModelsForProviderInstance(const QString& instanceId) const
{
    return m_modelFactory ? m_modelFactory->cachedModels(instanceId) : QList<AvailableModel>();
}

void ChatService::fetchModelsForProviderInstanceAsync(const QString& instanceId)
{
    if (m_modelFactory)
        m_modelFactory->fetchModelsAsync(instanceId);
}

QStringList ChatService::registeredToolNames() const
{
    return ChatStateRepository::collectToolNamesFrom(m_toolDispatcher);
}

void ChatService::subscribeConversationEvent(QObject* context, const std::function<void(const QJsonObject&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::conversationEvent, context, [handler](const QJsonObject& event) {
        handler(event);
    });
}

void ChatService::subscribeStreamData(QObject* context, const std::function<void(const QString&, const QString&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::streamDataReceived, context, [handler](const QString& sessionId, const QString& data) {
        handler(sessionId, data);
    });
}

void ChatService::subscribeFinished(QObject* context, const std::function<void(const QString&, const QString&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::finished, context, [handler](const QString& sessionId, const QString& content) {
        handler(sessionId, content);
    });
}

void ChatService::subscribeError(QObject* context, const std::function<void(const QString&, const QString&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::errorOccurred, context, [handler](const QString& sessionId, const QString& error) {
        handler(sessionId, error);
    });
}

void ChatService::subscribeToolCallsStarted(QObject* context, const std::function<void(const QString&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::toolCallsStarted, context, [handler](const QString& sessionId) {
        handler(sessionId);
    });
}

void ChatService::subscribeToolEvent(QObject* context, const std::function<void(const QString&, const ToolExecutionEvent&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::toolEvent, context, [handler](const QString& sessionId, const ToolExecutionEvent& event) {
        handler(sessionId, event);
    });
}

void ChatService::subscribeReasoningStarted(QObject* context, const std::function<void(const QString&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::reasoningStarted, context, [handler](const QString& sessionId) {
        handler(sessionId);
    });
}

void ChatService::subscribeReasoningStopped(QObject* context, const std::function<void(const QString&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::reasoningStopped, context, [handler](const QString& sessionId) {
        handler(sessionId);
    });
}

void ChatService::subscribeSessionCreated(QObject* context, const std::function<void(const QString&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::sessionCreated, context, [handler](const QString& sessionId) {
        handler(sessionId);
    });
}

void ChatService::subscribeSessionRemoved(QObject* context, const std::function<void(const QString&)>& handler)
{
    if (!context || !handler)
        return;
    QObject::connect(this, &ChatService::sessionRemoved, context, [handler](const QString& sessionId) {
        handler(sessionId);
    });
}

QJsonObject ChatService::loadMemoryPolicyObject(bool* ok) const
{
    return m_configService->loadMemoryPolicyObject(ok);
}

bool ChatService::saveMemoryPolicyObject(const QJsonObject& obj) const
{
    return m_configService->saveMemoryPolicyObject(obj);
}

QString ChatService::loadUserMemoryMarkdown(bool* ok) const
{
    return m_configService->loadUserMemoryMarkdown(ok);
}

bool ChatService::saveUserMemoryMarkdown(const QString& markdown, QString* errOut) const
{
    return m_configService->saveUserMemoryMarkdown(markdown, errOut);
}

QString ChatService::agentHeartbeatInstructionPath(const QString& agentId) const
{
    return m_configService->agentHeartbeatInstructionPath(agentId);
}

QString ChatService::heartbeatRuntimeStateLocation(const QString& agentId) const
{
    return m_configService->heartbeatRuntimeStateLocation(agentId);
}

QJsonObject ChatService::loadHeartbeatRuntimeState(const QString& agentId, bool* ok) const
{
    return m_configService->loadHeartbeatRuntimeState(agentId, ok);
}

QString ChatService::readPossiblyMojibakeUtf8File(const QString& filePath, bool* ok) const
{
    return m_configService->readPossiblyMojibakeUtf8File(filePath, ok);
}

bool ChatService::writeUtf8TextFile(const QString& filePath, const QString& text, QString* errOut) const
{
    return m_configService->writeUtf8TextFile(filePath, text, errOut);
}

HeartbeatConfig ChatService::heartbeatConfigForAgent(const QString& agentId) const
{
    return m_heartbeatService ? m_heartbeatService->configForAgent(agentId) : HeartbeatConfig {};
}

QString ChatService::heartbeatPathForAgent(const QString& agentId) const
{
    return m_heartbeatService ? m_heartbeatService->heartbeatPathForAgent(agentId) : QString();
}

void ChatService::updateHeartbeatConfig(const QString& agentId, const HeartbeatConfig& config)
{
    if (m_heartbeatService)
        m_heartbeatService->updateConfig(agentId, config);
}

void ChatService::startHeartbeatForAgent(const QString& agentId)
{
    if (m_heartbeatService)
        m_heartbeatService->startHeartbeat(agentId);
}

void ChatService::stopHeartbeatForAgent(const QString& agentId)
{
    if (m_heartbeatService)
        m_heartbeatService->stopHeartbeat(agentId);
}

void ChatService::triggerHeartbeatForAgent(const QString& agentId, const QString& reason)
{
    if (m_heartbeatService)
        m_heartbeatService->triggerHeartbeat(agentId, reason);
}

QList<ScheduledJob> ChatService::allScheduledJobs() const
{
    return m_schedulerService ? m_schedulerService->allJobs() : QList<ScheduledJob>();
}

bool ChatService::scheduledJobById(const QString& jobId, ScheduledJob* outJob) const
{
    return m_schedulerService && m_schedulerService->jobById(jobId, outJob);
}

QString ChatService::addScheduledJob(const ScheduledJob& job)
{
    return m_schedulerService ? m_schedulerService->addJob(job) : QString();
}

bool ChatService::updateScheduledJob(const QString& jobId, const ScheduledJob& job)
{
    return m_schedulerService && m_schedulerService->updateJob(jobId, job);
}

bool ChatService::removeScheduledJob(const QString& jobId)
{
    return m_schedulerService && m_schedulerService->removeJob(jobId);
}

void ChatService::triggerScheduledJob(const QString& jobId)
{
    if (m_schedulerService)
        m_schedulerService->triggerJob(jobId);
}

void ChatService::setModelConfigPathOverride(const QString& filePath)
{
    if (m_persistence)
        m_persistence->setModelConfigPathOverride(filePath);
}

void ChatService::loadConfig()
{
    m_configService->loadConfig();
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

void ChatService::pollExternalChanges()
{
    if (!m_persistence || !m_sessionManager || !DatabaseManager::instance()->isReady())
        return;

    const QList<Session*> sessions = m_sessionManager->allSessions();
    for (Session* session : sessions) {
        if (!session)
            continue;
        const QString sid = session->id();

        // 获取上次已知的最大 rowid
        const qint64 lastRowId = m_lastSyncRowIds.value(sid, 0);

        // 查询数据库中当前最大 rowid
        const qint64 currentMaxRowId = m_persistence->maxMessageRowId(sid);
        if (currentMaxRowId <= lastRowId)
            continue; // 没有新消息

        // 加载增量消息
        const QList<Message> newMessages = m_persistence->loadNewMessagesFromDb(sid, lastRowId);
        if (newMessages.isEmpty()) {
            m_lastSyncRowIds.insert(sid, currentMaxRowId);
            continue;
        }
        int injectedCount = 0;
        for (const Message& msg : newMessages) {
            // 跳过本进程已知的消息（通过 ID 去重）
            if (session->findMessageById(msg.id))
                continue;

            // 通过 SessionManager 注入，触发 messagePosted 信号 → UI 实时刷新
            // （INSERT OR IGNORE 保证幂等性，不会重复写入 DB）
            m_sessionManager->postMessage(sid, msg);
            ++injectedCount;
        }

        m_lastSyncRowIds.insert(sid, currentMaxRowId);

        if (injectedCount > 0) {
            qDebug() << "[ChatService] 跨进程同步：会话" << sid
                     << "注入" << injectedCount << "条新消息";

            // 通知 UI 刷新消息列表
            QJsonObject syncEvent;
            syncEvent.insert(QStringLiteral("type"), QStringLiteral("sync_messages_injected"));
            syncEvent.insert(QStringLiteral("sessionId"), sid);
            syncEvent.insert(QStringLiteral("count"), injectedCount);
            emit conversationEvent(syncEvent);
        }
    }
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

bool ChatService::renameSessionAndRuntime(const QString& sessionId, const QString& name)
{
    const QString trimmedSessionId = sessionId.trimmed();
    const QString trimmedName = name.trimmed();
    if (trimmedSessionId.isEmpty())
        return false;

    Session* session = m_sessionManager ? m_sessionManager->findById(trimmedSessionId) : nullptr;
    if (session)
        session->setTitle(trimmedName);

    AgentRuntime* runtime = runtimeForSession(trimmedSessionId);
    if (runtime && runtime->identity()) {
        runtime->identity()->setName(trimmedName);
        LLMConfig cfg = runtime->config();
        cfg.userName = trimmedName;
        runtime->setConfig(cfg);
    }
    return true;
}

void ChatService::clearConversationHistory(const QString& sessionId)
{
    const QString trimmedSessionId = sessionId.trimmed();
    if (trimmedSessionId.isEmpty())
        return;

    Session* session = m_sessionManager ? m_sessionManager->findById(trimmedSessionId) : nullptr;
    if (session)
        session->clearMessages();

    AgentRuntime* runtime = runtimeForSession(trimmedSessionId);
    if (runtime && runtime->currentSessionId() == trimmedSessionId)
        runtime->clearHistory();

    m_teammateInjections.remove(trimmedSessionId);
}

bool ChatService::loadSessionsFromDisk()
{
    if (!m_stateRepository)
        return false;

    // 清理现有 Runtime（由 RuntimeManager 管理，这里只清空引用）
    auto& runtimes = m_runtimeManager->runtimes();
    for (AgentRuntime* runtime : runtimes)
        runtime->deleteLater();
    runtimes.clear();
    m_turnManager.clear();
    m_agentActiveSession.clear();
    m_lastSavedMessageCounts.clear();
    m_delegateStartMsByToolKey.clear();
    m_delegateStatsBySession.clear();
    m_toolProgressLastPersistMsByKey.clear();
    m_toolProgressLastDigestByKey.clear();

    if (m_persistence && DatabaseManager::instance()->isReady()) {
        const QString importedMarker = m_persistence->getAppState(QStringLiteral("legacyEventsImported"));
        if (importedMarker.trimmed().isEmpty()) {
            const qint64 beforeCount = m_persistence->eventCountInDb();
            qint64 imported = 0;
            if (m_persistence->importLegacyEventLogsToDb(&imported)) {
                m_persistence->setAppState(QStringLiteral("legacyEventsImported"), QString::number(imported));
                if (imported > 0)
                    qInfo() << "[ChatService] Imported legacy event logs into SQLite:" << imported;
            } else if (beforeCount > 0) {
                m_persistence->setAppState(QStringLiteral("legacyEventsImported"), QStringLiteral("0"));
            }
        }
    }

    const ChatStateRepository::LoadResult loaded = m_stateRepository->loadState(m_runtimeManager->defaultAgentConfig());
    if (!loaded.success)
        return false;

    m_lastSavedMessageCounts = loaded.savedMessageCounts;
    m_currentSessionId = loaded.currentSessionId;
    if (m_persistence && DatabaseManager::instance()->isReady())
        m_persistence->setAppState(QStringLiteral("storageBackend"), QStringLiteral("sqlite"));

    if (m_memoryManager) {
        QString memoryError;
        if (!m_memoryManager->ensureUserMemoryDocument(&memoryError) && !memoryError.isEmpty())
            qWarning() << "[ChatService] user memory init failed after load:" << memoryError;
    }
    if (m_identityManager) {
        const QList<Identity*> agents = m_identityManager->allAgents();
        for (Identity* agent : agents)
            ensureMemoryInitializedForAgent(agent);
    }

    for (auto it = loaded.pendingTurnsBySession.constBegin();
         it != loaded.pendingTurnsBySession.constEnd();
         ++it) {
        SessionPipeline& pipeline = ensurePipeline(it.key());
        for (const TurnTask& turn : it.value())
            pipeline.queue.append(turn);
        tryStartNextTurn(it.key());
    }

    if (loaded.loadedFromLegacyFiles && DatabaseManager::instance()->isReady()) {
        qInfo() << "[ChatService] Legacy file state loaded; backfilling SQLite.";
        saveSessionsToDisk();
        if (m_persistence)
            m_persistence->setAppState(QStringLiteral("legacyStateImported"), QStringLiteral("1"));
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
    const LLMConfig defaultCfg2 = m_runtimeManager->defaultAgentConfig();
    profile->setLlmConfig(defaultCfg2);
    profile->setSystemPrompt(defaultCfg2.systemPrompt);
    profile->setDelegateEnabled(true);
    profile->setAllowedTools(ChatStateRepository::collectToolNamesFrom(m_toolDispatcher));
    Identity* agentIdentity = m_identityManager->createAgent(
        session->title().isEmpty() ? QStringLiteral("TM Agent") : session->title(),
        profile);
    ensureMemoryInitializedForAgent(agentIdentity);
    session->addParticipant(agentIdentity->id());
    return agentIdentity;
}

LLMConfig ChatService::composeConfigForIdentity(Identity* identity) const
{
    return m_runtimeManager->composeConfigForIdentity(identity);
}

QJsonArray ChatService::buildRuntimeHistoryFromMessages(Session* session) const
{
    QJsonArray history;
    if (!session)
        return history;

    QSet<QString> validToolCallIds;
    const QList<Message> messages = session->allMessages();
    QSet<QString> heartbeatTraceIds;
    for (const Message& msg : messages) {
        if (isHeartbeatPromptText(msg.content.text) && !msg.traceId.trimmed().isEmpty())
            heartbeatTraceIds.insert(msg.traceId.trimmed());
    }
    for (const Message& msg : messages) {
        if (msg.status == Message::Status::Cancelled
            || msg.status == Message::Status::Interrupted
            || msg.status == Message::Status::Error) {
            continue;
        }

        const QString content = msg.content.text;
        const QString trimmedContent = content.trimmed();
        const QString traceId = msg.traceId.trimmed();

        if (isHeartbeatPromptText(content))
            continue;
        if (!traceId.isEmpty()
            && heartbeatTraceIds.contains(traceId)
            && isHeartbeatNoChangeReplyText(content)) {
            continue;
        }

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
            if (toolContent.size() > kHistoryToolResultMaxChars) {
                toolContent = toolContent.left(kHistoryToolResultMaxChars)
                    + QStringLiteral("\n...[tool result truncated]...");
            }

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

    // 后处理：确保每个带 tool_calls 的 assistant 消息后都有完整的 tool result
    QJsonArray sanitized;
    for (int i = 0; i < history.size(); ++i) {
        QJsonObject msg = history[i].toObject();
        sanitized.append(msg);

        if (msg.value(QStringLiteral("role")).toString() != QLatin1String("assistant")
            || !msg.contains(QStringLiteral("tool_calls")))
            continue;

        QJsonArray toolCalls = msg[QStringLiteral("tool_calls")].toArray();
        QSet<QString> expectedIds;
        for (const QJsonValue& tc : toolCalls)
            expectedIds.insert(tc.toObject()[QStringLiteral("id")].toString());

        // 收集后续 tool 消息已覆盖的 id（不限于紧邻，跳过中间非 tool 消息）
        QSet<QString> foundIds;
        for (int j = i + 1; j < history.size(); ++j) {
            QJsonObject next = history[j].toObject();
            const QString nextRole = next.value(QStringLiteral("role")).toString();
            if (nextRole == QLatin1String("tool")) {
                foundIds.insert(next[QStringLiteral("tool_call_id")].toString());
            } else if (nextRole == QLatin1String("assistant") && next.contains(QStringLiteral("tool_calls"))) {
                break; // 遇到下一个带 tool_calls 的 assistant 消息，停止搜索
            }
        }

        // 补齐缺失的 tool result
        for (const QString& id : expectedIds) {
            if (!foundIds.contains(id)) {
                QJsonObject placeholder;
                placeholder[QStringLiteral("role")] = QStringLiteral("tool");
                placeholder[QStringLiteral("tool_call_id")] = id;
                placeholder[QStringLiteral("content")] = QStringLiteral("[工具结果不可用]");
                sanitized.append(placeholder);
            }
        }
    }
    return compactHistoryWithBudget(sanitized, kHistoryMaxMessages, kHistoryMaxChars);
}

AgentRuntime* ChatService::ensureRuntimeForAgent(Identity* agentIdentity)
{
    return m_runtimeManager->ensureRuntimeForAgent(agentIdentity);
}

void ChatService::releaseRuntimeIfUnused(const QString& agentIdentityId)
{
    m_runtimeManager->releaseRuntimeIfUnused(agentIdentityId);
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

void ChatService::flushPendingDeltaLog(const QString& sessionId, SessionPipeline* pipeline, const TurnTask* turn, bool force)
{
    if (m_logVerboseStreamEvents || !pipeline || pipeline->pendingDeltaLog.isEmpty())
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int charCount = pipeline->pendingDeltaLog.size();
    const int chunkCount = pipeline->pendingDeltaChunks;
    const qint64 spanMs = pipeline->pendingDeltaStartedAtMs > 0 ? (nowMs - pipeline->pendingDeltaStartedAtMs) : 0;

    if (!force) {
        const bool byChars = charCount >= kDeltaBatchFlushChars;
        const bool byChunks = chunkCount >= kDeltaBatchFlushChunks;
        const bool byInterval = pipeline->lastDeltaFlushedAtMs <= 0
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

    emitPipelineEvent(QStringLiteral("turn_delta_batch"), sessionId, turn, pipeline->pendingDeltaLog, QString(), extra, true);

    pipeline->pendingDeltaLog.clear();
    pipeline->pendingDeltaChunks = 0;
    pipeline->pendingDeltaStartedAtMs = 0;
    pipeline->lastDeltaFlushedAtMs = nowMs;
}

bool ChatService::appendEventLog(const QJsonObject& event) const
{
    return m_persistence && m_persistence->appendEventLog(event);
}

void ChatService::emitPipelineEvent(const QString& type, const QString& sessionId, const TurnTask* turn, const QString& delta, const QString& error, const QJsonObject& extra, bool persistToDisk)
{
    SessionPipeline* pipeline = findPipeline(sessionId);

    QJsonObject event;
    event.insert(QStringLiteral("event_schema_version"), 1);
    event.insert(QStringLiteral("type"), type);
    event.insert(QStringLiteral("sessionId"), sessionId);
    event.insert(QStringLiteral("session_id"), sessionId);
    event.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
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

    if (shouldMirrorEventToIoHistory(type))
        appendRuntimeIoEventEntry(sessionId, type, turn, error, extra);

    emit conversationEvent(event);
    if (persistToDisk && !appendEventLog(event)) {
        const QString logPath = m_persistence
            ? (DatabaseManager::instance()->isReady()
                   ? QStringLiteral("sqlite://events")
                   : QDir(m_persistence->dataRootPath()).filePath(QStringLiteral("logs/events-current.jsonl")))
            : QStringLiteral("<persistence-unavailable>");
        qWarning() << "[ChatService] 事件日志写入失败：" << logPath;
    }
}

void ChatService::appendRuntimeIoEventEntry(const QString& sessionId, const QString& type, const TurnTask* turn, const QString& error, const QJsonObject& extra)
{
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (!runtime)
        return;

    const QString recordedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QJsonObject eventObj;
    eventObj.insert(QStringLiteral("type"), type);
    eventObj.insert(QStringLiteral("session_id"), sessionId);
    eventObj.insert(QStringLiteral("timestamp"), recordedAt);
    if (!error.isEmpty())
        eventObj.insert(QStringLiteral("error"), error);
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        eventObj.insert(it.key(), it.value());

    if (turn) {
        if (!turn->requestTraceId.isEmpty())
            eventObj.insert(QStringLiteral("trace_id"), turn->requestTraceId);
        if (!turn->turnId.isEmpty())
            eventObj.insert(QStringLiteral("turn_id"), turn->turnId);
        if (!turn->runId.isEmpty())
            eventObj.insert(QStringLiteral("run_id"), turn->runId);
    }

    QJsonObject entry;
    entry.insert(QStringLiteral("kind"), QStringLiteral("event"));
    entry.insert(QStringLiteral("recorded_at"), recordedAt);
    const QString requestId = turn
        ? QStringLiteral("event:%1:%2")
              .arg(turn->runId.isEmpty() ? type : turn->runId, type)
        : QStringLiteral("event:%1").arg(type);
    entry.insert(QStringLiteral("request_id"), requestId);
    entry.insert(QStringLiteral("event"), eventObj);

    runtime->appendIoHistoryEntry(sessionId, entry);
}

void ChatService::clearToolProgressCacheForSession(const QString& sessionId)
{
    const QString keyPrefix = sessionId.trimmed() + QStringLiteral("|");
    for (auto it = m_toolProgressLastPersistMsByKey.begin(); it != m_toolProgressLastPersistMsByKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_toolProgressLastPersistMsByKey.erase(it);
        else
            ++it;
    }
    for (auto it = m_toolProgressLastDigestByKey.begin(); it != m_toolProgressLastDigestByKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_toolProgressLastDigestByKey.erase(it);
        else
            ++it;
    }
}

void ChatService::clearDelegateStartsForSession(const QString& sessionId)
{
    const QString keyPrefix = sessionId.trimmed() + QStringLiteral("|");
    for (auto it = m_delegateStartMsByToolKey.begin(); it != m_delegateStartMsByToolKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_delegateStartMsByToolKey.erase(it);
        else
            ++it;
    }
}

void ChatService::tryStartNextTurn(const QString& sessionId)
{
    ChatCoordinatorFactory factory(*this);
    ConversationDispatchCoordinator coordinator(factory.makeDispatchDependencies(), factory.makeDispatchLimits());
    coordinator.tryStartNextTurn(sessionId);
}

void ChatService::enqueueInternalTurn(const QString& sessionId, const QString& content, const QString& clientMessageId)
{
    if (sessionId.isEmpty() || content.isEmpty())
        return;

    // 将队友回复存入注入队列，tryStartNextTurn 构建 runtimeHistory 后会追加进去。
    // 不写入 Session Message 列表，UI 不可见。
    QStringList& injections = m_teammateInjections[sessionId];
    injections.append(content);
    // 保留最近 kMaxTeammateInjections 条，防止无限膨胀
    static constexpr int kMaxTeammateInjections = 20;
    if (injections.size() > kMaxTeammateInjections)
        injections = injections.mid(injections.size() - kMaxTeammateInjections);

    TurnTask turn;
    turn.userContent = content;
    turn.clientMessageId = clientMessageId.isEmpty()
        ? QStringLiteral("internal-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
        : clientMessageId;
    m_turnManager.enqueueTurn(sessionId, turn);
    tryStartNextTurn(sessionId);
}

void ChatService::finalizeTurn(const QString& sessionId, TurnTask* outTurn)
{
    // finalizeTurn 现在由 TurnCompletionCoordinator 内部调用，
    // 但 abortCurrent/abortAndRollback 仍需要直接清理轮次
    m_turnManager.clearActiveTurn(sessionId, outTurn);
    clearDelegateStartsForSession(sessionId);
    clearToolProgressCacheForSession(sessionId);
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
    ChatCoordinatorFactory factory(*this);
    ConversationStreamCoordinator coordinator(factory.makeStreamDependencies());
    coordinator.onRuntimeStreamData(sessionId, data);
}

void ChatService::onRuntimeFinished(const QString& sessionId, const QString& fullContent)
{
    ChatCoordinatorFactory factory(*this);
    TurnCompletionCoordinator coordinator(factory.makeTurnCompletionDependencies());
    coordinator.onRuntimeFinished(sessionId, fullContent);
}

void ChatService::onRuntimeError(const QString& sessionId, const QString& errorMsg)
{
    ChatCoordinatorFactory factory(*this);
    TurnCompletionCoordinator coordinator(factory.makeTurnCompletionDependencies());
    coordinator.onRuntimeError(sessionId, errorMsg);
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
    reportPulseProgress(agentId, QStringLiteral("tool_event"));
    const QString toolName = event.toolName.trimmed();

    {
        ChatCoordinatorFactory factory(*this);
        ToolEventCoordinator coordinator(factory.makeToolEventDependencies());
        coordinator.handleToolEvent(sessionId, activeTurn, event);
    }
}

void ChatService::connectRuntimeSignals(AgentRuntime* runtime)
{
    if (!runtime)
        return;
    ensureAgentPulse(runtime->identityId());
    connect(runtime, &AgentRuntime::streamDataReceived, this, &ChatService::onRuntimeStreamData);
    connect(runtime, &AgentRuntime::finished, this, &ChatService::onRuntimeFinished);
    connect(runtime, &AgentRuntime::errorOccurred, this, &ChatService::onRuntimeError);
    connect(runtime, &AgentRuntime::toolCallsStarted, this, &ChatService::onRuntimeToolCallsStarted);
    connect(runtime, &AgentRuntime::toolEvent, this, &ChatService::onRuntimeToolEvent);
    connect(runtime, &AgentRuntime::reasoningStarted, this, &ChatService::reasoningStarted);
    connect(runtime, &AgentRuntime::reasoningStopped, this, &ChatService::reasoningStopped);
}

bool ChatService::isUserIdentity(const QString& identityId) const
{
    if (!m_identityManager || identityId.trimmed().isEmpty())
        return false;
    Identity* identity = m_identityManager->findById(identityId);
    return identity && identity->isUser();
}

MemoryMaintenanceService ChatService::makeMemoryMaintenanceService()
{
    return MemoryMaintenanceService(
        MemoryMaintenanceService::Dependencies {
            [this](const QString& agentId, QJsonObject* metadata, QString* error) {
                return m_memoryManager
                    && m_memoryManager->rebuildSearchIndex(agentId, metadata, error);
            },
            [this]() { return m_memoryManager && m_memoryManager->reflectionEnabled(); },
            [this]() { return m_memoryManager ? m_memoryManager->reflectionIntervalTurns() : 0; },
            [this](const QString& agentId,
                   const QString& sessionId,
                   const QString& turnId,
                   const QString& traceId,
                   QString* summary,
                   QString* writtenPath,
                   QJsonObject* metadata,
                   QString* error) {
                return m_memoryManager
                    && m_memoryManager->reflectAndScore(
                        agentId, sessionId, turnId, traceId, summary, writtenPath, metadata, error);
            },
            [this](const QString& agentId) {
                return m_memoryRetainedTurnsByAgent.value(agentId.trimmed(), 0);
            },
            [this](const QString& agentId, int retainedTurns) {
                m_memoryRetainedTurnsByAgent.insert(agentId.trimmed(), retainedTurns);
            },
            [this](const QString& sessionId,
                   const QString& type,
                   const TurnTask* turn,
                   const QString& delta,
                   const QString& error,
                   const QJsonObject& extra,
                   bool persistToDisk) {
                emitPipelineEvent(type, sessionId, turn, delta, error, extra, persistToDisk);
            }
        });
}

void ChatService::updateTaskStateForSession(const QString& sessionId, const QString& state, const TurnTask* turn, const QJsonObject& extra)
{
    if (!m_taskStateService || sessionId.trimmed().isEmpty())
        return;

    QJsonObject patch = extra;
    patch.insert(QStringLiteral("state"), state);

    const QString agentId = agentIdentityIdForSession(sessionId).trimmed();
    if (!agentId.isEmpty())
        patch.insert(QStringLiteral("agent_id"), agentId);

    if (turn) {
        if (!turn->requestTraceId.trimmed().isEmpty())
            patch.insert(QStringLiteral("trace_id"), turn->requestTraceId.trimmed());
        if (!turn->turnId.trimmed().isEmpty())
            patch.insert(QStringLiteral("turn_id"), turn->turnId.trimmed());
        if (!turn->runId.trimmed().isEmpty())
            patch.insert(QStringLiteral("run_id"), turn->runId.trimmed());
    }

    QJsonObject mergedState;
    if (!m_taskStateService->updateState(sessionId, patch, &mergedState))
        return;

    emitPipelineEvent(QStringLiteral("task_state.updated"), sessionId, turn, QString(), QString(), mergedState);
}

void ChatService::clearTaskStateForSession(const QString& sessionId)
{
    if (!m_taskStateService || sessionId.trimmed().isEmpty())
        return;
    m_taskStateService->clearState(sessionId);
}

// P0: 子 Agent 完成自动通知
void ChatService::onDelegateJobSettled(const QString& jobId, const QString& ownerAgentId, bool success, const QString& result)
{
    ChatCoordinatorFactory factory(*this);
    BackgroundTaskCoordinator coordinator(factory.makeBackgroundTaskDependencies());
    coordinator.onDelegateJobSettled(jobId, ownerAgentId, success, result);
}

void ChatService::onHeartbeatTriggered(const QString& agentId, const QString& reason)
{
    if (!m_identityManager)
        return;

    Identity* agent = m_identityManager->findById(agentId);
    if (!agent || !agent->isAgent())
        return;

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return;
    HeartbeatStateStore stateStore(
        HeartbeatStateStore::Dependencies {
            [this](const QString& key) {
                return m_persistence ? m_persistence->getAppState(key) : QString();
            },
            [this](const QString& key, const QString& value) {
                return m_persistence ? m_persistence->setAppState(key, value) : false;
            },
            [this](const QString& path) {
                return m_persistence ? m_persistence->readJsonObject(path) : QJsonObject();
            },
            [this]() {
                return m_persistence ? m_persistence->agentsDirPath() : QString();
            },
            []() { return DatabaseManager::instance()->isReady(); }
        });

    const QString reasonLabel = reason.trimmed().isEmpty()
        ? QStringLiteral("interval")
        : reason.trimmed();
    HeartbeatConfig hbCfg;
    if (m_heartbeatService)
        hbCfg = m_heartbeatService->configForAgent(trimmedAgentId);
    hbCfg.snapshotSignals = normalizeHeartbeatSignals(hbCfg.snapshotSignals);
    const QSet<QString> enabledSignals(hbCfg.snapshotSignals.begin(), hbCfg.snapshotSignals.end());
    const bool watchProvider = enabledSignals.contains(QStringLiteral("provider_status"));
    const bool watchDelegate = enabledSignals.contains(QStringLiteral("delegate_jobs"));
    const bool watchPulse = enabledSignals.contains(QStringLiteral("pulse_state"));
    const bool watchScheduler = enabledSignals.contains(QStringLiteral("scheduler_jobs"));
    const bool watchMemory = enabledSignals.contains(QStringLiteral("memory_progress"));

    QString providerId;
    bool providerDown = false;
    if (watchProvider && m_healthMonitor) {
        const LLMConfig cfg = composeConfigForIdentity(agent);
        providerId = ModelFactory::resolveInstanceId(cfg);
        providerDown = (!providerId.isEmpty() && m_healthMonitor->isProviderDown(providerId));
    }

    const QList<DelegateTaskScheduler::JobInfo> activeJobs = DelegateTaskScheduler::instance()->listJobs(trimmedAgentId, true, 50);

    int schedulerEnabledJobs = 0;
    QDateTime schedulerNextFireAtUtc;
    if (watchPulse) {
        ensureAgentPulse(trimmedAgentId);
    }
    QString pulseState;
    if (watchPulse) {
        AgentPulse* pulse = m_agentPulseRegistry ? m_agentPulseRegistry->find(trimmedAgentId) : nullptr;
        if (pulse)
            pulseState = pulseStateToString(pulse->currentState());
    }
    if (watchScheduler && m_schedulerService) {
        const QList<ScheduledJob> jobs = m_schedulerService->allJobs();
        for (const ScheduledJob& job : jobs) {
            if (job.agentId.trimmed() != trimmedAgentId)
                continue;
            if (job.enabled)
                ++schedulerEnabledJobs;
            if (job.nextFireAtUtc.isValid()
                && (!schedulerNextFireAtUtc.isValid() || job.nextFireAtUtc < schedulerNextFireAtUtc)) {
                schedulerNextFireAtUtc = job.nextFireAtUtc;
            }
        }
    }
    qint64 memoryDocSizeBytes = -1;
    if (watchMemory) {
        const QString memoryMdPath = QDir(QDir(m_persistence ? m_persistence->agentsDirPath() : QString())
                                              .filePath(trimmedAgentId))
                                         .filePath(QStringLiteral("memory.md"));
        if (!memoryMdPath.trimmed().isEmpty() && QFile::exists(memoryMdPath))
            memoryDocSizeBytes = QFileInfo(memoryMdPath).size();
    }
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    HeartbeatRuntimeState& runtimeState = m_heartbeatRuntimeByAgent[trimmedAgentId];
    if (!runtimeState.loaded)
        stateStore.load(trimmedAgentId, &runtimeState);

    HeartbeatSnapshotCoordinator::Inputs snapshotInputs;
    snapshotInputs.agentId = trimmedAgentId;
    snapshotInputs.reason = reasonLabel;
    snapshotInputs.config = hbCfg;
    snapshotInputs.providerId = providerId;
    snapshotInputs.providerDown = providerDown;
    snapshotInputs.activeJobs = activeJobs;
    snapshotInputs.pulseState = pulseState;
    snapshotInputs.schedulerEnabledJobs = schedulerEnabledJobs;
    snapshotInputs.schedulerNextFireAtUtc = schedulerNextFireAtUtc;
    snapshotInputs.memoryRetainedTurns = m_memoryRetainedTurnsByAgent.value(trimmedAgentId, 0);
    snapshotInputs.memoryDocSizeBytes = memoryDocSizeBytes;
    snapshotInputs.runtimeState.hasSnapshot = runtimeState.hasSnapshot;
    snapshotInputs.runtimeState.stateObj = runtimeState.stateObj;
    snapshotInputs.runtimeState.lastSnapshotObj = runtimeState.lastSnapshotObj;
    snapshotInputs.runtimeState.lastSnapshotDigest = runtimeState.lastSnapshotDigest;
    snapshotInputs.runtimeState.lastNotifyAtUtc = runtimeState.lastNotifyAtUtc;
    snapshotInputs.runtimeState.lastPersistAtUtc = runtimeState.lastPersistAtUtc;
    snapshotInputs.nowUtc = nowUtc;

    const HeartbeatSnapshotCoordinator::Result snapshotResult =
        HeartbeatSnapshotCoordinator::evaluate(snapshotInputs);
    if (!snapshotResult.valid)
        return;

    runtimeState.hasSnapshot = snapshotResult.runtimeState.hasSnapshot;
    runtimeState.stateObj = snapshotResult.runtimeState.stateObj;
    runtimeState.lastSnapshotObj = snapshotResult.runtimeState.lastSnapshotObj;
    runtimeState.lastSnapshotDigest = snapshotResult.runtimeState.lastSnapshotDigest;
    runtimeState.lastNotifyAtUtc = snapshotResult.runtimeState.lastNotifyAtUtc;
    runtimeState.lastPersistAtUtc = snapshotResult.runtimeState.lastPersistAtUtc;

    bool shouldPersistState = snapshotResult.shouldPersistState;
    auto persistStateIfNeeded = [&](bool forcePersist) mutable {
        const bool doPersist = forcePersist || shouldPersistState;
        stateStore.persist(trimmedAgentId, &runtimeState, nowUtc, doPersist);
    };
    const QJsonObject triggeredExtra = snapshotResult.triggeredExtra;
    emitPipelineEvent(QStringLiteral("heartbeat.triggered"), QString(), nullptr, QString(), QString(), triggeredExtra);

    if (providerDown) {
        persistStateIfNeeded(false);
        QJsonObject extra = triggeredExtra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("provider_down"));
        emitPipelineEvent(QStringLiteral("heartbeat.skipped"), QString(), nullptr, QString(), QStringLiteral("provider_down"), extra);
        return;
    }

    if (!snapshotResult.shouldNotify) {
        persistStateIfNeeded(false);
        QJsonObject completeExtra = triggeredExtra;
        completeExtra.insert(QStringLiteral("silent"), true);
        completeExtra.insert(QStringLiteral("silent_reason"), snapshotResult.skipReason);
        emitPipelineEvent(QStringLiteral("heartbeat.completed"), QString(), nullptr, QString(), QString(), completeExtra);
        return;
    }

    ChatCoordinatorFactory factory(*this);
    const PrimarySessionResolver resolver = factory.makePrimarySessionResolver();
    const QString sessionId = resolver.resolveForAgent(trimmedAgentId, true, false, QStringLiteral("heartbeat"));
    if (sessionId.isEmpty()) {
        persistStateIfNeeded(false);
        QJsonObject extra = triggeredExtra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("no_session"));
        emitPipelineEvent(QStringLiteral("heartbeat.skipped"), QString(), nullptr, QString(), QStringLiteral("no_session"), extra);
        return;
    }
    HeartbeatDispatchCoordinator coordinator(
        factory.makeHeartbeatDispatchDependencies(runtimeState, shouldPersistState, nowUtc));
    coordinator.dispatch(
        trimmedAgentId,
        sessionId,
        reasonLabel,
        snapshotResult.forceInteractive,
        snapshotResult.hasChange,
        watchDelegate,
        watchProvider,
        watchPulse,
        providerDown,
        providerId,
        activeJobs,
        triggeredExtra);
}

void ChatService::onScheduledJobTriggered(const QString& jobId, const QString& jobName)
{
    if (!m_schedulerService || !m_identityManager)
        return;

    ChatCoordinatorFactory factory(*this);
    BackgroundTaskCoordinator coordinator(factory.makeBackgroundTaskDependencies());
    coordinator.onScheduledJobTriggered(jobId, jobName);
}

void ChatService::ensureAgentPulse(const QString& agentId)
{
    if (m_agentPulseRegistry)
        m_agentPulseRegistry->ensure(agentId);
}

void ChatService::reportPulseProgress(const QString& agentId, const QString& summary)
{
    if (m_agentPulseRegistry)
        m_agentPulseRegistry->reportProgress(agentId, summary);
}

ToolResult ChatService::executeMemoryWriteTool(const QJsonObject& args)
{
    return makeMemoryToolWriteService().execute(args);
}

MemoryToolWriteService ChatService::makeMemoryToolWriteService()
{
    MemoryMaintenanceService memoryMaintenance = makeMemoryMaintenanceService();
    return MemoryToolWriteService(
        MemoryToolWriteService::Dependencies {
            [this](const QString& agentId) {
                return m_agentActiveSession.value(agentId).trimmed();
            },
            [this](const QString& agentId) {
                ChatCoordinatorFactory factory(*this);
                const PrimarySessionResolver resolver = factory.makePrimarySessionResolver();
                return resolver.resolveForAgent(
                    agentId, false, false, QStringLiteral("memory_write"));
            },
            [this](const QString& sessionId) {
                return m_turnManager.activeTurn(sessionId);
            },
            [this](const QString& agentId,
                   const QString& sessionId,
                   const QString& turnId,
                   const QString& traceId,
                   const QString& memoryText,
                   const QString& reason,
                   QString* summary,
                   QString* writtenPath,
                   QJsonObject* metadata,
                   QString* error) {
                return m_memoryManager
                    && m_memoryManager->rememberToolRequested(agentId,
                                                              sessionId,
                                                              turnId,
                                                              traceId,
                                                              memoryText,
                                                              reason,
                                                              summary,
                                                              writtenPath,
                                                              metadata,
                                                              error);
            },
            [this](const QString& sessionId,
                   const QString& type,
                   const TurnTask* turn,
                   const QString& delta,
                   const QString& error,
                   const QJsonObject& extra,
                   bool persistToDisk) {
                emitPipelineEvent(type, sessionId, turn, delta, error, extra, persistToDisk);
            },
            [memoryMaintenance](const QString& sessionId,
                                const QString& agentId,
                                const TurnTask* turn,
                                const QString& reason,
                                const QString& sourcePath,
                                const QJsonObject& sourceMetadata) {
                memoryMaintenance.refreshIndexAndEmit(
                    sessionId, agentId, turn, reason, sourcePath, sourceMetadata);
            }
        });
}

void ChatService::ensureMemoryInitializedForAgent(Identity* agentIdentity)
{
    if (!m_memoryManager || !agentIdentity || !agentIdentity->isAgent())
        return;

    if (m_persistence) {
        const QString agentId = agentIdentity->id().trimmed();
        const QString workspacePath = QDir(
                                          m_persistence->agentsDirPath())
                                          .filePath(agentId + QStringLiteral("/workspace"));
        if (!QDir().mkpath(workspacePath)) {
            qWarning() << "[ChatService] agent workspace init failed:"
                       << agentId
                       << workspacePath;
        }

        const QString heartbeatMdPath = m_persistence->agentHeartbeatInstructionPath(agentId);
        if (!QFile::exists(heartbeatMdPath)) {
            HeartbeatPromptBuilder::repairInstructionFileIfNeeded(heartbeatMdPath);
            QFile file(heartbeatMdPath);
            if (!file.exists()) {
                if (!QDir().mkpath(QFileInfo(heartbeatMdPath).absolutePath()))
                    qWarning() << "[ChatService] heartbeat template dir init failed:" << heartbeatMdPath;
                else if (file.open(QFile::WriteOnly | QFile::Text)) {
                    const QByteArray bytes = HeartbeatPromptBuilder::defaultTemplate().toUtf8();
                    file.write(bytes);
                    file.close();
                }
            }
        } else {
            HeartbeatPromptBuilder::repairInstructionFileIfNeeded(heartbeatMdPath);
        }

        const QString heartbeatCfgPath = m_persistence->agentHeartbeatConfigPath(agentId);
        if (!QFile::exists(heartbeatCfgPath)) {
            QJsonObject cfg;
            cfg.insert(QStringLiteral("enabled"), true);
            cfg.insert(QStringLiteral("intervalMs"), 30 * 60 * 1000);
            cfg.insert(QStringLiteral("coalesceMs"), 250);
            cfg.insert(QStringLiteral("duplicateWindowMs"), 24 * 60 * 60 * 1000);
            cfg.insert(QStringLiteral("silentWhenNoChange"), true);
            cfg.insert(QStringLiteral("notifyOnChangeOnly"), true);
            cfg.insert(QStringLiteral("notifyMinIntervalMs"), 30 * 60 * 1000);
            cfg.insert(QStringLiteral("persistStateOnNoChange"), false);
            cfg.insert(QStringLiteral("statePersistIntervalMs"), 60 * 1000);
            QJsonArray snapshotSignals;
            snapshotSignals.append(QStringLiteral("provider_status"));
            snapshotSignals.append(QStringLiteral("delegate_jobs"));
            snapshotSignals.append(QStringLiteral("pulse_state"));
            cfg.insert(QStringLiteral("snapshotSignals"), snapshotSignals);
            cfg.insert(QStringLiteral("heartbeatPath"), heartbeatMdPath);
            QJsonObject activeHours;
            activeHours.insert(QStringLiteral("start"), QStringLiteral("08:00"));
            activeHours.insert(QStringLiteral("end"), QStringLiteral("23:00"));
            activeHours.insert(QStringLiteral("timezone"), QStringLiteral("Asia/Shanghai"));
            cfg.insert(QStringLiteral("activeHours"), activeHours);
            m_persistence->writeJsonObject(heartbeatCfgPath, cfg);
        }
    }

    QString memoryError;
    if (!m_memoryManager->initializeForAgent(agentIdentity, &memoryError) && !memoryError.isEmpty()) {
        qWarning() << "[ChatService] agent memory init failed:"
                   << agentIdentity->id()
                   << memoryError;
    }
}

void ChatService::saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId)
{
    m_configService->saveTabState(openAgentIds, activeIdentityId);
}

ChatService::TabState ChatService::loadTabState() const
{
    const ConfigService::TabState cs = m_configService->loadTabState();
    TabState state;
    state.openAgentIds = cs.openAgentIds;
    state.activeIdentityId = cs.activeIdentityId;
    return state;
}
