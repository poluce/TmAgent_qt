#ifndef LLMAGENT_H
#define LLMAGENT_H

#include "ToolTypes.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>

class QTimer;
class ToolDispatcher;
class ModelFactory;
class LLMProvider;

class LLMAgent : public QObject {
    Q_OBJECT
public:
    explicit LLMAgent(QObject* parent = nullptr);

    /**
     * @brief 设置模型工厂，用于按模型类型获取 LLMProvider（必须设置后才能发请求）
     */
    void setModelFactory(ModelFactory* factory);
    ModelFactory* modelFactory() const { return m_modelFactory; }

    /**
     * @brief 当前 Agent 使用的模型类型（供 ModelFactory 按类型分配 Provider）
     */
    QString modelId() const;

    // 发送消息，支持多轮对话上下文
    void sendMessage(const QString& prompt);

    // 单次问答,不保存对话历史(适用于短期调用、工具调用等场景)
    void askOnce(const QString& prompt);

    // 设置 Agent 的角色 (System Prompt)
    void setSystemPrompt(const QString& prompt);
    QString systemPrompt() const { return m_systemPrompt; }

    // 对话历史管理
    void clearHistory();                   // 清空对话历史
    void setHistory(const QJsonArray& h);  // 恢复对话历史（用于会话切换）
    QJsonArray getHistory() const;         // 获取对话历史
    int getConversationCount() const; // 获取对话轮数

    // 请求/响应 JSON 历史
    void setIoHistory(const QJsonArray& h);
    QJsonArray getIoHistory() const;

    // 中断请求
    void abort();

    // 中断请求并回滚本轮对话，返回被回滚的用户消息内容
    QString abortAndRollback();

    // 工具管理
    QList<Tool> getTools() const; // 获取已注册的工具列表

    /**
     * @brief 设置工具调度器（Agent 自治执行工具调用）
     * @param dispatcher 工具调度器指针（生命周期由外部管理）
     * @param allowedTools 可选工具白名单。空列表表示允许 dispatcher 中的全部工具。
     * @note 会自动从 dispatcher 获取并注册所有工具 Schema
     */
    void setToolDispatcher(ToolDispatcher* dispatcher,
                           const QStringList& allowedTools = QStringList());

    // 配置管理
    void setConfig(const LLMConfig& config);
    LLMConfig config() const { return m_config; }

    /**
     * @brief 热切换模型（保留对话历史）
     * @param newConfig 新的配置信息。下次发请求时经 ModelFactory 按 newConfig.modelId 取 Provider。
     */
    void reloadModel(const LLMConfig& newConfig);

    /**
     * @brief 设置递归深度 (用于动态能力剥夺)
     * @param depth 剩余深度 (0 表示剥夺委派能力)
     */
    void setRecursionDepth(int depth);

signals:
    void streamDataReceived(const QString& data); // 收到流式字节流数据
    void finished(const QString& fullContent);    // 请求圆满结束

    // 错误信号
    void errorOccurred(const QString& errorMsg); // 发生错误

    // 工具事件信号（结构化事件，统一处理）
    void toolEvent(const ToolExecutionEvent& event);

    // 工具调用开始（用于 UI 丢弃草稿）
    void toolCallsStarted();

public slots:
    // 提交工具执行结果
    void submitToolResult(const QString& toolId, const QString& result);

private:
    // 内部发送流程
    void sendRequest(const QString& prompt, bool saveToHistory);

    // 准备发送的消息列表（处理工具模式和历史记录）
    QJsonArray buildMessageHistory(const QJsonObject& userMsg, bool saveToHistory);

    // 统一的内部发送函数（已注册工具会自动带上）
    void postRequestToServer(const QJsonArray& messages);
    void executeToolCalls(const QJsonArray& toolCalls);
    void resumeAfterToolExecution();

    // 阶段一:结果格式化和智能摘要
    QString formatToolResultSummary(const QString& toolName, const QString& rawResult);
    QString summarizeCommandOutput(const QString& cmdOutput);
    QString summarizeFileOperation(const QString& fileResult);
    bool isToolEnabled(const QString& toolName) const;

    void registerTool(const Tool& tool);
    void clearTools();

    // 内部槽函数：处理来自 LLMProvider 的事件
    void onDeltaReceived(const QString& delta);
    void onToolCallsReceived(const QJsonArray& toolCalls);
    void onClientFinished(const QString& fullContent);
    void onClientError(const QString& errorMsg);

    void recordRequestJson(const QJsonObject& request, const QString& requestId, const QString& modelId);
    QJsonObject buildResponseJson(const QString& content,
                                  const QJsonArray& toolCalls,
                                  const QString& finishReason,
                                  const QString& requestId,
                                  const QString& modelId) const;
    void recordResponseJson(const QJsonObject& response);
    void recordErrorJson(const QString& errorMsg);
    void resetToolLoopGuards();
    void resetToolState();
    QString buildToolRoundSignature(const QList<ToolCall>& calls) const;

    QString m_fullContent;
    QString m_systemPrompt;
    QJsonArray m_conversationHistory; // 对话历史
    bool m_saveToHistory = true;      // 是否保存到对话历史

    QJsonArray m_ioHistory;
    int m_pendingIoIndex = -1;
    QString m_pendingRequestId;
    QString m_pendingModelId;

    // 工具相关成员变量
    QList<Tool> m_tools;                   // 已注册的工具列表
    QSet<QString> m_enabledToolNames;      // 已启用工具名（用于执行前权限校验）
    QList<ToolCall> m_pendingToolCalls;    // 待处理的工具调用
    QJsonArray m_currentMessages;          // 当前对话的完整消息历史
    QMap<QString, QString> m_toolResults;  // 工具执行结果 (toolId -> result)
    bool m_isToolMode = false;             // 是否处于工具调用模式
    bool m_waitingForToolResponse = false; // 是否等待工具结果后的最终回复
    QSet<QString> m_deferredToolIds;       // 延迟完成的工具调用 ID

    // 流式工具调用累积变量
    QString m_lastFinishReason;          // 最后的 finish_reason
    QJsonArray m_streamingToolCallsJson; // 累积的工具调用 JSON 片段

    // 工具调度器（Agent 自治执行）
    ToolDispatcher* m_toolDispatcher = nullptr;
    ModelFactory* m_modelFactory = nullptr;

    // 当前请求使用的 Provider（由 Agent 拥有，parent = this）
    LLMProvider* m_currentProvider = nullptr;

    // Agent 配置
    LLMConfig m_config;

    // 请求代次：每次发起新请求或中断都会递增，用于丢弃旧 Provider 的晚到事件。
    quint64 m_dispatchToken = 0;

    // 工具循环熔断（单 turn）
    static constexpr int kMaxToolRoundsPerTurn = 15;
    static constexpr int kMaxConsecutiveSameToolRounds = 6;
    static constexpr int kMaxConsecutiveNoProgressRounds = 4;
    static constexpr qint64 kMaxToolLoopTimeMs = 120000; // 2 分钟

    int m_toolRoundCount = 0;
    int m_consecutiveSameToolRounds = 0;
    int m_consecutiveNoProgressRounds = 0;
    QString m_lastToolRoundSignature;
    QString m_lastPrimaryToolName;
    QElapsedTimer m_toolLoopTimer;
};

#endif // LLMAGENT_H
