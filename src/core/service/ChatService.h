#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include "core/agent/ToolTypes.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class AgentRuntime;
class Identity;
class Session;
class ModelFactory;
class ToolDispatcher;
class McpToolProvider;
class IdentityManager;
class SessionManager;

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
    QString sessionsFilePath() const;
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
    struct TurnTask {
        QString requestTraceId;
        QString turnId;
        QString runId;
        QString clientMessageId;
        QString userContent;
        QString assistantContent;
    };

    struct SessionPipeline {
        QList<TurnTask> queue;
        TurnTask activeTurn;
        bool hasActiveTurn = false;
        quint64 seq = 0;
    };

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
    void emitPipelineEvent(const QString& type,
                           const QString& sessionId,
                           const TurnTask* turn = nullptr,
                           const QString& delta = QString(),
                           const QString& error = QString(),
                           const QJsonObject& extra = QJsonObject());

    void onRuntimeStreamData(const QString& sessionId, const QString& data);
    void onRuntimeFinished(const QString& sessionId, const QString& fullContent);
    void onRuntimeError(const QString& sessionId, const QString& errorMsg);
    void onRuntimeToolCallsStarted(const QString& sessionId);
    void onRuntimeToolEvent(const QString& sessionId, const ToolExecutionEvent& event);

    void connectRuntimeSignals(AgentRuntime* runtime);
    bool isUserIdentity(const QString& identityId) const;

    // ---- 目录化持久化路径 ----
    QString dataRootPath() const;
    QString configDirPath() const;
    QString appStatePath() const;
    QString manifestPath() const;
    QString identitiesDirPath() const;
    QString agentsDirPath() const;
    QString userIdentityPath() const;
    QString agentProfilePath(const QString& agentId) const;
    QString sessionsDirPath() const;
    QString sessionsIndexPath() const;
    QString sessionDataDirPath(const QString& sessionId) const;
    QString sessionMetaPath(const QString& sessionId) const;
    QString sessionMessagesPath(const QString& sessionId) const;
    QString sessionIoHistoryPath(const QString& sessionId) const;
    QString sessionPendingTurnsPath(const QString& sessionId) const;

    static constexpr int kSoftQueueDepth = 10;
    static constexpr int kHardQueueDepth = 200;

    IdentityManager* m_identityManager = nullptr;
    SessionManager* m_sessionManager = nullptr;
    ModelFactory* m_modelFactory = nullptr;
    ToolDispatcher* m_toolDispatcher = nullptr;
    McpToolProvider* m_mcpProvider = nullptr;

    QHash<QString, AgentRuntime*> m_runtimes; // agentIdentityId -> AgentRuntime*
    QHash<QString, SessionPipeline> m_pipelines; // sessionId -> command/turn pipeline
    QHash<QString, QString> m_agentActiveSession; // agentIdentityId -> running sessionId
    QString m_currentSessionId;
    LLMConfig m_defaultAgentConfig;
};

#endif // CHATSERVICE_H
