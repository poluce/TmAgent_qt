#include "WorkspaceService.h"

#include "ApplicationServices.h"
#include "ConversationService.h"
#include "GovernanceService.h"
#include "MemoryService.h"
#include "AgentRuntime.h"
#include "ChatStateRepository.h"
#include "ConfigService.h"
#include "RuntimeManager.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "llm/LLMTypes.h"
#include <QDebug>
#include <QDir>
#include <QTimer>

WorkspaceService::WorkspaceService(ApplicationServices& app)
    : m_app(app)
    , m_stateRepository(new ChatStateRepository())
{
}

WorkspaceService::~WorkspaceService() = default;

void WorkspaceService::initializeStateRepository()
{
    if (!m_stateRepository)
        return;
    m_stateRepository->setDependencies(m_app.m_identityManager,
                                       m_app.m_sessionManager,
                                       m_app.m_governanceService
                                           ? m_app.m_governanceService->toolDispatcher()
                                           : nullptr,
                                       m_app.m_persistence.get());
}

void WorkspaceService::startExternalSyncTimer()
{
    if (m_syncTimer)
        return;
    m_syncTimer = new QTimer(&m_app);
    m_syncTimer->setInterval(5000);
    QObject::connect(m_syncTimer, &QTimer::timeout, &m_app, [this]() { pollExternalChanges(); });
    m_syncTimer->start();
    qDebug() << "[ApplicationServices] 跨进程同步轮询已启动（间隔 5s）";
}

const QString& WorkspaceService::currentSessionIdValue() const
{
    return m_currentSessionId;
}

Session* WorkspaceService::createNewSession(const QString& agentName)
{
    const QString userId = m_app.m_identityManager->userIdentity()->id();

    auto* profile = new IdentityProfile();
    const LLMConfig defaultCfg = m_app.m_conversationService->runtimeManager()->defaultAgentConfig();
    profile->setLlmConfig(defaultCfg);
    profile->setSystemPrompt(defaultCfg.systemPrompt);
    profile->setDelegateEnabled(true);
    profile->setAllowedTools(ChatStateRepository::collectToolNamesFrom(
        m_app.m_governanceService ? m_app.m_governanceService->toolDispatcher() : nullptr));

    const QString name = agentName.isEmpty() ? QStringLiteral("TM Agent") : agentName;
    Identity* agentIdentity = m_app.m_identityManager->createAgent(name, profile);
    if (m_app.m_memoryService)
        m_app.m_memoryService->ensureMemoryInitializedForAgent(agentIdentity);
    if (agentIdentity && m_app.m_memoryService) {
        const QString agentId = agentIdentity->id().trimmed();
        if (!agentId.isEmpty()) {
            m_app.m_memoryService->ensureAgentPulse(agentId);
            m_app.m_memoryService->startAgentHeartbeat(agentId);
        }
    }

    Session* session = m_app.m_sessionManager->createPrivateSession(userId, agentIdentity->id());
    session->setTitle(name);

    m_currentSessionId = session->id();
    emit m_app.sessionCreated(session->id());
    saveSessionsToDisk();
    return session;
}

Session* WorkspaceService::createSessionForIdentity(const QString& identityId, const QString& title)
{
    const QString userId =
        m_app.m_identityManager ? m_app.m_identityManager->userIdentity()->id() : QString();
    return createSessionForIdentityAs(userId, identityId, title);
}

Session* WorkspaceService::createSessionForIdentityAs(const QString& actorIdentityId,
                                                      const QString& identityId,
                                                      const QString& title)
{
    if (!canIdentityManageSessions(actorIdentityId)) {
        qWarning() << "[ApplicationServices] 拒绝创建会话，actor 无权限:" << actorIdentityId
                   << "target:" << identityId;
        return nullptr;
    }

    Identity* identity = m_app.m_identityManager->findById(identityId);
    if (!identity)
        return nullptr;

    if (identity->isUser())
        return createNewSession(title);

    if (m_app.m_memoryService)
        m_app.m_memoryService->ensureMemoryInitializedForAgent(identity);
    const QString agentId = identity->id().trimmed();
    if (!agentId.isEmpty() && m_app.m_memoryService) {
        m_app.m_memoryService->ensureAgentPulse(agentId);
        m_app.m_memoryService->startAgentHeartbeat(agentId);
    }

    const QString userId = m_app.m_identityManager->userIdentity()->id();
    Session* session = m_app.m_sessionManager->createPrivateSession(userId, identityId);
    session->setTitle(title.isEmpty() ? identity->name() : title);

    emit m_app.sessionCreated(session->id());
    saveSessionsToDisk();
    return session;
}

QList<Session*> WorkspaceService::sessionsForIdentity(const QString& identityId) const
{
    return m_app.m_sessionManager->sessionsForIdentity(identityId);
}

void WorkspaceService::removeSession(const QString& sessionId)
{
    const QString userId =
        m_app.m_identityManager ? m_app.m_identityManager->userIdentity()->id() : QString();
    removeSessionAs(userId, sessionId);
}

bool WorkspaceService::removeSessionAs(const QString& actorIdentityId, const QString& sessionId)
{
    if (!canIdentityManageSessions(actorIdentityId)) {
        qWarning() << "[ApplicationServices] 拒绝删除会话，actor 无权限:" << actorIdentityId
                   << "session:" << sessionId;
        return false;
    }

    Session* session = m_app.m_sessionManager->findById(sessionId);
    if (!session)
        return false;

    const QString agentId =
        m_app.m_conversationService ? m_app.m_conversationService->agentIdentityIdForSession(sessionId)
                                    : QString();
    AgentRuntime* runtime =
        m_app.m_conversationService ? m_app.m_conversationService->runtimeForSession(sessionId)
                                    : nullptr;
    if (runtime && runtime->isStreaming() && m_app.m_conversationService
        && m_app.m_conversationService->activeSessionByAgent().value(agentId) == sessionId) {
        runtime->abort();
        m_app.m_conversationService->activeSessionByAgent().remove(agentId);
    }

    if (m_app.m_conversationService) {
        m_app.m_conversationService->turnManager().removePipeline(sessionId);
        m_app.m_conversationService->clearTaskStateForSession(sessionId);
        m_app.m_conversationService->delegateStatsBySession().remove(sessionId);
        m_app.m_conversationService->clearToolProgressCacheForSession(sessionId);
        m_app.m_conversationService->clearDelegateStartsForSession(sessionId);
    }

    m_app.m_sessionManager->removeSession(sessionId);
    if (!agentId.isEmpty() && m_app.m_conversationService) {
        m_app.m_conversationService->tryStartNextTurnForAgent(agentId);
        m_app.m_conversationService->releaseRuntimeIfUnused(agentId);
    }

    if (m_currentSessionId == sessionId)
        m_currentSessionId.clear();

    if (m_app.m_persistence)
        m_app.m_persistence->removeSessionFromDb(sessionId);

    emit m_app.sessionRemoved(sessionId);
    saveSessionsToDisk();
    return true;
}

void WorkspaceService::switchSession(const QString& sessionId)
{
    if (sessionId == m_currentSessionId)
        return;
    m_currentSessionId = sessionId;

    AgentRuntime* runtime =
        m_app.m_conversationService ? m_app.m_conversationService->runtimeForSession(sessionId)
                                    : nullptr;
    if (runtime && !runtime->isStreaming())
        runtime->switchToSession(sessionId);
}

QString WorkspaceService::currentSessionId() const
{
    return m_currentSessionId;
}

QString WorkspaceService::agentDisplayNameForSession(const QString& sessionId) const
{
    Session* session = m_app.m_sessionManager->findById(sessionId);
    if (!session)
        return QStringLiteral("TM Agent");

    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_app.m_identityManager->findById(pid);
        if (identity && identity->isAgent())
            return identity->name();
    }

    if (!session->title().isEmpty())
        return session->title();

    return QStringLiteral("TM Agent");
}

bool WorkspaceService::canIdentityManageSessions(const QString& identityId) const
{
    if (!m_app.m_identityManager || identityId.trimmed().isEmpty())
        return false;
    Identity* identity = m_app.m_identityManager->findById(identityId);
    return identity && identity->isUser();
}

bool WorkspaceService::canIdentitySendMessage(const QString& identityId, const QString& sessionId) const
{
    if (!canIdentityManageSessions(identityId))
        return false;
    if (sessionId.isEmpty())
        return true;
    return m_app.m_sessionManager && m_app.m_sessionManager->findById(sessionId) != nullptr;
}

bool WorkspaceService::canIdentityManageGlobalConfig(const QString& identityId) const
{
    return canIdentityManageSessions(identityId);
}

void WorkspaceService::appendSessionMessageToDisk(const QString& sessionId, const Message& msg)
{
    if (!m_app.m_persistence || sessionId.trimmed().isEmpty() || !msg.isValid())
        return;
    if (!m_app.m_persistence->appendSessionMessage(sessionId,
                                                   m_app.m_persistence->messageToJson(msg))) {
        qWarning() << "[ApplicationServices] 消息追加写入失败，sessionId=" << sessionId
                   << "messageId=" << msg.id;
        return;
    }

    Session* session = m_app.m_sessionManager ? m_app.m_sessionManager->findById(sessionId) : nullptr;
    if (session)
        m_lastSavedMessageCounts.insert(sessionId, session->messageCount());
}

void WorkspaceService::pollExternalChanges()
{
    if (!m_app.m_persistence || !m_app.m_sessionManager || !DatabaseManager::instance()->isReady())
        return;

    const QList<Session*> sessions = m_app.m_sessionManager->allSessions();
    for (Session* session : sessions) {
        if (!session)
            continue;
        const QString sid = session->id();
        const qint64 lastRowId = m_lastSyncRowIds.value(sid, 0);
        const qint64 currentMaxRowId = m_app.m_persistence->maxMessageRowId(sid);
        if (currentMaxRowId <= lastRowId)
            continue;

        const QList<Message> newMessages = m_app.m_persistence->loadNewMessagesFromDb(sid, lastRowId);
        if (newMessages.isEmpty()) {
            m_lastSyncRowIds.insert(sid, currentMaxRowId);
            continue;
        }
        int injectedCount = 0;
        for (const Message& msg : newMessages) {
            if (session->findMessageById(msg.id))
                continue;
            m_app.m_sessionManager->postMessage(sid, msg);
            ++injectedCount;
        }

        m_lastSyncRowIds.insert(sid, currentMaxRowId);

        if (injectedCount > 0) {
            qDebug() << "[ApplicationServices] 跨进程同步：会话" << sid << "注入" << injectedCount
                     << "条新消息";
            QJsonObject syncEvent;
            syncEvent.insert(QStringLiteral("type"), QStringLiteral("sync_messages_injected"));
            syncEvent.insert(QStringLiteral("sessionId"), sid);
            syncEvent.insert(QStringLiteral("count"), injectedCount);
            emit m_app.conversationEvent(syncEvent);
        }
    }
}

void WorkspaceService::saveSessionsToDisk()
{
    if (!m_stateRepository)
        return;
    m_lastSavedMessageCounts = m_stateRepository->saveState(
        m_currentSessionId,
        m_lastSavedMessageCounts,
        [this](const QString& sid) -> const SessionPipeline* {
            return m_app.m_conversationService ? m_app.m_conversationService->findPipeline(sid)
                                               : nullptr;
        });
}

bool WorkspaceService::loadSessionsFromDisk()
{
    if (!m_stateRepository)
        return false;

    auto& runtimes = m_app.m_conversationService->runtimeManager()->runtimes();
    for (AgentRuntime* runtime : runtimes)
        runtime->deleteLater();
    runtimes.clear();
    m_app.m_conversationService->turnManager().clear();
    m_app.m_conversationService->activeSessionByAgent().clear();
    m_lastSavedMessageCounts.clear();
    m_app.m_conversationService->delegateStartMsByToolKey().clear();
    m_app.m_conversationService->delegateStatsBySession().clear();
    m_app.m_conversationService->toolProgressLastPersistMsByKey().clear();
    m_app.m_conversationService->toolProgressLastDigestByKey().clear();

    if (m_app.m_persistence && DatabaseManager::instance()->isReady()) {
        const QString importedMarker =
            m_app.m_persistence->getAppState(QStringLiteral("legacyEventsImported"));
        if (importedMarker.trimmed().isEmpty()) {
            const qint64 beforeCount = m_app.m_persistence->eventCountInDb();
            qint64 imported = 0;
            if (m_app.m_persistence->importLegacyEventLogsToDb(&imported)) {
                m_app.m_persistence->setAppState(QStringLiteral("legacyEventsImported"),
                                                 QString::number(imported));
                if (imported > 0)
                    qInfo() << "[ApplicationServices] Imported legacy event logs into SQLite:"
                            << imported;
            } else if (beforeCount > 0) {
                m_app.m_persistence->setAppState(QStringLiteral("legacyEventsImported"),
                                                 QStringLiteral("0"));
            }
        }
    }

    const ChatStateRepository::LoadResult loaded =
        m_stateRepository->loadState(m_app.m_conversationService->runtimeManager()->defaultAgentConfig());
    if (!loaded.success)
        return false;

    m_lastSavedMessageCounts = loaded.savedMessageCounts;
    m_currentSessionId = loaded.currentSessionId;
    if (m_app.m_persistence && DatabaseManager::instance()->isReady()) {
        m_app.m_persistence->setAppState(QStringLiteral("storageBackend"), QStringLiteral("sqlite"));
    }

    if (m_app.m_memoryService)
        m_app.m_memoryService->ensureUserMemoryDocument();
    if (m_app.m_identityManager && m_app.m_memoryService) {
        const QList<Identity*> agents = m_app.m_identityManager->allAgents();
        for (Identity* agent : agents)
            m_app.m_memoryService->ensureMemoryInitializedForAgent(agent);
    }

    for (auto it = loaded.pendingTurnsBySession.constBegin();
         it != loaded.pendingTurnsBySession.constEnd();
         ++it) {
        SessionPipeline& pipeline = m_app.m_conversationService->ensurePipeline(it.key());
        for (const TurnTask& turn : it.value())
            pipeline.queue.append(turn);
        m_app.m_conversationService->tryStartNextTurn(it.key());
    }

    if (loaded.loadedFromLegacyFiles && DatabaseManager::instance()->isReady()) {
        qInfo() << "[ApplicationServices] Legacy file state loaded; backfilling SQLite.";
        saveSessionsToDisk();
        if (m_app.m_persistence) {
            m_app.m_persistence->setAppState(QStringLiteral("legacyStateImported"),
                                             QStringLiteral("1"));
        }
    }

    return true;
}

void WorkspaceService::saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId)
{
    if (m_app.m_governanceService && m_app.m_governanceService->configService()) {
        m_app.m_governanceService->configService()->saveTabState(openAgentIds, activeIdentityId);
    }
}

ChatTabState WorkspaceService::loadTabState() const
{
    ChatTabState state;
    if (!m_app.m_governanceService || !m_app.m_governanceService->configService())
        return state;
    const ConfigService::TabState cs = m_app.m_governanceService->configService()->loadTabState();
    state.openAgentIds = cs.openAgentIds;
    state.activeIdentityId = cs.activeIdentityId;
    return state;
}
