#ifndef MEMORYSERVICE_H
#define MEMORYSERVICE_H

#include "AppFacade.h"
#include "HeartbeatRuntimeState.h"
#include "core/agent/ToolTypes.h"
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <memory>

class AgentPulse;
class AgentPulseRegistry;
class ApplicationServices;
class HealthMonitor;
class HeartbeatService;
class Identity;
class MemoryManager;
class ModelFactory;
class RuntimeManager;
class SchedulerService;
struct ConversationRuntimeEventsAccess;
struct ConversationCompletionAccess;
struct MemoryHeartbeatAccess;
struct MemoryBackgroundJobsAccess;
struct TurnTask;

class MemoryService final : public IMemoryService {
    friend class ApplicationServices;
    friend class ConversationService;
    friend class WorkspaceService;
    friend class HeartbeatService;
    friend struct ConversationRuntimeEventsAccess;
    friend struct ConversationCompletionAccess;
    friend struct MemoryHeartbeatAccess;
    friend struct MemoryBackgroundJobsAccess;
public:
    explicit MemoryService(ApplicationServices& app);
    ~MemoryService();

    bool removeAgentMemoryAs(const QString& actorIdentityId, const QString& agentIdentityId) override;
    bool rememberMessageAs(const QString& actorIdentityId,
                           const QString& sessionId,
                           const QString& messageId,
                           const QString& fallbackContent = QString(),
                           QString* error = nullptr) override;
    bool rebuildMemoryIndexAs(const QString& actorIdentityId,
                              const QString& agentIdentityId = QString(),
                              QJsonObject* result = nullptr,
                              QString* error = nullptr) override;
    QJsonObject loadMemoryPolicyObject(bool* ok = nullptr) const override;
    bool saveMemoryPolicyObject(const QJsonObject& obj) const override;
    QString loadUserMemoryMarkdown(bool* ok = nullptr) const override;
    bool saveUserMemoryMarkdown(const QString& markdown, QString* errOut = nullptr) const override;
    QString agentHeartbeatInstructionPath(const QString& agentId) const override;
    QString heartbeatRuntimeStateLocation(const QString& agentId) const override;
    QJsonObject loadHeartbeatRuntimeState(const QString& agentId, bool* ok = nullptr) const override;
    QString readPossiblyMojibakeUtf8File(const QString& filePath, bool* ok = nullptr) const override;
    bool writeUtf8TextFile(const QString& filePath,
                           const QString& text,
                           QString* errOut = nullptr) const override;
    HeartbeatPolicy heartbeatPolicyForAgent(const QString& agentId) const override;
    QString heartbeatInstructionPathForAgent(const QString& agentId) const override;
    void updateHeartbeatPolicy(const QString& agentId, const HeartbeatPolicy& policy) override;
    void startAgentHeartbeat(const QString& agentId) override;
    void stopAgentHeartbeat(const QString& agentId) override;
    void requestManualHeartbeat(const QString& agentId,
                                const QString& reason = QStringLiteral("manual")) override;
    QList<ScheduledJob> allScheduledJobs() const override;
    bool scheduledJobById(const QString& jobId, ScheduledJob* outJob) const override;
    QString addScheduledJob(const ScheduledJob& job) override;
    bool updateScheduledJob(const QString& jobId, const ScheduledJob& job) override;
    bool removeScheduledJob(const QString& jobId) override;
    void triggerScheduledJob(const QString& jobId) override;

private:
    void initialize(RuntimeManager* runtimeManager, ModelFactory* modelFactory);
    MemoryManager* memoryManager() const;
    HealthMonitor* healthMonitor() const;
    HeartbeatService* heartbeatService() const;
    SchedulerService* schedulerService() const;
    AgentPulseRegistry* agentPulseRegistry() const;
    QHash<QString, int>& memoryRetainedTurnsByAgent();
    const QHash<QString, int>& memoryRetainedTurnsByAgent() const;
    QHash<QString, AgentPulse*>& agentPulses();
    const QHash<QString, AgentPulse*>& agentPulses() const;
    void onDelegateJobSettled(const QString& jobId,
                              const QString& ownerAgentId,
                              bool success,
                              const QString& result);
    void onConversationEvent(const QJsonObject& event);
    void onScheduledJobTriggered(const QString& jobId, const QString& jobName);
    void ensureAgentPulse(const QString& agentId);
    void reportPulseProgress(const QString& agentId, const QString& summary = QString());
    ToolResult executeMemoryWriteTool(const QJsonObject& args);
    void refreshMemoryIndexAndEmit(const QString& sessionId,
                                   const QString& agentId,
                                   const TurnTask* turn,
                                   const QString& reason,
                                   const QString& sourcePath,
                                   const QJsonObject& sourceMetadata) const;
    void maybeReflectMemoryAndEmit(const QString& sessionId,
                                   const QString& agentId,
                                   const TurnTask& turn,
                                   bool forceReflection,
                                   const QString& triggerReason);
    QString resolvePrimarySessionForAgent(const QString& agentId,
                                          bool createIfMissing,
                                          bool isolated,
                                          const QString& titleSuffix = QString()) const;
    void ensureMemoryInitializedForAgent(Identity* agentIdentity);
    bool ensureUserMemoryDocument();

private:
    ApplicationServices& m_app;
    std::unique_ptr<MemoryManager> m_memoryManager;
    std::unique_ptr<HealthMonitor> m_healthMonitor;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<SchedulerService> m_schedulerService;
    std::unique_ptr<AgentPulseRegistry> m_agentPulseRegistry;
    QHash<QString, AgentPulse*> m_agentPulses;
    QHash<QString, int> m_memoryRetainedTurnsByAgent;
};

#endif // MEMORYSERVICE_H
