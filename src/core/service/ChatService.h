#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include "core/agent/ToolTypes.h"
#include <QHash>
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
    void sendUserMessage(const QString& sessionId, const QString& text);
    void abortCurrent(const QString& sessionId);
    QString abortAndRollback(const QString& sessionId);

    // ---- 会话管理 ----
    Session* createNewSession(const QString& agentName = QString());
    void removeSession(const QString& sessionId);
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
    QString agentDisplayNameForSession(const QString& sessionId) const;

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

signals:
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
    void connectRuntimeSignals(AgentRuntime* runtime);

    IdentityManager* m_identityManager = nullptr;
    SessionManager* m_sessionManager = nullptr;
    ModelFactory* m_modelFactory = nullptr;
    ToolDispatcher* m_toolDispatcher = nullptr;
    McpToolProvider* m_mcpProvider = nullptr;

    QHash<QString, AgentRuntime*> m_runtimes; // sessionId -> AgentRuntime*
    QString m_currentSessionId;
    LLMConfig m_defaultAgentConfig;
};

#endif // CHATSERVICE_H
