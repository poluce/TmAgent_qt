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
class MemoryMaintenanceService;
class MemoryManager;
class MemoryToolWriteService;
class ModelFactory;
class RuntimeManager;
class SchedulerService;
struct TurnTask;

class MemoryService final : public IMemoryService {
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
    HeartbeatConfig heartbeatConfigForAgent(const QString& agentId) const override;
    QString heartbeatPathForAgent(const QString& agentId) const override;
    void updateHeartbeatConfig(const QString& agentId, const HeartbeatConfig& config) override;
    void startHeartbeatForAgent(const QString& agentId) override;
    void stopHeartbeatForAgent(const QString& agentId) override;
    void triggerHeartbeatForAgent(const QString& agentId,
                                  const QString& reason = QStringLiteral("requested")) override;
    QList<ScheduledJob> allScheduledJobs() const override;
    bool scheduledJobById(const QString& jobId, ScheduledJob* outJob) const override;
    QString addScheduledJob(const ScheduledJob& job) override;
    bool updateScheduledJob(const QString& jobId, const ScheduledJob& job) override;
    bool removeScheduledJob(const QString& jobId) override;
    void triggerScheduledJob(const QString& jobId) override;

    void initialize(RuntimeManager* runtimeManager, ModelFactory* modelFactory);
    MemoryManager* memoryManager() const;
    HealthMonitor* healthMonitor() const;
    HeartbeatService* heartbeatService() const;
    SchedulerService* schedulerService() const;
    AgentPulseRegistry* agentPulseRegistry() const;
    QHash<QString, int>& memoryRetainedTurnsByAgent();
    const QHash<QString, int>& memoryRetainedTurnsByAgent() const;
    QHash<QString, HeartbeatRuntimeState>& heartbeatRuntimeByAgent();
    const QHash<QString, HeartbeatRuntimeState>& heartbeatRuntimeByAgent() const;
    QHash<QString, AgentPulse*>& agentPulses();
    const QHash<QString, AgentPulse*>& agentPulses() const;
    void onDelegateJobSettled(const QString& jobId,
                              const QString& ownerAgentId,
                              bool success,
                              const QString& result);
    void onHeartbeatTriggered(const QString& agentId, const QString& reason);
    void onHeartbeatSkipped(const QString& agentId, const QString& reason);
    void onScheduledJobTriggered(const QString& jobId, const QString& jobName);
    void ensureAgentPulse(const QString& agentId);
    void reportPulseProgress(const QString& agentId, const QString& summary = QString());
    ToolResult executeMemoryWriteTool(const QJsonObject& args);
    MemoryMaintenanceService makeMemoryMaintenanceService();
    MemoryToolWriteService makeMemoryToolWriteService();
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
    QHash<QString, HeartbeatRuntimeState> m_heartbeatRuntimeByAgent;
};

#endif // MEMORYSERVICE_H
