#include "LLMAgent.h"
#include "AgentEventBus.h"
#include "OpenAICompatibleClient.h"
#include "ToolDispatcher.h"
#include <QDebug>
#include <QTimer>

LLMAgent::LLMAgent(QObject* parent)
    : QObject(parent)
{
    // 默认创建 OpenAI 兼容客户端
    recreateClient(m_config);

    connect(AgentEventBus::instance(), &AgentEventBus::toolResultReady, this, &LLMAgent::submitToolResult);
}

void LLMAgent::recreateClient(const LLMConfig& config)
{
    if (m_llmClient) {
        m_llmClient->abort();
        m_llmClient->deleteLater();
        m_llmClient = nullptr;
    }

    // 工厂逻辑：根据 config.provider 创建对应的 Client
    // 目前仅支持 OpenAICompatible (包含 DeepSeek, OpenAI 等)
    // 后续可扩展 AnthropicClient, GeminiClient 等
    switch (config.provider) {
    case LLMConfig::ProviderType::Anthropic:
        // TODO: 实现 AnthropicClient
        // m_llmClient = new AnthropicClient(this);
        qWarning() << "Anthropic client not implemented yet, fallback to OpenAI Compatible";
        m_llmClient = new OpenAICompatibleClient(this);
        break;
    case LLMConfig::ProviderType::Google_Gemini:
        // TODO: 实现 GeminiClient
        qWarning() << "Gemini client not implemented yet, fallback to OpenAI Compatible";
        m_llmClient = new OpenAICompatibleClient(this);
        break;
    case LLMConfig::ProviderType::OpenAI_Compatible:
    default:
        m_llmClient = new OpenAICompatibleClient(this);
        break;
    }

    // 重新连接信号
    connect(m_llmClient, &ILLMClient::deltaReceived, this, &LLMAgent::onDeltaReceived);
    connect(m_llmClient, &ILLMClient::toolCallsReceived, this, &LLMAgent::onToolCallsReceived);
    connect(m_llmClient, &ILLMClient::finished, this, &LLMAgent::onClientFinished);
    connect(m_llmClient, &ILLMClient::errorOccurred, this, &LLMAgent::onClientError);

    qDebug() << "LLMAgent: Client recreated for provider" << (int)config.provider;
}

void LLMAgent::setSystemPrompt(const QString& prompt)
{
    m_systemPrompt = prompt;
}

void LLMAgent::setConfig(const LLMConfig& config)
{
    // 普通设置配置，不强制重建 Client (除非必要)
    // 如果 Provider 变了，建议用 reloadModel
    m_config = config;
    m_systemPrompt = config.systemPrompt;
}

void LLMAgent::reloadModel(const LLMConfig& newConfig)
{
    // 1. 检查是否需要切换底层 Client (协议变更)
    if (m_config.provider != newConfig.provider || !m_llmClient) {
        recreateClient(newConfig);
    }

    // 2. 上下文长度自适应 (Context Adaptation)
    // 如果新模型的上下文窗口比当前累积的历史小，进行裁剪
    // 估算策略: 粗略按 1汉字=2token, 1英语单词=1token 计算，或者简单按字符数/4
    // 这里采用简单策略：保留 System + User Last N + Assistant Last N
    // 真正的 Token 计算需要 Tokenizer，目前用防御性策略

    if (newConfig.maxContextTokens > 0) {
        int estimatedHistoryTokens = 0;
        for (const auto& msg : m_conversationHistory) {
            estimatedHistoryTokens += msg.toObject()["content"].toString().length(); // 字符充当 token 近似值(偏保守)
        }

        if (estimatedHistoryTokens > newConfig.maxContextTokens) {
            qWarning() << "LLMAgent: History too long for new model (" << estimatedHistoryTokens
                       << ">" << newConfig.maxContextTokens << "), truncating...";

            // 简单截断策略：保留最近 10 轮
            while (m_conversationHistory.size() > 20) { // 20条消息 = 10轮
                m_conversationHistory.removeFirst();
            }
        }
    }

    // 3. 更新配置
    m_config = newConfig;

    // 4. 更新 System Prompt (如果新配置为空则保留旧的，或者依业务决定)
    if (!newConfig.systemPrompt.isEmpty()) {
        m_systemPrompt = newConfig.systemPrompt;
    }

    qDebug() << "LLMAgent: Model reloaded. Vendor:" << m_config.vendor << "Model:" << m_config.model;
}

void LLMAgent::sendMessage(const QString& prompt)
{
    sendRequest(prompt, true);
}

void LLMAgent::askOnce(const QString& prompt)
{
    sendRequest(prompt, false);
}

void LLMAgent::sendRequest(const QString& prompt, bool saveToHistory)
{
    m_llmClient->abort();
    m_fullContent.clear();
    m_saveToHistory = saveToHistory;

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt;

    if (saveToHistory)
        m_conversationHistory.append(userMsg);

    m_currentMessages = buildMessageHistory(userMsg, saveToHistory);
    postRequestToServer(m_currentMessages);
}

QJsonArray LLMAgent::buildMessageHistory(const QJsonObject& userMsg, bool saveToHistory)
{
    QJsonArray messages;
    // 添加 System Prompt
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = m_systemPrompt;
    messages.append(systemMsg);

    if (saveToHistory) {
        for (const QJsonValue& msg : m_conversationHistory) {
            messages.append(msg);
        }
    } else {
        messages.append(userMsg);
    }
    return messages;
}

void LLMAgent::postRequestToServer(const QJsonArray& messages)
{
    if (!m_config.isValid()) {
        emit errorOccurred("API Key 未设置");
        return;
    }
    m_llmClient->postRequest(m_config, messages, m_tools);
}

void LLMAgent::onDeltaReceived(const QString& delta)
{
    if (delta.isEmpty())
        return;

    m_fullContent += delta;
    emit streamDataReceived(delta);
}

void LLMAgent::onToolCallsReceived(const QJsonArray& toolCalls)
{
    emit toolCallsStarted();

    // 助手消息需要包含工具调用
    QJsonObject assistantMsg;
    assistantMsg["role"] = "assistant";
    if (!m_fullContent.isEmpty())
        assistantMsg["content"] = m_fullContent;
    assistantMsg["tool_calls"] = toolCalls;

    // 记录到历史
    if (m_saveToHistory) {
        m_conversationHistory.append(assistantMsg);
    }

    // 清除内容缓存，防止叠加
    m_fullContent.clear();

    executeToolCalls(toolCalls);
}

void LLMAgent::onClientFinished(const QString& fullContent)
{
    if (m_isToolMode) {
        if (!m_waitingForToolResponse)
            return;
        m_isToolMode = false;
        m_waitingForToolResponse = false;
    }

    if (m_saveToHistory && !fullContent.isEmpty()) {
        QJsonObject assistantMsg;
        assistantMsg["role"] = "assistant";
        assistantMsg["content"] = fullContent;
        m_conversationHistory.append(assistantMsg);
    }
    emit finished(fullContent);
}

void LLMAgent::onClientError(const QString& errorMsg)
{
    m_isToolMode = false;
    m_waitingForToolResponse = false;
    emit errorOccurred(errorMsg);
}

void LLMAgent::executeToolCalls(const QJsonArray& toolCalls)
{
    m_isToolMode = true;
    m_waitingForToolResponse = false;
    m_pendingToolCalls.clear();
    m_toolResults.clear();

    if (!m_toolDispatcher)
        return;

    for (const QJsonValue& item : toolCalls) {
        ToolCall call = ToolCall::fromDeepSeekJson(item.toObject());
        m_pendingToolCalls.append(call);

        // 发送开始事件
        ToolExecutionEvent startEvent(call);
        emit toolEvent(startEvent);
        AgentEventBus::instance()->postToolEvent(startEvent);

        // 执行并获取结构化结果
        ToolResult result = m_toolDispatcher->dispatch(call);

        if (isDeferredToolResult(result.rawContent)) {
            m_deferredToolIds.insert(call.id);

            ToolExecutionEvent progressEvent;
            progressEvent.toolName = call.name;
            progressEvent.toolId = call.id;
            progressEvent.status = "progress";
            progressEvent.success = true;
            progressEvent.rawResult = result.rawContent;
            progressEvent.formattedResult = stripDeferredToolPrefix(result.rawContent);
            emit toolEvent(progressEvent);
            AgentEventBus::instance()->postToolEvent(progressEvent);
            continue;
        }

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

void LLMAgent::submitToolResult(const QString& toolId, const QString& result)
{
    m_toolResults[toolId] = result;

    if (m_deferredToolIds.contains(toolId)) {
        QString toolName;
        for (const auto& call : m_pendingToolCalls) {
            if (call.id == toolId) {
                toolName = call.name;
                break;
            }
        }

        ToolExecutionEvent endEvent;
        endEvent.toolName = toolName.isEmpty() ? QString("tool") : toolName;
        endEvent.toolId = toolId;
        endEvent.status = "completed";
        endEvent.success = !result.startsWith("错误");
        endEvent.rawResult = result;
        endEvent.formattedResult = result;
        emit toolEvent(endEvent);
        AgentEventBus::instance()->postToolEvent(endEvent);

        m_deferredToolIds.remove(toolId);
    }

    // 检查是否全部完成
    bool allDone = true;
    for (const auto& call : m_pendingToolCalls) {
        if (!m_toolResults.contains(call.id)) {
            allDone = false;
            break;
        }
    }

    if (allDone) {
        // 构建并将工具反馈消息加入历史
        for (const auto& call : m_pendingToolCalls) {
            QJsonObject toolMsg;
            toolMsg["role"] = "tool";
            toolMsg["tool_call_id"] = call.id;
            toolMsg["content"] = m_toolResults[call.id].left(2000); // 截断保护

            if (m_saveToHistory) {
                m_conversationHistory.append(toolMsg);
            }
        }

        m_waitingForToolResponse = true;

        // 重新构建消息序列并请求
        QTimer::singleShot(0, this, [this]() {
            m_currentMessages = buildMessageHistory(QJsonObject(), m_saveToHistory);
            postRequestToServer(m_currentMessages);
        });
    }
}

void LLMAgent::abort()
{
    qDebug() << "LLMAgent: [Action] Core agent logic is aborting...";
    m_llmClient->abort();

    // 关键修复：重置所有中间状态，防止中断后又因定时器或回调恢复执行
    m_isToolMode = false;
    m_waitingForToolResponse = false;
    m_pendingToolCalls.clear();
    m_deferredToolIds.clear();
    m_toolResults.clear();
}

QString LLMAgent::abortAndRollback()
{
    // 1. 先执行标准中断
    abort();

    // 2. 回滚本轮对话消息，从后往前查找并移除
    QString userContent;

    // 从历史末尾开始，移除本轮所有消息（user、assistant、tool）
    while (!m_conversationHistory.isEmpty()) {
        QJsonObject lastMsg = m_conversationHistory.last().toObject();
        QString role = lastMsg["role"].toString();

        if (role == "user") {
            // 找到本轮的用户消息，记录内容后移除
            userContent = lastMsg["content"].toString();
            m_conversationHistory.removeLast();
            break; // 回滚完成
        } else if (role == "assistant" || role == "tool") {
            // 移除 assistant 或 tool 消息
            m_conversationHistory.removeLast();
        } else {
            // 遇到其他类型消息，停止回滚
            break;
        }
    }

    // 3. 清空当前消息缓存
    m_currentMessages = QJsonArray();
    m_fullContent.clear();

    qDebug() << "LLMAgent: [Rollback] 已回滚本轮对话，用户消息:" << userContent.left(50);

    return userContent;
}

void LLMAgent::clearHistory()
{
    m_conversationHistory = QJsonArray();
    m_currentMessages = QJsonArray();
    m_isToolMode = false;
}

void LLMAgent::setHistory(const QJsonArray& h)
{
    m_conversationHistory = h;
    m_currentMessages = QJsonArray();
    m_isToolMode = false;
}

// ... 其他辅助函数 (getHistory, registerTool 等) 保持微调
QJsonArray LLMAgent::getHistory() const { return m_conversationHistory; }
int LLMAgent::getConversationCount() const { return m_conversationHistory.size() / 2; }
void LLMAgent::registerTool(const Tool& tool) { m_tools.append(tool); }
void LLMAgent::clearTools() { m_tools.clear(); }

void LLMAgent::setToolDispatcher(ToolDispatcher* d)
{
    m_toolDispatcher = d;
    if (d) {
        clearTools();

        // 1. 注册基础工具
        for (const auto& t : d->getAllToolSchemas())
            registerTool(t);

        // 2. 注册 Agent 委派工具 (根据当前配置深度决策)
        // 注意：ToolDispatcher 只是负责创建工具实例并注册到它自己的 registry
        // 这里我们需要显式触发 Dispatcher 去根据 Config 生成 ToolEntry
        // 然后再次获取 all schemas。
        // 但目前的 ToolDispatcher::registerAgentTools 是将 Tool 注册进 Dispatcher。
        // 所以逻辑应该是：

        d->registerAgentTools(m_config);

        // 重新获取所有工具 (包含刚才新注册的 AgentTool)
        clearTools(); // 重新清空，以免重复 (虽然上面已经 register 了一遍，但为了安全)
        for (const auto& t : d->getAllToolSchemas())
            registerTool(t);
    }
}

QList<Tool> LLMAgent::getTools() const { return m_tools; }

void LLMAgent::setRecursionDepth(int depth)
{
    m_config.recursionDepth = depth;

    // 每次深度变化，可能需要重新注册/注销委派工具
    // 但为简化实现，我们假设深度设置发生在 setToolDispatcher 之前
    // 或者用户可以在运行中动态调用 setToolDispatcher 来刷新工具
    // 如果想要动态生效，这里应该通知 Dispatcher。

    if (m_toolDispatcher) {
        // 简单暴力刷新：重新设置一遍 dispatch
        setToolDispatcher(m_toolDispatcher);
    }
}
