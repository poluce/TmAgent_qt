#ifndef CHATCAPABILITYINTERFACES_H
#define CHATCAPABILITYINTERFACES_H

#include "core/agent/ToolTypes.h"
#include "HeartbeatService.h"
#include "SchedulerService.h"
#include "llm/LLMTypes.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class Session;

struct ChatTabState {
    QStringList openAgentIds;
    QString activeIdentityId;
};

class IApplicationStartup {
public:
    virtual ~IApplicationStartup() = default;

    virtual void initialize() = 0;
    virtual void loadConfig() = 0;
    virtual void setModelConfigPathOverride(const QString& filePath) = 0;
};

class IWorkspacePersistence {
public:
    virtual ~IWorkspacePersistence() = default;

    virtual void saveSessionsToDisk() = 0;
    virtual bool loadSessionsFromDisk() = 0;
    virtual void saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId) = 0;
    virtual ChatTabState loadTabState() const = 0;
};

class ISessionCommands {
public:
    virtual ~ISessionCommands() = default;

    virtual Session* createNewSession(const QString& agentName = QString()) = 0;
    virtual Session* createSessionForIdentity(const QString& identityId, const QString& title = QString()) = 0;
    virtual Session* createSessionForIdentityAs(const QString& actorIdentityId, const QString& identityId, const QString& title = QString()) = 0;
    virtual void removeSession(const QString& sessionId) = 0;
    virtual bool removeSessionAs(const QString& actorIdentityId, const QString& sessionId) = 0;
    virtual void switchSession(const QString& sessionId) = 0;
};

class ISessionQueries {
public:
    virtual ~ISessionQueries() = default;

    virtual QList<Session*> sessionsForIdentity(const QString& identityId) const = 0;
    virtual QString currentSessionId() const = 0;
    virtual QString agentDisplayNameForSession(const QString& sessionId) const = 0;
    virtual bool canIdentityManageSessions(const QString& identityId) const = 0;
    virtual bool canIdentitySendMessage(const QString& identityId, const QString& sessionId = QString()) const = 0;
    virtual bool canIdentityManageGlobalConfig(const QString& identityId) const = 0;
};

class IConversationCommands {
public:
    virtual ~IConversationCommands() = default;

    virtual QString enqueueUserMessage(const QString& sessionId, const QString& text, const QString& clientMessageId = QString()) = 0;
    virtual QString enqueueUserMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& text, const QString& clientMessageId = QString()) = 0;
    virtual void sendUserMessage(const QString& sessionId, const QString& text) = 0;
    virtual void sendUserMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& text) = 0;
    virtual void abortCurrent(const QString& sessionId) = 0;
    virtual QString abortAndRollback(const QString& sessionId) = 0;
};

class IConversationQueries {
public:
    virtual ~IConversationQueries() = default;

    virtual bool isSessionStreaming(const QString& sessionId) const = 0;
    virtual int pendingTurnCount(const QString& sessionId) const = 0;
    virtual QString activeRunId(const QString& sessionId) const = 0;
    virtual QJsonObject taskStateForSession(const QString& sessionId) const = 0;
};

class IMemoryCommands {
public:
    virtual ~IMemoryCommands() = default;

    virtual bool removeAgentMemoryAs(const QString& actorIdentityId, const QString& agentIdentityId) = 0;
    virtual bool rememberMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& messageId, const QString& fallbackContent = QString(), QString* error = nullptr) = 0;
    virtual bool rebuildMemoryIndexAs(const QString& actorIdentityId, const QString& agentIdentityId = QString(), QJsonObject* result = nullptr, QString* error = nullptr) = 0;
};

class IGovernanceCommands {
public:
    virtual ~IGovernanceCommands() = default;

    virtual void registerModelConfig(const ModelConfig& config) = 0;
    virtual void setDefaultAgentConfig(const LLMConfig& config) = 0;
    virtual void applyConfigToAllRuntimes() = 0;
    virtual void applyToolDispatcherToAllRuntimes() = 0;
    virtual void applyMcpConfig(const QStringList& specs) = 0;
    virtual bool saveMcpConfigSpecs(const QStringList& specs) const = 0;
    virtual bool saveToolLoopPolicyObject(const QJsonObject& raw, QString* errOut = nullptr) const = 0;
};

class IGovernanceQueries {
public:
    virtual ~IGovernanceQueries() = default;

    virtual LLMConfig defaultAgentConfig() const = 0;
    virtual QStringList loadMcpConfigSpecs() const = 0;
    virtual QString mcpConfigPath() const = 0;
    virtual QString modelConfigPath() const = 0;
    virtual QJsonObject defaultToolLoopPolicyObject() const = 0;
    virtual QJsonObject normalizeToolLoopPolicyObject(const QJsonObject& raw) const = 0;
    virtual QJsonObject loadToolLoopPolicyObject() const = 0;
};

class IGovernanceModelCatalog {
public:
    virtual ~IGovernanceModelCatalog() = default;

    virtual QStringList registeredModelConfigIds() const = 0;
    virtual QStringList enabledProviderInstanceIds() const = 0;
    virtual QString displayNameForProviderInstance(const QString& instanceId) const = 0;
    virtual QList<AvailableModel> cachedModelsForProviderInstance(const QString& instanceId) const = 0;
    virtual void fetchModelsForProviderInstanceAsync(const QString& instanceId) = 0;
    virtual QStringList registeredToolNames() const = 0;
};

class IConversationViewQueries {
public:
    virtual ~IConversationViewQueries() = default;

    virtual QString runtimeIdentityIdForSession(const QString& sessionId) const = 0;
    virtual QJsonArray ioHistoryForSession(const QString& sessionId) const = 0;
    virtual QString modelDisplayName(const LLMConfig& config) const = 0;
};

class IConversationViewCommands {
public:
    virtual ~IConversationViewCommands() = default;

    virtual bool renameSessionAndRuntime(const QString& sessionId, const QString& name) = 0;
    virtual void clearConversationHistory(const QString& sessionId) = 0;
};

class IConversationSubscriptions {
public:
    virtual ~IConversationSubscriptions() = default;

    virtual void subscribeConversationEvent(QObject* context, const std::function<void(const QJsonObject&)>& handler) = 0;
    virtual void subscribeStreamData(QObject* context, const std::function<void(const QString&, const QString&)>& handler) = 0;
    virtual void subscribeFinished(QObject* context, const std::function<void(const QString&, const QString&)>& handler) = 0;
    virtual void subscribeError(QObject* context, const std::function<void(const QString&, const QString&)>& handler) = 0;
    virtual void subscribeToolCallsStarted(QObject* context, const std::function<void(const QString&)>& handler) = 0;
    virtual void subscribeToolEvent(QObject* context, const std::function<void(const QString&, const ToolExecutionEvent&)>& handler) = 0;
    virtual void subscribeReasoningStarted(QObject* context, const std::function<void(const QString&)>& handler) = 0;
    virtual void subscribeReasoningStopped(QObject* context, const std::function<void(const QString&)>& handler) = 0;
    virtual void subscribeSessionCreated(QObject* context, const std::function<void(const QString&)>& handler) = 0;
    virtual void subscribeSessionRemoved(QObject* context, const std::function<void(const QString&)>& handler) = 0;
};

struct IdentityViewCapabilities {
    struct Workspace {
        IWorkspacePersistence* persistence = nullptr;
        ISessionCommands* sessionCommands = nullptr;
        ISessionQueries* sessionQueries = nullptr;
        IConversationCommands* conversationCommands = nullptr;
        IConversationQueries* conversationQueries = nullptr;
    } workspace;

    struct Memory {
        IMemoryCommands* commands = nullptr;
    } memory;

    struct History {
        IConversationViewQueries* queries = nullptr;
        IConversationViewCommands* commands = nullptr;
    } history;

    struct Heartbeat {
        std::function<HeartbeatConfig(const QString&)> configForAgent;
        std::function<QJsonObject(const QString&, bool* ok)> loadRuntimeState;
    } heartbeat;
};

struct AgentChatWidgetCapabilities {
    struct Workspace {
        IApplicationStartup* startup = nullptr;
        IWorkspacePersistence* persistence = nullptr;
        ISessionCommands* sessionCommands = nullptr;
        ISessionQueries* sessionQueries = nullptr;
        IConversationCommands* conversationCommands = nullptr;
        IConversationQueries* conversationQueries = nullptr;
        IConversationSubscriptions* subscriptions = nullptr;
        IConversationViewQueries* viewQueries = nullptr;
        IConversationViewCommands* viewCommands = nullptr;
    } workspace;

    struct Governance {
        IGovernanceCommands* commands = nullptr;
        IGovernanceQueries* queries = nullptr;
    } governance;
};

struct GovernanceUiCapabilities {
    IGovernanceCommands* governanceCommands = nullptr;
    const IGovernanceQueries* governanceQueries = nullptr;
    IGovernanceModelCatalog* modelCatalog = nullptr;
    std::function<void(QObject* context, const std::function<void(const QString&)>& handler)> subscribeModelCatalogUpdated;
};

struct CommandPolicyCapabilities {
    IGovernanceCommands* governanceCommands = nullptr;
    const IGovernanceQueries* governanceQueries = nullptr;
};

struct McpConfigDialogCapabilities {
    IGovernanceCommands* governanceCommands = nullptr;
    const IGovernanceQueries* governanceQueries = nullptr;
};

struct ModelConfigDialogCapabilities {
    IGovernanceCommands* governanceCommands = nullptr;
    const IGovernanceQueries* governanceQueries = nullptr;
    IGovernanceModelCatalog* modelCatalog = nullptr;
    std::function<void(QObject* context, const std::function<void(const QString&)>& handler)> subscribeModelCatalogUpdated;
};

struct AgentLifecycleCapabilities {
    IWorkspacePersistence* persistence = nullptr;
    ISessionCommands* sessionCommands = nullptr;
    IMemoryCommands* memoryCommands = nullptr;
    IGovernanceCommands* governanceCommands = nullptr;
    const IGovernanceQueries* governanceQueries = nullptr;
    IGovernanceModelCatalog* modelCatalog = nullptr;
    std::function<void(QObject* context, const std::function<void(const QString&)>& handler)> subscribeModelCatalogUpdated;
};

struct InformationSettingsCapabilities {
    IWorkspacePersistence* persistence = nullptr;
    ISessionQueries* sessionQueries = nullptr;
    IMemoryCommands* memoryCommands = nullptr;
    IGovernanceCommands* governanceCommands = nullptr;
    const IGovernanceQueries* governanceQueries = nullptr;
    IGovernanceModelCatalog* modelCatalog = nullptr;
    std::function<bool(const QString&)> canManageGlobalConfig;

    struct Memory {
        std::function<QJsonObject(bool* ok)> loadPolicyObject;
        std::function<bool(const QJsonObject&)> savePolicyObject;
        std::function<QString(bool* ok)> loadUserMarkdown;
        std::function<bool(const QString&, QString* errOut)> saveUserMarkdown;
    } memory;

    struct Heartbeat {
        std::function<QString(const QString&)> instructionPath;
        std::function<QString(const QString&)> runtimeStateLocation;
        std::function<QJsonObject(const QString&, bool* ok)> loadRuntimeState;
        std::function<QString(const QString&, bool* ok)> readInstructionText;
        std::function<bool(const QString&, const QString&, QString* errOut)> writeInstructionText;
        std::function<HeartbeatConfig(const QString&)> configForAgent;
        std::function<QString(const QString&)> pathForAgent;
        std::function<void(const QString&, const HeartbeatConfig&)> updateConfig;
        std::function<void(const QString&)> startForAgent;
        std::function<void(const QString&, const QString&)> triggerForAgent;
    } heartbeat;

    struct Scheduler {
        std::function<QList<ScheduledJob>()> allJobs;
        std::function<bool(const QString&, ScheduledJob*)> jobById;
        std::function<QString(const ScheduledJob&)> addJob;
        std::function<bool(const QString&, const ScheduledJob&)> updateJob;
        std::function<bool(const QString&)> removeJob;
        std::function<void(const QString&)> triggerJob;
    } scheduler;
};

struct MainWindowCapabilities {
    IApplicationStartup* startup = nullptr;
    IWorkspacePersistence* persistence = nullptr;
    ISessionQueries* sessionQueries = nullptr;
    IConversationSubscriptions* subscriptions = nullptr;
    IdentityViewCapabilities identityView;
    CommandPolicyCapabilities commandPolicy;
    McpConfigDialogCapabilities mcpConfig;
    ModelConfigDialogCapabilities modelConfig;
    AgentLifecycleCapabilities agentLifecycle;
    InformationSettingsCapabilities informationSettings;
};

template <typename T>
IdentityViewCapabilities makeIdentityViewCapabilities(T* service)
{
    IdentityViewCapabilities caps;
    caps.workspace.persistence = service;
    caps.workspace.sessionCommands = service;
    caps.workspace.sessionQueries = service;
    caps.workspace.conversationCommands = service;
    caps.workspace.conversationQueries = service;
    caps.memory.commands = service;
    caps.history.queries = service;
    caps.history.commands = service;
    caps.heartbeat.configForAgent = [service](const QString& agentId) {
        return service->heartbeatConfigForAgent(agentId);
    };
    caps.heartbeat.loadRuntimeState = [service](const QString& agentId, bool* ok) {
        return service->loadHeartbeatRuntimeState(agentId, ok);
    };
    return caps;
}

template <typename T>
AgentChatWidgetCapabilities makeAgentChatWidgetCapabilities(T* service)
{
    AgentChatWidgetCapabilities caps;
    caps.workspace.startup = service;
    caps.workspace.persistence = service;
    caps.workspace.sessionCommands = service;
    caps.workspace.sessionQueries = service;
    caps.workspace.conversationCommands = service;
    caps.workspace.conversationQueries = service;
    caps.workspace.subscriptions = service;
    caps.workspace.viewQueries = service;
    caps.workspace.viewCommands = service;
    caps.governance.commands = service;
    caps.governance.queries = service;
    return caps;
}

template <typename T>
GovernanceUiCapabilities makeGovernanceUiCapabilities(T* service)
{
    GovernanceUiCapabilities caps;
    caps.governanceCommands = service;
    caps.governanceQueries = service;
    caps.modelCatalog = service;
    caps.subscribeModelCatalogUpdated = [service](QObject* context, const std::function<void(const QString&)>& handler) {
        if (!context || !handler)
            return;
        QObject::connect(service, &T::modelCatalogUpdated, context, [handler](const QString& instanceId) {
            handler(instanceId);
        });
    };
    return caps;
}

template <typename T>
CommandPolicyCapabilities makeCommandPolicyCapabilities(T* service)
{
    CommandPolicyCapabilities caps;
    caps.governanceCommands = service;
    caps.governanceQueries = service;
    return caps;
}

template <typename T>
McpConfigDialogCapabilities makeMcpConfigDialogCapabilities(T* service)
{
    McpConfigDialogCapabilities caps;
    caps.governanceCommands = service;
    caps.governanceQueries = service;
    return caps;
}

template <typename T>
ModelConfigDialogCapabilities makeModelConfigDialogCapabilities(T* service)
{
    ModelConfigDialogCapabilities caps;
    caps.governanceCommands = service;
    caps.governanceQueries = service;
    caps.modelCatalog = service;
    caps.subscribeModelCatalogUpdated = [service](QObject* context, const std::function<void(const QString&)>& handler) {
        if (!context || !handler)
            return;
        QObject::connect(service, &T::modelCatalogUpdated, context, [handler](const QString& instanceId) {
            handler(instanceId);
        });
    };
    return caps;
}

template <typename T>
AgentLifecycleCapabilities makeAgentLifecycleCapabilities(T* service)
{
    AgentLifecycleCapabilities caps;
    caps.persistence = service;
    caps.sessionCommands = service;
    caps.memoryCommands = service;
    caps.governanceCommands = service;
    caps.governanceQueries = service;
    caps.modelCatalog = service;
    caps.subscribeModelCatalogUpdated = [service](QObject* context, const std::function<void(const QString&)>& handler) {
        if (!context || !handler)
            return;
        QObject::connect(service, &T::modelCatalogUpdated, context, [handler](const QString& instanceId) {
            handler(instanceId);
        });
    };
    return caps;
}

template <typename T>
InformationSettingsCapabilities makeInformationSettingsCapabilities(T* service)
{
    InformationSettingsCapabilities caps;
    caps.persistence = service;
    caps.sessionQueries = service;
    caps.memoryCommands = service;
    caps.governanceCommands = service;
    caps.governanceQueries = service;
    caps.modelCatalog = service;
    caps.canManageGlobalConfig = [service](const QString& identityId) {
        return service->canIdentityManageGlobalConfig(identityId);
    };
    caps.memory.loadPolicyObject = [service](bool* ok) { return service->loadMemoryPolicyObject(ok); };
    caps.memory.savePolicyObject = [service](const QJsonObject& obj) { return service->saveMemoryPolicyObject(obj); };
    caps.memory.loadUserMarkdown = [service](bool* ok) { return service->loadUserMemoryMarkdown(ok); };
    caps.memory.saveUserMarkdown = [service](const QString& markdown, QString* errOut) {
        return service->saveUserMemoryMarkdown(markdown, errOut);
    };
    caps.heartbeat.instructionPath = [service](const QString& agentId) {
        return service->agentHeartbeatInstructionPath(agentId);
    };
    caps.heartbeat.runtimeStateLocation = [service](const QString& agentId) {
        return service->heartbeatRuntimeStateLocation(agentId);
    };
    caps.heartbeat.loadRuntimeState = [service](const QString& agentId, bool* ok) {
        return service->loadHeartbeatRuntimeState(agentId, ok);
    };
    caps.heartbeat.readInstructionText = [service](const QString& filePath, bool* ok) {
        return service->readPossiblyMojibakeUtf8File(filePath, ok);
    };
    caps.heartbeat.writeInstructionText = [service](const QString& filePath, const QString& text, QString* errOut) {
        return service->writeUtf8TextFile(filePath, text, errOut);
    };
    caps.heartbeat.configForAgent = [service](const QString& agentId) {
        return service->heartbeatConfigForAgent(agentId);
    };
    caps.heartbeat.pathForAgent = [service](const QString& agentId) {
        return service->heartbeatPathForAgent(agentId);
    };
    caps.heartbeat.updateConfig = [service](const QString& agentId, const HeartbeatConfig& config) {
        service->updateHeartbeatConfig(agentId, config);
    };
    caps.heartbeat.startForAgent = [service](const QString& agentId) {
        service->startHeartbeatForAgent(agentId);
    };
    caps.heartbeat.triggerForAgent = [service](const QString& agentId, const QString& reason) {
        service->triggerHeartbeatForAgent(agentId, reason);
    };
    caps.scheduler.allJobs = [service]() { return service->allScheduledJobs(); };
    caps.scheduler.jobById = [service](const QString& jobId, ScheduledJob* outJob) {
        return service->scheduledJobById(jobId, outJob);
    };
    caps.scheduler.addJob = [service](const ScheduledJob& job) { return service->addScheduledJob(job); };
    caps.scheduler.updateJob = [service](const QString& jobId, const ScheduledJob& job) {
        return service->updateScheduledJob(jobId, job);
    };
    caps.scheduler.removeJob = [service](const QString& jobId) {
        return service->removeScheduledJob(jobId);
    };
    caps.scheduler.triggerJob = [service](const QString& jobId) {
        service->triggerScheduledJob(jobId);
    };
    return caps;
}

template <typename T>
MainWindowCapabilities makeMainWindowCapabilities(T* service)
{
    MainWindowCapabilities caps;
    caps.startup = service;
    caps.persistence = service;
    caps.sessionQueries = service;
    caps.subscriptions = service;
    caps.identityView = makeIdentityViewCapabilities(service);
    caps.commandPolicy = makeCommandPolicyCapabilities(service);
    caps.mcpConfig = makeMcpConfigDialogCapabilities(service);
    caps.modelConfig = makeModelConfigDialogCapabilities(service);
    caps.agentLifecycle = makeAgentLifecycleCapabilities(service);
    caps.informationSettings = makeInformationSettingsCapabilities(service);
    return caps;
}

#endif // CHATCAPABILITYINTERFACES_H

