#ifndef APPLICATIONSERVICES_H
#define APPLICATIONSERVICES_H

#include "AppFacade.h"
#include "core/agent/ToolTypes.h"
#include "HeartbeatRuntimeState.h"
#include "HeartbeatService.h"
#include "SchedulerService.h"
#include "TurnManager.h"
#include "llm/LLMTypes.h"
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <functional>
#include <memory>

class AgentRuntime;
class Identity;
class Session;
struct Message;
class ModelFactory;
class ToolDispatcher;
class McpToolProvider;
class IdentityManager;
class SessionManager;
class ChatPersistenceService;
class ChatStateRepository;
class QTimer;
class MemoryManager;
class RuntimeManager;
class ConfigService;
class HealthMonitor;
class HeartbeatService;
class SchedulerService;
class AgentPulse;
class AgentPulseRegistry;
class TaskStateService;
class ChatCoordinatorFactory;
class MemoryMaintenanceService;
class MemoryToolWriteService;
struct ConversationCoreDeps;
class WorkspaceService;
class ConversationService;
class GovernanceService;
class MemoryService;

/**
 * @brief 应用级 facade / composition root。
 *
 * 对外只暴露 IAppFacade，四组子系统能力由独立服务对象承载。
 * 当前核心状态与协作逻辑仍集中在本类内部，子服务通过委托复用这些实现。
 */
class ApplicationServices : public QObject,
                            public IAppFacade {
    Q_OBJECT
public:
    explicit ApplicationServices(QObject* parent = nullptr);
    ~ApplicationServices() override;

    IWorkspaceService& workspace() override;
    IConversationService& conversation() override;
    IGovernanceService& governance() override;
    IMemoryService& memory() override;
    AppEventHub* events() override { return m_eventHub.get(); }

    // ---- 初始化 ----
    void initialize() override;

    // ---- 用户消息 ----
    QString enqueueUserMessage(const QString& sessionId, const QString& text, const QString& clientMessageId = QString());
    QString enqueueUserMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& text, const QString& clientMessageId = QString());
    void sendUserMessage(const QString& sessionId, const QString& text);
    void sendUserMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& text);
    void abortCurrent(const QString& sessionId);
    QString abortAndRollback(const QString& sessionId);

    // ---- 会话管理 ----
    Session* createNewSession(const QString& agentName = QString());
    Session* createSessionForIdentity(const QString& identityId, const QString& title = QString());
    Session* createSessionForIdentityAs(const QString& actorIdentityId, const QString& identityId, const QString& title = QString());
    QList<Session*> sessionsForIdentity(const QString& identityId) const;
    void removeSession(const QString& sessionId);
    bool removeSessionAs(const QString& actorIdentityId, const QString& sessionId);
    bool removeAgentMemoryAs(const QString& actorIdentityId, const QString& agentIdentityId);
    bool rememberMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& messageId, const QString& fallbackContent = QString(), QString* error = nullptr);
    bool rebuildMemoryIndexAs(const QString& actorIdentityId, const QString& agentIdentityId = QString(), QJsonObject* result = nullptr, QString* error = nullptr);
    void switchSession(const QString& sessionId);
    QString currentSessionId() const;

    // ---- AgentRuntime 管理 ----
    AgentRuntime* runtimeForSession(const QString& sessionId) const;
    AgentRuntime* ensureRuntimeForSession(const QString& sessionId);

    // ---- 配置（委托给 ConfigService / RuntimeManager）----
    void registerModelConfig(const ModelConfig& config);
    void setDefaultAgentConfig(const LLMConfig& config);
    LLMConfig defaultAgentConfig() const;
    void applyConfigToAllRuntimes();
    void applyToolDispatcherToAllRuntimes();

    // ---- 查询 ----
    bool isSessionStreaming(const QString& sessionId) const;
    int pendingTurnCount(const QString& sessionId) const;
    QString activeRunId(const QString& sessionId) const;
    QString agentDisplayNameForSession(const QString& sessionId) const;
    QJsonObject taskStateForSession(const QString& sessionId) const;
    QString runtimeIdentityIdForSession(const QString& sessionId) const;
    QJsonArray ioHistoryForSession(const QString& sessionId) const;
    QString modelDisplayName(const LLMConfig& config) const;
    bool canIdentityManageSessions(const QString& identityId) const;
    bool canIdentitySendMessage(const QString& identityId, const QString& sessionId = QString()) const;
    bool canIdentityManageGlobalConfig(const QString& identityId) const;

    // ---- MCP 配置 ----
    void applyMcpConfig(const QStringList& specs);
    QStringList loadMcpConfigSpecs() const;
    bool saveMcpConfigSpecs(const QStringList& specs) const;
    bool saveToolLoopPolicyObject(const QJsonObject& raw, QString* errOut = nullptr) const;
    QString mcpConfigPath() const;

    // ---- 模型配置 ----
    QString modelConfigPath() const;
    QJsonObject defaultToolLoopPolicyObject() const;
    QJsonObject normalizeToolLoopPolicyObject(const QJsonObject& raw) const;
    QJsonObject loadToolLoopPolicyObject() const;
    QJsonObject loadMemoryPolicyObject(bool* ok = nullptr) const;
    bool saveMemoryPolicyObject(const QJsonObject& obj) const;
    QString loadUserMemoryMarkdown(bool* ok = nullptr) const;
    bool saveUserMemoryMarkdown(const QString& markdown, QString* errOut = nullptr) const;
    QString agentHeartbeatInstructionPath(const QString& agentId) const;
    QString heartbeatRuntimeStateLocation(const QString& agentId) const;
    QJsonObject loadHeartbeatRuntimeState(const QString& agentId, bool* ok = nullptr) const;
    QString readPossiblyMojibakeUtf8File(const QString& filePath, bool* ok = nullptr) const;
    bool writeUtf8TextFile(const QString& filePath, const QString& text, QString* errOut = nullptr) const;
    HeartbeatConfig heartbeatConfigForAgent(const QString& agentId) const;
    QString heartbeatPathForAgent(const QString& agentId) const;
    void updateHeartbeatConfig(const QString& agentId, const HeartbeatConfig& config);
    void startHeartbeatForAgent(const QString& agentId);
    void stopHeartbeatForAgent(const QString& agentId);
    void triggerHeartbeatForAgent(const QString& agentId, const QString& reason = QStringLiteral("requested"));
    QList<ScheduledJob> allScheduledJobs() const;
    bool scheduledJobById(const QString& jobId, ScheduledJob* outJob) const;
    QString addScheduledJob(const ScheduledJob& job);
    bool updateScheduledJob(const QString& jobId, const ScheduledJob& job);
    bool removeScheduledJob(const QString& jobId);
    void triggerScheduledJob(const QString& jobId);
    void setModelConfigPathOverride(const QString& filePath) override;
    void loadConfig() override;

    // ---- 治理目录查询 ----
    QStringList registeredModelConfigIds() const;
    QStringList enabledProviderInstanceIds() const;
    QString displayNameForProviderInstance(const QString& instanceId) const;
    QList<AvailableModel> cachedModelsForProviderInstance(const QString& instanceId) const;
    void fetchModelsForProviderInstanceAsync(const QString& instanceId);
    QStringList registeredToolNames() const;

    // ---- 持久化 ----
    void saveSessionsToDisk();
    bool loadSessionsFromDisk();
    bool renameSessionAndRuntime(const QString& sessionId, const QString& name);
    void clearConversationHistory(const QString& sessionId);

    // ---- Tab 状态持久化 ----
    void saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId);
    using TabState = ChatTabState;
    TabState loadTabState() const;

signals:
    // 事件流（供任意界面层/服务层订阅，不依赖具体 UI 组件）
    void conversationEvent(const QJsonObject& event);

    // 转发 AgentRuntime 信号
    void streamDataReceived(const QString& sessionId, const QString& data);
    void finished(const QString& sessionId, const QString& fullContent);
    void errorOccurred(const QString& sessionId, const QString& errorMsg);
    void toolCallsStarted(const QString& sessionId);
    void toolEvent(const QString& sessionId, const ToolExecutionEvent& event);

    // 思考状态 UI 转发
    void reasoningStarted(const QString& sessionId);
    void reasoningStopped(const QString& sessionId);

    // 会话事件
    void sessionCreated(const QString& sessionId);
    void sessionRemoved(const QString& sessionId);
    void configLoaded();
    void modelCatalogUpdated(const QString& instanceId);

private:
    SessionPipeline& ensurePipeline(const QString& sessionId);
    SessionPipeline* findPipeline(const QString& sessionId);
    const SessionPipeline* findPipeline(const QString& sessionId) const;
    QString agentIdentityIdForSession(const QString& sessionId) const;
    Identity* findOrCreateAgentIdentity(Session* session);
    AgentRuntime* ensureRuntimeForAgent(Identity* agentIdentity);
    void releaseRuntimeIfUnused(const QString& agentIdentityId);
    LLMConfig composeConfigForIdentity(Identity* identity) const;
    QJsonArray buildRuntimeHistoryFromMessages(Session* session) const;
    void tryStartNextTurn(const QString& sessionId);
    void tryStartNextTurnForAgent(const QString& agentIdentityId);
    void enqueueInternalTurn(const QString& sessionId, const QString& content, const QString& clientMessageId = QString());
    void resetSessionStreamState(const QString& sessionId);
    void finalizeTurn(const QString& sessionId, TurnTask* outTurn);
    void flushPendingDeltaLog(const QString& sessionId, SessionPipeline* pipeline, const TurnTask* turn, bool force);
    void emitPipelineEvent(const QString& type, const QString& sessionId, const TurnTask* turn = nullptr, const QString& delta = QString(), const QString& error = QString(), const QJsonObject& extra = QJsonObject(), bool persistToDisk = true);
    void appendRuntimeIoEventEntry(const QString& sessionId, const QString& type, const TurnTask* turn, const QString& error, const QJsonObject& extra);
    void clearToolProgressCacheForSession(const QString& sessionId);
    void clearDelegateStartsForSession(const QString& sessionId);

    void onRuntimeStreamData(const QString& sessionId, const QString& data);
    void onRuntimeFinished(const QString& sessionId, const QString& fullContent);
    void onRuntimeError(const QString& sessionId, const QString& errorMsg);
    void onRuntimeToolCallsStarted(const QString& sessionId);
    void onRuntimeToolEvent(const QString& sessionId, const ToolExecutionEvent& event);

    void connectRuntimeSignals(AgentRuntime* runtime);
    bool isUserIdentity(const QString& identityId) const;

    void appendSessionMessageToDisk(const QString& sessionId, const Message& msg);
    void pollExternalChanges();
    bool appendEventLog(const QJsonObject& event) const;
    void ensureMemoryInitializedForAgent(Identity* agentIdentity);
    MemoryMaintenanceService makeMemoryMaintenanceService();
    MemoryToolWriteService makeMemoryToolWriteService();
    ConversationCoreDeps makeConversationCoreDeps();

    void onHeartbeatTriggered(const QString& agentId, const QString& reason);
    void onDelegateJobSettled(const QString& jobId, const QString& ownerAgentId, bool success, const QString& result);
    void onScheduledJobTriggered(const QString& jobId, const QString& jobName);
    void ensureAgentPulse(const QString& agentId);
    void reportPulseProgress(const QString& agentId, const QString& summary = QString());
    ToolResult executeMemoryWriteTool(const QJsonObject& args);
    void updateTaskStateForSession(const QString& sessionId, const QString& state, const TurnTask* turn, const QJsonObject& extra = QJsonObject());
    void clearTaskStateForSession(const QString& sessionId);

    static constexpr int kSoftQueueDepth = 10;
    static constexpr int kHardQueueDepth = 200;
    static constexpr int kQueueMergeWindowMs = 2500;
    static constexpr int kQueueMergeMaxMergedMessages = 4;
    static constexpr int kQueueMergeMaxChars = 12000;
    static constexpr int kHistoryMaxMessages = 120;
    static constexpr int kHistoryMaxChars = 32000;
    static constexpr int kHistoryToolResultMaxChars = 3000;
    static constexpr int kMemoryContextMaxChars = 4500;
    static constexpr int kDeltaBatchFlushIntervalMs = 400;
    static constexpr int kDeltaBatchFlushChars = 120;
    static constexpr int kDeltaBatchFlushChunks = 20;
    static constexpr qint64 kToolProgressPersistMinIntervalMs = 1200;

    struct DelegateStats {
        int totalCount = 0;
        int successCount = 0;
        int failureCount = 0;
        qint64 totalDurationMs = 0;
    };

    IdentityManager* m_identityManager = nullptr;
    SessionManager* m_sessionManager = nullptr;
    ModelFactory* m_modelFactory = nullptr;
    ToolDispatcher* m_toolDispatcher = nullptr;
    McpToolProvider* m_mcpProvider = nullptr;
    std::unique_ptr<ChatPersistenceService> m_persistence;
    std::unique_ptr<ChatStateRepository> m_stateRepository;
    std::unique_ptr<MemoryManager> m_memoryManager;
    std::unique_ptr<HealthMonitor> m_healthMonitor;
    std::unique_ptr<HeartbeatService> m_heartbeatService;
    std::unique_ptr<SchedulerService> m_schedulerService;
    std::unique_ptr<TaskStateService> m_taskStateService;
    std::unique_ptr<AgentPulseRegistry> m_agentPulseRegistry;
    RuntimeManager* m_runtimeManager = nullptr;
    ConfigService* m_configService = nullptr;

    TurnManager m_turnManager;
    QHash<QString, QString> m_agentActiveSession;      // agentIdentityId -> running sessionId
    QHash<QString, int> m_memoryRetainedTurnsByAgent;  // agentIdentityId -> retained turn count
    QHash<QString, int> m_lastSavedMessageCounts;      // sessionId -> last persisted message count
    QHash<QString, qint64> m_delegateStartMsByToolKey; // "sessionId|toolId" -> start epoch ms
    QHash<QString, DelegateStats> m_delegateStatsBySession;
    QHash<QString, qint64> m_toolProgressLastPersistMsByKey;         // "sessionId|runId|toolName|toolId" -> epoch ms
    QHash<QString, QString> m_toolProgressLastDigestByKey;           // "sessionId|runId|toolName|toolId" -> digest
    QHash<QString, AgentPulse*> m_agentPulses;                       // agentIdentityId -> pulse instance
    QHash<QString, HeartbeatRuntimeState> m_heartbeatRuntimeByAgent; // agentId -> cached heartbeat state
    QHash<QString, QStringList> m_teammateInjections;                  // sessionId -> pending teammate reply contents
    QString m_currentSessionId;
    bool m_logVerboseStreamEvents = false;

    // 跨进程同步
    QTimer* m_syncTimer = nullptr;
    QHash<QString, qint64> m_lastSyncRowIds; // sessionId -> last polled rowid
    std::unique_ptr<AppEventHub> m_eventHub;
    std::unique_ptr<WorkspaceService> m_workspaceService;
    std::unique_ptr<ConversationService> m_conversationService;
    std::unique_ptr<GovernanceService> m_governanceService;
    std::unique_ptr<MemoryService> m_memoryService;
};

#endif // APPLICATIONSERVICES_H

