#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include "core/agent/ToolTypes.h"
#include "core/service/TurnManager.h"
#include <memory>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

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
class MemoryManager;

/**
 * @brief UI 层统一入口——桥接 UI 和 AgentRuntime
 *
 * ChatService 管理所有 AgentRuntime 实例，提供统一的消息发送、
 * 会话切换、中止请求等接口。UI 层只需与 ChatService 交互。
 */
class ChatService : public QObject {
    Q_OBJECT
public:
    explicit ChatService(QObject* parent = nullptr);
    ~ChatService() override;

    // ---- 初始化 ----
    void initialize();

    // ---- 用户消息 ----
    QString enqueueUserMessage(const QString& sessionId, const QString& text,
                               const QString& clientMessageId = QString());
    QString enqueueUserMessageAs(const QString& actorIdentityId,
                                 const QString& sessionId,
                                 const QString& text,
                                 const QString& clientMessageId = QString());
    void sendUserMessage(const QString& sessionId, const QString& text);
    void sendUserMessageAs(const QString& actorIdentityId,
                           const QString& sessionId,
                           const QString& text);
    void abortCurrent(const QString& sessionId);
    QString abortAndRollback(const QString& sessionId);

    // ---- 会话管理 ----
    Session* createNewSession(const QString& agentName = QString());
    Session* createSessionForIdentity(const QString& identityId, const QString& title = QString());
    Session* createSessionForIdentityAs(const QString& actorIdentityId,
                                        const QString& identityId,
                                        const QString& title = QString());
    QList<Session*> sessionsForIdentity(const QString& identityId) const;
    void removeSession(const QString& sessionId);
    bool removeSessionAs(const QString& actorIdentityId, const QString& sessionId);
    bool removeAgentMemoryAs(const QString& actorIdentityId, const QString& agentIdentityId);
    void switchSession(const QString& sessionId);
    QString currentSessionId() const;

    // ---- AgentRuntime 管理 ----
    AgentRuntime* runtimeForSession(const QString& sessionId) const;
    AgentRuntime* ensureRuntimeForSession(const QString& sessionId);

    // ---- 配置 ----
    void setDefaultAgentConfig(const LLMConfig& config);
    LLMConfig defaultAgentConfig() const;
    void applyConfigToAllRuntimes();
    void applyToolDispatcherToAllRuntimes();

    // ---- 查询 ----
    bool isSessionStreaming(const QString& sessionId) const;
    int pendingTurnCount(const QString& sessionId) const;
    QString activeRunId(const QString& sessionId) const;
    QString agentDisplayNameForSession(const QString& sessionId) const;
    bool canIdentityManageSessions(const QString& identityId) const;
    bool canIdentitySendMessage(const QString& identityId, const QString& sessionId = QString()) const;
    bool canIdentityManageGlobalConfig(const QString& identityId) const;

    // ---- 底层访问 ----
    ModelFactory* modelFactory() const;
    ToolDispatcher* toolDispatcher() const;
    McpToolProvider* mcpProvider() const;

    // ---- MCP 配置 ----
    void applyMcpConfig(const QStringList& specs);
    QStringList loadMcpConfigSpecs() const;
    bool saveMcpConfigSpecs(const QStringList& specs) const;
    QString mcpConfigPath() const;

    // ---- 模型配置 ----
    QString modelConfigPath() const;
    void loadConfig();

    // ---- 持久化 ----
    void saveSessionsToDisk();
    bool loadSessionsFromDisk();

    // ---- Tab 状态持久化 ----
    void saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId);
    struct TabState { QStringList openAgentIds; QString activeIdentityId; };
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

    // 会话事件
    void sessionCreated(const QString& sessionId);
    void sessionRemoved(const QString& sessionId);
    void configLoaded();

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
    void resetSessionStreamState(const QString& sessionId);
    void finalizeTurn(const QString& sessionId, TurnTask* outTurn);
    void flushPendingDeltaLog(const QString& sessionId,
                              SessionPipeline* pipeline,
                              const TurnTask* turn,
                              bool force);
    void emitPipelineEvent(const QString& type,
                           const QString& sessionId,
                           const TurnTask* turn = nullptr,
                           const QString& delta = QString(),
                           const QString& error = QString(),
                           const QJsonObject& extra = QJsonObject(),
                           bool persistToDisk = true);

    void onRuntimeStreamData(const QString& sessionId, const QString& data);
    void onRuntimeFinished(const QString& sessionId, const QString& fullContent);
    void onRuntimeError(const QString& sessionId, const QString& errorMsg);
    void onRuntimeToolCallsStarted(const QString& sessionId);
    void onRuntimeToolEvent(const QString& sessionId, const ToolExecutionEvent& event);

    void connectRuntimeSignals(AgentRuntime* runtime);
    bool isUserIdentity(const QString& identityId) const;

    void appendSessionMessageToDisk(const QString& sessionId, const Message& msg);
    bool appendEventLog(const QJsonObject& event) const;
    void ensureMemoryInitializedForAgent(Identity* agentIdentity);

    static constexpr int kSoftQueueDepth = 10;
    static constexpr int kHardQueueDepth = 200;
    static constexpr int kQueueMergeWindowMs = 2500;
    static constexpr int kQueueMergeMaxMergedMessages = 4;
    static constexpr int kQueueMergeMaxChars = 12000;
    static constexpr int kDeltaBatchFlushIntervalMs = 400;
    static constexpr int kDeltaBatchFlushChars = 120;
    static constexpr int kDeltaBatchFlushChunks = 20;

    IdentityManager* m_identityManager = nullptr;
    SessionManager* m_sessionManager = nullptr;
    ModelFactory* m_modelFactory = nullptr;
    ToolDispatcher* m_toolDispatcher = nullptr;
    McpToolProvider* m_mcpProvider = nullptr;
    std::unique_ptr<ChatPersistenceService> m_persistence;
    std::unique_ptr<ChatStateRepository> m_stateRepository;
    std::unique_ptr<MemoryManager> m_memoryManager;

    QHash<QString, AgentRuntime*> m_runtimes; // agentIdentityId -> AgentRuntime*
    TurnManager m_turnManager;
    QHash<QString, QString> m_agentActiveSession; // agentIdentityId -> running sessionId
    QHash<QString, int> m_lastSavedMessageCounts; // sessionId -> last persisted message count
    QString m_currentSessionId;
    LLMConfig m_defaultAgentConfig;
    bool m_logVerboseStreamEvents = false;
};

#endif // CHATSERVICE_H
