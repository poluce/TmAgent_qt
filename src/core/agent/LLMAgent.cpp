#include "LLMAgent.h"
#include "ToolDispatcher.h"
#include "DeepSeekClient.h"
#include "AgentEventBus.h"
#include <QTimer>
#include <QDebug>

LLMAgent::LLMAgent(QObject *parent) : QObject(parent) {
    // 初始化协议客户端
    m_llmClient = new DeepSeekClient(this);
    
    connect(m_llmClient, &ILLMClient::deltaReceived, this, &LLMAgent::onDeltaReceived);
    connect(m_llmClient, &ILLMClient::toolCallsReceived, this, &LLMAgent::onToolCallsReceived);
    connect(m_llmClient, &ILLMClient::finished, this, &LLMAgent::onClientFinished);
    connect(m_llmClient, &ILLMClient::errorOccurred, this, &LLMAgent::onClientError);
}

void LLMAgent::setSystemPrompt(const QString& prompt) {
    m_systemPrompt = prompt;
}

void LLMAgent::setConfig(const LLMConfig& config) {
    m_config = config;
    m_systemPrompt = config.systemPrompt;
}

void LLMAgent::sendMessage(const QString& prompt) {
    sendRequest(prompt, true);
}

void LLMAgent::askOnce(const QString& prompt) {
    sendRequest(prompt, false);
}

void LLMAgent::sendRequest(const QString& prompt, bool saveToHistory) {
    m_llmClient->abort();
    m_fullContent.clear();
    m_saveToHistory = saveToHistory;
    
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt;
    
    if (saveToHistory) m_conversationHistory.append(userMsg);
    
    m_currentMessages = buildMessageHistory(userMsg, saveToHistory);
    postRequestToServer(m_currentMessages);
}

QJsonArray LLMAgent::buildMessageHistory(const QJsonObject& userMsg, bool saveToHistory) {
    QJsonArray messages;
    // 添加 System Prompt
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = m_systemPrompt;
    messages.append(systemMsg);

    if (m_isToolMode) {
        // 工具模式逻辑保留
        messages = m_currentMessages; 
    } else if (saveToHistory) {
        for (const QJsonValue& msg : m_conversationHistory) messages.append(msg);
    } else {
        messages.append(userMsg);
    }
    return messages;
}

void LLMAgent::postRequestToServer(const QJsonArray& messages) {
    if (!m_config.isValid()) {
        emit errorOccurred("API Key 未设置");
        return;
    }
    m_llmClient->postRequest(m_config, messages, m_tools);
}

void LLMAgent::onDeltaReceived(const QString& delta) {
    m_fullContent += delta;
    emit streamDataReceived(delta);
}

void LLMAgent::onToolCallsReceived(const QJsonArray& toolCalls) {
    // 助手消息需要包含工具调用
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    if (!m_fullContent.isEmpty()) assistantMsg["content"] = m_fullContent;
    assistantMsg["tool_calls"] = toolCalls;
    m_currentMessages.append(assistantMsg);

    executeToolCalls(toolCalls);
}

void LLMAgent::onClientFinished(const QString& fullContent) {
    if (m_isToolMode) {
        // 工具模式下，第一段回复只包含工具调用；等待工具结果后的最终回复才结束
        if (!m_waitingForToolResponse) return;
        m_isToolMode = false;
        m_waitingForToolResponse = false;
    }
    
    if (m_saveToHistory) {
        QJsonObject assistantMsg;
        assistantMsg["role"] = "assistant";
        assistantMsg["content"] = fullContent;
        m_conversationHistory.append(assistantMsg);
    }
    emit finished(fullContent);
}

void LLMAgent::onClientError(const QString& errorMsg) {
    m_isToolMode = false;
    m_waitingForToolResponse = false;
    emit errorOccurred(errorMsg);
}

void LLMAgent::executeToolCalls(const QJsonArray& toolCalls) {
    m_isToolMode = true;
    m_waitingForToolResponse = false;
    m_pendingToolCalls.clear();
    m_toolResults.clear();

    if (!m_toolDispatcher) return;

    for (const QJsonValue& item : toolCalls) {
        ToolCall call = ToolCall::fromDeepSeekJson(item.toObject());
        m_pendingToolCalls.append(call);
        
        // 发送开始事件
        ToolExecutionEvent startEvent(call);
        emit toolEvent(startEvent);
        AgentEventBus::instance()->postToolEvent(startEvent);

        // 执行并获取结构化结果
        ToolResult result = m_toolDispatcher->dispatch(call);
        
        // 标准化结果通知
        ToolExecutionEvent endEvent;
        endEvent.toolName = call.name;
        endEvent.toolId = call.id;
        endEvent.status = "completed";
        endEvent.success = result.success;
        endEvent.rawResult = result.rawContent;
        endEvent.formattedResult = result.userSummary; // 使用工具自带的摘要
        
        emit toolEvent(endEvent);
        AgentEventBus::instance()->postToolEvent(endEvent);

        // 提交到 Agent 上下文
        submitToolResult(call.id, result.rawContent);
    }
}

void LLMAgent::submitToolResult(const QString& toolId, const QString& result) {
    m_toolResults[toolId] = result;
    
    // 检查是否全部完成
    bool allDone = true;
    for (const auto& call : m_pendingToolCalls) {
        if (!m_toolResults.contains(call.id)) { allDone = false; break; }
    }

    if (allDone) {
        // 构建工具反馈消息
        for (const auto& call : m_pendingToolCalls) {
            QJsonObject toolMsg;
            toolMsg["role"] = "tool";
            toolMsg["tool_call_id"] = call.id;
            toolMsg["content"] = m_toolResults[call.id].left(2000); // 截断保护
            m_currentMessages.append(toolMsg);
        }
        m_waitingForToolResponse = true;
        QTimer::singleShot(0, this, [this]() {
            postRequestToServer(m_currentMessages);
        });
    }
}

void LLMAgent::abort() {
    m_llmClient->abort();
}

void LLMAgent::clearHistory() {
    m_conversationHistory = QJsonArray();
    m_currentMessages = QJsonArray();
    m_isToolMode = false;
}

// ... 其他辅助函数 (getHistory, registerTool 等) 保持微调
QJsonArray LLMAgent::getHistory() const { return m_conversationHistory; }
int LLMAgent::getConversationCount() const { return m_conversationHistory.size() / 2; }
void LLMAgent::registerTool(const Tool& tool) { m_tools.append(tool); }
void LLMAgent::clearTools() { m_tools.clear(); }
void LLMAgent::setToolDispatcher(ToolDispatcher* d) { 
    m_toolDispatcher = d; 
    if (d) {
        clearTools();
        for (const auto& t : d->getAllToolSchemas()) registerTool(t);
    }
}
QList<Tool> LLMAgent::getTools() const { return m_tools; }
