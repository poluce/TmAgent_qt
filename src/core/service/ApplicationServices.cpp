#include "ApplicationServices.h"

#include "ConversationService.h"
#include "GovernanceService.h"
#include "MemoryService.h"
#include "WorkspaceService.h"
#include "AgentRuntime.h"
#include "ConfigService.h"
#include "HeartbeatService.h"
#include "RuntimeManager.h"
#include "SchedulerService.h"
#include "TaskStateService.h"
#include "TeammateManager.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/backend/BackendPluginManager.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "core/tools/MemoryTool.h"
#include "core/tools/SchedulerTool.h"
#include <QProcessEnvironment>

namespace {
bool envFlagEnabled(const char* key)
{
    const QString raw =
        QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(key)).trimmed().toLower();
    return raw == QLatin1String("1") || raw == QLatin1String("true")
        || raw == QLatin1String("yes") || raw == QLatin1String("on");
}
} // namespace

ApplicationServices::ApplicationServices(QObject* parent)
    : QObject(parent)
    , m_persistence(new ChatPersistenceService())
    , m_logVerboseStreamEvents(envFlagEnabled("TMAGENT_LOG_STREAM_EVENTS_VERBOSE"))
    , m_eventHub(new AppEventHub(this))
    , m_workspaceService(new WorkspaceService(*this))
    , m_conversationService(new ConversationService(*this))
    , m_governanceService(new GovernanceService(*this))
    , m_memoryService(new MemoryService(*this))
{
    QObject::connect(this,
                     &ApplicationServices::conversationEvent,
                     m_eventHub.get(),
                     &AppEventHub::conversationEvent);
    QObject::connect(this,
                     &ApplicationServices::conversationEvent,
                     this,
                     [this](const QJsonObject& event) {
                         if (m_memoryService)
                             m_memoryService->onConversationEvent(event);
                     });
    QObject::connect(this,
                     &ApplicationServices::streamDataReceived,
                     m_eventHub.get(),
                     &AppEventHub::streamDataReceived);
    QObject::connect(this, &ApplicationServices::finished, m_eventHub.get(), &AppEventHub::finished);
    QObject::connect(this,
                     &ApplicationServices::errorOccurred,
                     m_eventHub.get(),
                     &AppEventHub::errorOccurred);
    QObject::connect(this,
                     &ApplicationServices::toolCallsStarted,
                     m_eventHub.get(),
                     &AppEventHub::toolCallsStarted);
    QObject::connect(this, &ApplicationServices::toolEvent, m_eventHub.get(), &AppEventHub::toolEvent);
    QObject::connect(this,
                     &ApplicationServices::reasoningStarted,
                     m_eventHub.get(),
                     &AppEventHub::reasoningStarted);
    QObject::connect(this,
                     &ApplicationServices::reasoningStopped,
                     m_eventHub.get(),
                     &AppEventHub::reasoningStopped);
    QObject::connect(this,
                     &ApplicationServices::sessionCreated,
                     m_eventHub.get(),
                     &AppEventHub::sessionCreated);
    QObject::connect(this,
                     &ApplicationServices::sessionRemoved,
                     m_eventHub.get(),
                     &AppEventHub::sessionRemoved);
    QObject::connect(this,
                     &ApplicationServices::configLoaded,
                     m_eventHub.get(),
                     &AppEventHub::configLoaded);
    QObject::connect(this,
                     &ApplicationServices::modelCatalogUpdated,
                     m_eventHub.get(),
                     &AppEventHub::modelCatalogUpdated);

    QObject::connect(m_conversationService->runtimeManager(),
                     &RuntimeManager::runtimeCreated,
                     this,
                     [this](AgentRuntime* runtime) { m_conversationService->connectRuntimeSignals(runtime); });
    QObject::connect(m_governanceService->configService(),
                     &ConfigService::configLoaded,
                     this,
                     &ApplicationServices::configLoaded);
    if (m_memoryService->schedulerService()) {
        QObject::connect(m_memoryService->schedulerService(),
                         &SchedulerService::jobFired,
                         this,
                         [this](const QString& jobId, const QString& jobName) {
                             m_memoryService->onScheduledJobTriggered(jobId, jobName);
                         });
    }
    QObject::connect(DelegateTaskScheduler::instance(),
                     &DelegateTaskScheduler::jobSettled,
                     this,
                     [this](const QString& jobId,
                            const QString& ownerAgentId,
                            bool success,
                            const QString& result) {
                         m_memoryService->onDelegateJobSettled(jobId, ownerAgentId, success, result);
                     });
}

ApplicationServices::~ApplicationServices()
{
    if (m_workspaceService)
        m_workspaceService->saveSessionsToDisk();
}

IWorkspaceService& ApplicationServices::workspace()
{
    return *m_workspaceService;
}

IConversationService& ApplicationServices::conversation()
{
    return *m_conversationService;
}

IGovernanceService& ApplicationServices::governance()
{
    return *m_governanceService;
}

IMemoryService& ApplicationServices::memory()
{
    return *m_memoryService;
}

void ApplicationServices::initialize()
{
    DatabaseManager::instance()->initialize();
    BackendPluginManager::instance()->initialize();

    m_identityManager = IdentityManager::instance();
    m_sessionManager = SessionManager::instance();
    MemoryTool::setWriteHandler(
        [this](const QJsonObject& args) { return m_memoryService->executeMemoryWriteTool(args); });
    SchedulerTool::setDependencies(
        SchedulerTool::Dependencies {
            [this]() {
                return m_memoryService ? m_memoryService->allScheduledJobs() : QList<ScheduledJob>();
            },
            [this](const QString& jobId, ScheduledJob* outJob) {
                return m_memoryService && m_memoryService->scheduledJobById(jobId, outJob);
            },
            [this](const ScheduledJob& job) {
                return m_memoryService ? m_memoryService->addScheduledJob(job) : QString();
            },
            [this](const QString& jobId, const ScheduledJob& job) {
                return m_memoryService && m_memoryService->updateScheduledJob(jobId, job);
            },
            [this](const QString& jobId) {
                return m_memoryService && m_memoryService->removeScheduledJob(jobId);
            },
            [this](const QString& jobId) {
                if (m_memoryService)
                    m_memoryService->triggerScheduledJob(jobId);
            }
        });

    if (m_workspaceService)
        m_workspaceService->initializeStateRepository();
    if (m_governanceService)
        m_governanceService->initialize(m_conversationService->runtimeManager());

    m_conversationService->runtimeManager()->setModelFactory(m_governanceService->modelFactory());
    m_conversationService->runtimeManager()->setToolDispatcher(m_governanceService->toolDispatcher());
    m_conversationService->runtimeManager()->setSessionManager(m_sessionManager);
    m_conversationService->runtimeManager()->setPersistence(m_persistence.get());

    const QStringList teammateBackendIds = BackendPluginManager::instance()->teammateBackendIds();
    for (const QString& backendId : teammateBackendIds) {
        if (ITeammateBackend* backend = BackendPluginManager::instance()->teammateBackend(backendId))
            TeammateManager::instance()->registerBackend(backend);
    }
    QObject::connect(TeammateManager::instance(),
                     &TeammateManager::teammateReplied,
                     this,
                     [this](const QString& teammateId,
                            const QString& teammateName,
                            bool success,
                            const QString& content,
                            const QString& threadId) {
                         m_conversationService->handleTeammateReply(
                             teammateId, teammateName, success, content, threadId);
                     });

    if (m_sessionManager) {
        QObject::connect(m_sessionManager,
                         &SessionManager::messagePosted,
                         this,
                         [this](const QString& sessionId, const Message& msg) {
                             if (m_workspaceService)
                                 m_workspaceService->appendSessionMessageToDisk(sessionId, msg);
                         },
                         Qt::UniqueConnection);
    }

    m_identityManager->userIdentity();
    if (m_memoryService)
        m_memoryService->ensureUserMemoryDocument();

    m_governanceService->loadConfig();
    m_memoryService->initialize(m_conversationService->runtimeManager(),
                                m_governanceService->modelFactory());
    if (m_conversationService->taskStateService())
        m_conversationService->taskStateService()->setPersistence(m_persistence.get());

    if (m_identityManager) {
        const QList<Identity*> agents = m_identityManager->allAgents();
        for (Identity* agent : agents) {
            if (!agent || !agent->isAgent())
                continue;
            const QString agentId = agent->id().trimmed();
            if (agentId.isEmpty())
                continue;
            m_memoryService->ensureAgentPulse(agentId);
            m_memoryService->startAgentHeartbeat(agentId);
        }
    }

    if (m_workspaceService)
        m_workspaceService->startExternalSyncTimer();
}

void ApplicationServices::setModelConfigPathOverride(const QString& filePath)
{
    if (m_governanceService)
        m_governanceService->setModelConfigPathOverride(filePath);
}

void ApplicationServices::loadConfig()
{
    if (m_governanceService)
        m_governanceService->loadConfig();
}
