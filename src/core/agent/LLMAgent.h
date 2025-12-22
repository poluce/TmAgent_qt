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

class QTimer;  // 前向声明

// LLM 配置结构体
struct LLMConfig {
    QString apiKey;      // API 密钥
    QString baseUrl;     // API 基础地址
    QString model;       // 模型名称
    
    // 默认构造函数
    LLMConfig() 
        : baseUrl("https://api.deepseek.com")
        , model("deepseek-chat") {}
    
    // 带参数构造函数
    LLMConfig(const QString& key, const QString& url, const QString& mdl)
        : apiKey(key), baseUrl(url), model(mdl) {}
    
    // 检查配置是否有效
    bool isValid() const {
        return !apiKey.isEmpty() && !baseUrl.isEmpty() && !model.isEmpty();
    }
};

// 工具定义结构体
struct Tool {
    QString name;           // 工具名称
    QString description;    // 工具描述
    QJsonObject inputSchema; // 输入参数 JSON Schema
    
    // 转换为 DeepSeek API 格式 (OpenAI 兼容)
    QJsonObject toJson() const {
        QJsonObject functionObj;
        functionObj["name"] = name;
        functionObj["description"] = description;
        functionObj["parameters"] = inputSchema;  // DeepSeek 使用 parameters
        
        QJsonObject obj;
        obj["type"] = "function";  // DeepSeek 需要包装在 function 中
        obj["function"] = functionObj;
        return obj;
    }
};


// 工具调用请求结构体
struct ToolCall {
    QString id;             // 工具调用 ID
    QString name;           // 工具名称
    QJsonObject input;      // 输入参数
};


class LLMAgent : public QObject {
    Q_OBJECT
public:
    explicit LLMAgent(QObject *parent = nullptr);
    
    // 发送消息，支持多轮对话上下文（后续可加）
    void ask(const QString& prompt);
    
    // 单次问答,不保存对话历史(适用于短期调用、工具调用等场景)
    void askOnce(const QString& prompt);
    
    // 设置 Agent 的角色 (System Prompt)
    void setSystemPrompt(const QString& prompt);
    QString systemPrompt() const { return m_systemPrompt; }
    
    // 设置/获取 LLM 配置
    void setConfig(const LLMConfig& config);
    LLMConfig getConfig() const;

    // 对话历史管理
    void clearHistory();                    // 清空对话历史
    QJsonArray getHistory() const;          // 获取对话历史
    int getConversationCount() const;       // 获取对话轮数

    // 中断请求
    void abort();

    // 工具管理
    void registerTool(const Tool& tool);           // 注册工具
    void clearTools();                             // 清空所有工具
    QList<Tool> getTools() const;                  // 获取已注册的工具列表
    
    // 带工具的问答
    void askWithTools(const QString& prompt);

signals:
    void chunkReceived(const QString& chunk);    // 收到文本片段
    void finished(const QString& fullContent);   // 请求圆满结束
    
    // 工具调用相关信号
    void toolCallRequested(const QString& toolId,
                          const QString& toolName,
                          const QJsonObject& input);  // Claude 请求调用工具
    void errorOccurred(const QString& errorMsg); // 发生错误

public slots:
    // 提交工具执行结果
    void submitToolResult(const QString& toolId, const QString& result);

private slots:
    void onReadyRead();
    void onFinished();
    void onError(QNetworkReply::NetworkError code);

private:
    // 通用请求发送函数
    void sendRequest(const QString& prompt, bool saveToHistory);
    
    // 工具相关的内部函数
    void sendRequestWithTools(const QJsonArray& messages);
    void parseNonStreamResponse(const QJsonObject& response);
    void handleToolUseResponse(const QJsonArray& content);
    void continueConversationWithToolResults();

    QNetworkAccessManager *m_manager;
    QNetworkReply *m_currentReply = nullptr;
    QTimer *m_timeoutTimer = nullptr;  // 超时定时器
    QString m_fullContent;
    QString m_systemPrompt;
    QJsonArray m_conversationHistory;  // 对话历史
    LLMConfig m_config;                // LLM 配置
    bool m_saveToHistory = true;       // 是否保存到对话历史
    
    // 工具相关成员变量
    QList<Tool> m_tools;               // 已注册的工具列表
    QList<ToolCall> m_pendingToolCalls; // 待处理的工具调用
    QJsonArray m_currentMessages;      // 当前对话的完整消息历史
    QMap<QString, QString> m_toolResults; // 工具执行结果 (toolId -> result)
    bool m_isToolMode = false;         // 是否处于工具调用模式
};

#endif // LLMAGENT_H
