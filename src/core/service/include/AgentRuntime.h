#ifndef AGENTRUNTIME_H
#define AGENTRUNTIME_H

#include "core/agent/ToolTypes.h"
#include "llm/LLMTypes.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class Identity;
class LLMAgent;
class ToolDispatcher;
class ModelFactory;
class McpToolProvider;
class Session;

/**
 * @brief Agent 运行时——每个 Agent Identity 一个实例
 *
 * 持有 Identity + LLMAgent + ToolDispatcher（按 Identity 隔离）。
 * 负责 Agent 的 LLM 请求生命周期管理。
 */
class AgentRuntime : public QObject {
    Q_OBJECT
public:
    explicit AgentRuntime(Identity* identity, QObject* parent = nullptr);
    ~AgentRuntime() override;

    // ---- 基本属性 ----
    Identity* identity() const;
    LLMAgent* llmAgent() const;
    QString identityId() const;

    // ---- 消息发送 ----
    void sendMessage(const QString& sessionId, const QString& text);
    void abort();
    bool isStreaming() const;

    // ---- 配置 ----
    void setModelFactory(ModelFactory* factory);
    void setToolDispatcher(ToolDispatcher* dispatcher);
    void applyConfig();

    // ---- 会话管理 ----
    void switchToSession(const QString& sessionId);
    QString currentSessionId() const;

    // ---- LLMAgent 代理方法 ----
    void setHistory(const QJsonArray& history);
    QJsonArray getHistory() const;
    QJsonArray getIoHistory() const;
    void setIoContext(const QJsonObject& context);
    void appendIoHistoryEntry(const QString& sessionId, const QJsonObject& entry);
    void clearHistory();
    QString abortAndRollback();
    LLMConfig config() const;
    void setConfig(const LLMConfig& config);

signals:
    // 转发 LLMAgent 的信号，附加 sessionId
    void streamDataReceived(const QString& sessionId, const QString& data);
    void finished(const QString& sessionId, const QString& fullContent);
    void errorOccurred(const QString& sessionId, const QString& errorMsg);
    void toolCallsStarted(const QString& sessionId);
    void toolEvent(const QString& sessionId, const ToolExecutionEvent& event);

    // 思考状态 UI
    void reasoningStarted(const QString& sessionId);
    void reasoningStopped(const QString& sessionId);

private slots:
    void onStreamDataReceived(const QString& data);
    void onFinished(const QString& fullContent);
    void onErrorOccurred(const QString& errorMsg);
    void onToolCallsStarted();
    void onToolEvent(const ToolExecutionEvent& event);
    void onReasoningStarted();
    void onReasoningStopped();

private:
    void connectAgentSignals();
    void saveCurrentIoHistory();

    Identity* m_identity = nullptr;
    LLMAgent* m_llmAgent = nullptr;
    ToolDispatcher* m_toolDispatcher = nullptr;
    QString m_currentSessionId;
    bool m_isStreaming = false;
    QHash<QString, QJsonArray> m_sessionIoHistory; // sessionId -> io history
};

#endif // AGENTRUNTIME_H
