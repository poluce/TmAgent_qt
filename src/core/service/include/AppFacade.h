#ifndef APPFACADE_H
#define APPFACADE_H

#include "HeartbeatTypes.h"
#include "core/agent/ToolPluginTypes.h"
#include "SchedulerService.h"
#include "core/agent/ToolTypes.h"
#include "llm/LLMTypes.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class Session;

struct ChatTabState {
    QStringList openAgentIds;
    QString activeIdentityId;
};

class AppEventHub : public QObject {
    Q_OBJECT
public:
    explicit AppEventHub(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

signals:
    void conversationEvent(const QJsonObject& event);
    void streamDataReceived(const QString& sessionId, const QString& data);
    void finished(const QString& sessionId, const QString& fullContent);
    void errorOccurred(const QString& sessionId, const QString& errorMsg);
    void toolCallsStarted(const QString& sessionId);
    void toolEvent(const QString& sessionId, const ToolExecutionEvent& event);
    void reasoningStarted(const QString& sessionId);
    void reasoningStopped(const QString& sessionId);
    void sessionCreated(const QString& sessionId);
    void sessionRemoved(const QString& sessionId);
    void configLoaded();
    void modelCatalogUpdated(const QString& instanceId);
};

class IWorkspaceService {
public:
    virtual ~IWorkspaceService() = default;

    virtual Session* createNewSession(const QString& agentName = QString()) = 0;
    virtual Session* createSessionForIdentity(const QString& identityId, const QString& title = QString()) = 0;
    virtual Session* createSessionForIdentityAs(const QString& actorIdentityId,
                                                const QString& identityId,
                                                const QString& title = QString()) = 0;
    virtual QList<Session*> sessionsForIdentity(const QString& identityId) const = 0;
    virtual void removeSession(const QString& sessionId) = 0;
    virtual bool removeSessionAs(const QString& actorIdentityId, const QString& sessionId) = 0;
    virtual void switchSession(const QString& sessionId) = 0;
    virtual QString currentSessionId() const = 0;
    virtual QString agentDisplayNameForSession(const QString& sessionId) const = 0;
    virtual bool canIdentityManageSessions(const QString& identityId) const = 0;
    virtual bool canIdentitySendMessage(const QString& identityId, const QString& sessionId = QString()) const = 0;
    virtual bool canIdentityManageGlobalConfig(const QString& identityId) const = 0;
    virtual void saveSessionsToDisk() = 0;
    virtual bool loadSessionsFromDisk() = 0;
    virtual void saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId) = 0;
    virtual ChatTabState loadTabState() const = 0;
};

class IConversationService {
public:
    virtual ~IConversationService() = default;

    virtual QString enqueueUserMessage(const QString& sessionId,
                                       const QString& text,
                                       const QString& clientMessageId = QString()) = 0;
    virtual QString enqueueUserMessageAs(const QString& actorIdentityId,
                                         const QString& sessionId,
                                         const QString& text,
                                         const QString& clientMessageId = QString()) = 0;
    virtual void sendUserMessage(const QString& sessionId, const QString& text) = 0;
    virtual void sendUserMessageAs(const QString& actorIdentityId,
                                   const QString& sessionId,
                                   const QString& text) = 0;
    virtual void abortCurrent(const QString& sessionId) = 0;
    virtual QString abortAndRollback(const QString& sessionId) = 0;
    virtual bool isSessionStreaming(const QString& sessionId) const = 0;
    virtual int pendingTurnCount(const QString& sessionId) const = 0;
    virtual QString activeRunId(const QString& sessionId) const = 0;
    virtual QJsonObject taskStateForSession(const QString& sessionId) const = 0;
    virtual QString runtimeIdentityIdForSession(const QString& sessionId) const = 0;
    virtual QJsonArray ioHistoryForSession(const QString& sessionId) const = 0;
    virtual QString modelDisplayName(const LLMConfig& config) const = 0;
    virtual bool renameSessionAndRuntime(const QString& sessionId, const QString& name) = 0;
    virtual void clearConversationHistory(const QString& sessionId) = 0;
};

class IGovernanceService {
public:
    virtual ~IGovernanceService() = default;

    virtual void registerModelConfig(const ModelConfig& config) = 0;
    virtual void setDefaultAgentConfig(const LLMConfig& config) = 0;
    virtual LLMConfig defaultAgentConfig() const = 0;
    virtual void applyConfigToAllRuntimes() = 0;
    virtual void applyToolDispatcherToAllRuntimes() = 0;
    virtual void applyMcpConfig(const QStringList& specs) = 0;
    virtual QStringList loadMcpConfigSpecs() const = 0;
    virtual bool saveMcpConfigSpecs(const QStringList& specs) const = 0;
    virtual bool saveToolLoopPolicyObject(const QJsonObject& raw, QString* errOut = nullptr) const = 0;
    virtual QString mcpConfigPath() const = 0;
    virtual QString modelConfigPath() const = 0;
    virtual QJsonObject defaultToolLoopPolicyObject() const = 0;
    virtual QJsonObject normalizeToolLoopPolicyObject(const QJsonObject& raw) const = 0;
    virtual QJsonObject loadToolLoopPolicyObject() const = 0;
    virtual QStringList registeredModelConfigIds() const = 0;
    virtual QStringList enabledProviderInstanceIds() const = 0;
    virtual QString displayNameForProviderInstance(const QString& instanceId) const = 0;
    virtual QList<AvailableModel> cachedModelsForProviderInstance(const QString& instanceId) const = 0;
    virtual void fetchModelsForProviderInstanceAsync(const QString& instanceId) = 0;
    virtual QStringList registeredToolNames() const = 0;
    virtual QString toolPluginConfigPath() const = 0;
    virtual QJsonObject defaultToolPluginConfigObject() const = 0;
    virtual QJsonObject normalizeToolPluginConfigObject(const QJsonObject& raw) const = 0;
    virtual QJsonObject loadToolPluginConfigObject() const = 0;
    virtual bool saveToolPluginConfigObject(const QJsonObject& raw, QString* errOut = nullptr) const = 0;
    virtual QList<ToolPluginInfo> toolPluginInfos() const = 0;
    virtual void reloadToolPlugins() = 0;
};

class IMemoryService {
public:
    virtual ~IMemoryService() = default;

    virtual bool removeAgentMemoryAs(const QString& actorIdentityId, const QString& agentIdentityId) = 0;
    virtual bool rememberMessageAs(const QString& actorIdentityId,
                                   const QString& sessionId,
                                   const QString& messageId,
                                   const QString& fallbackContent = QString(),
                                   QString* error = nullptr) = 0;
    virtual bool rebuildMemoryIndexAs(const QString& actorIdentityId,
                                      const QString& agentIdentityId = QString(),
                                      QJsonObject* result = nullptr,
                                      QString* error = nullptr) = 0;
    virtual QJsonObject loadMemoryPolicyObject(bool* ok = nullptr) const = 0;
    virtual bool saveMemoryPolicyObject(const QJsonObject& obj) const = 0;
    virtual QString loadUserMemoryMarkdown(bool* ok = nullptr) const = 0;
    virtual bool saveUserMemoryMarkdown(const QString& markdown, QString* errOut = nullptr) const = 0;
    virtual QString agentHeartbeatInstructionPath(const QString& agentId) const = 0;
    virtual QString heartbeatRuntimeStateLocation(const QString& agentId) const = 0;
    virtual QJsonObject loadHeartbeatRuntimeState(const QString& agentId, bool* ok = nullptr) const = 0;
    virtual QString readPossiblyMojibakeUtf8File(const QString& filePath, bool* ok = nullptr) const = 0;
    virtual bool writeUtf8TextFile(const QString& filePath,
                                   const QString& text,
                                   QString* errOut = nullptr) const = 0;
    virtual HeartbeatPolicy heartbeatPolicyForAgent(const QString& agentId) const = 0;
    virtual QString heartbeatInstructionPathForAgent(const QString& agentId) const = 0;
    virtual void updateHeartbeatPolicy(const QString& agentId, const HeartbeatPolicy& policy) = 0;
    virtual void startAgentHeartbeat(const QString& agentId) = 0;
    virtual void stopAgentHeartbeat(const QString& agentId) = 0;
    virtual void requestManualHeartbeat(const QString& agentId,
                                        const QString& reason = QStringLiteral("manual")) = 0;
    virtual QList<ScheduledJob> allScheduledJobs() const = 0;
    virtual bool scheduledJobById(const QString& jobId, ScheduledJob* outJob) const = 0;
    virtual QString addScheduledJob(const ScheduledJob& job) = 0;
    virtual bool updateScheduledJob(const QString& jobId, const ScheduledJob& job) = 0;
    virtual bool removeScheduledJob(const QString& jobId) = 0;
    virtual void triggerScheduledJob(const QString& jobId) = 0;
};

class IAppFacade {
public:
    virtual ~IAppFacade() = default;

    virtual IWorkspaceService& workspace() = 0;
    virtual IConversationService& conversation() = 0;
    virtual IGovernanceService& governance() = 0;
    virtual IMemoryService& memory() = 0;
    virtual AppEventHub* events() = 0;
    virtual void initialize() = 0;
    virtual void loadConfig() = 0;
    virtual void setModelConfigPathOverride(const QString& filePath) = 0;
};

#endif // APPFACADE_H
