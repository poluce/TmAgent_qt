#ifndef LLMAGENT_H
#define LLMAGENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include "ToolTypes.h"

class QTimer;
class ToolDispatcher;
class ILLMClient;


class LLMAgent : public QObject {
    Q_OBJECT
public:
    explicit LLMAgent(QObject *parent = nullptr);
    
    // 发送消息，支持多轮对话上下文
    void sendMessage(const QString& prompt);
    
    // 单次问答,不保存对话历史(适用于短期调用、工具调用等场景)
    void askOnce(const QString& prompt);
    
    // 设置 Agent 的角色 (System Prompt)
    void setSystemPrompt(const QString& prompt);
    QString systemPrompt() const { return m_systemPrompt; }

    // 对话历史管理
    void clearHistory();                    // 清空对话历史
    QJsonArray getHistory() const;          // 获取对话历史
    int getConversationCount() const;       // 获取对话轮数

    // 中断请求
    void abort();

    // 工具管理
    QList<Tool> getTools() const;                  // 获取已注册的工具列表
    
    /**
     * @brief 设置工具调度器（Agent 自治执行工具调用）
     * @param dispatcher 工具调度器指针（生命周期由外部管理）
     * @note 会自动从 dispatcher 获取并注册所有工具 Schema
     */
    void setToolDispatcher(ToolDispatcher* dispatcher);

    // 配置管理
    void setConfig(const LLMConfig& config);
    LLMConfig config() const { return m_config; }

signals:
    void streamDataReceived(const QString& data);    // 收到流式字节流数据
    void finished(const QString& fullContent);   // 请求圆满结束
    
    // 错误信号
    void errorOccurred(const QString& errorMsg); // 发生错误
    
    // 工具事件信号（结构化事件，统一处理）
    void toolEvent(const ToolExecutionEvent& event);

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
    QString formatToolResultSummary(const QString& toolName, 
                                    const QString& rawResult);
    QString summarizeCommandOutput(const QString& cmdOutput);
    QString summarizeFileOperation(const QString& fileResult);
    
    void registerTool(const Tool& tool);
    void clearTools();

    // 内部槽函数：处理来自 Client 的事件
    void onDeltaReceived(const QString& delta);
    void onToolCallsReceived(const QJsonArray& toolCalls);
    void onClientFinished(const QString& fullContent);
    void onClientError(const QString& errorMsg);

    QString m_fullContent;
    QString m_systemPrompt;
    QJsonArray m_conversationHistory;  // 对话历史
    bool m_saveToHistory = true;       // 是否保存到对话历史
    
    // 工具相关成员变量
    QList<Tool> m_tools;               // 已注册的工具列表
    QList<ToolCall> m_pendingToolCalls; // 待处理的工具调用
    QJsonArray m_currentMessages;      // 当前对话的完整消息历史
    QMap<QString, QString> m_toolResults; // 工具执行结果 (toolId -> result)
    bool m_isToolMode = false;         // 是否处于工具调用模式
    bool m_waitingForToolResponse = false; // 是否等待工具结果后的最终回复
    
    // 流式工具调用累积变量
    QString m_lastFinishReason;        // 最后的 finish_reason
    QJsonArray m_streamingToolCallsJson; // 累积的工具调用 JSON 片段
    
    // 工具调度器（Agent 自治执行）
    ToolDispatcher* m_toolDispatcher = nullptr;
    ILLMClient* m_llmClient = nullptr;
    
    // Agent 配置
    LLMConfig m_config;

};

#endif // LLMAGENT_H
