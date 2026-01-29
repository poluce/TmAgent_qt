#include "LLMAgent.h"
#include "AgentEventBus.h"
#include "ToolDispatcher.h"
#include "newCore/ModelFactory.h"
#include "newCore/LLMProvider.h"
#include "newCore/LLMTypes.h"
#include <QDebug>
#include <QTimer>
#include <QUuid>

LLMAgent::LLMAgent(QObject* parent)
    : QObject(parent)
{
    connect(AgentEventBus::instance(), &AgentEventBus::toolResultReady, this, &LLMAgent::submitToolResult);
}

void LLMAgent::setModelFactory(ModelFactory* factory)
{
    m_modelFactory = factory;
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
    // 上下文长度自适应：若新模型上下文更小则裁剪历史
    if (newConfig.maxContextTokens > 0) {
        int estimatedHistoryTokens = 0;
        for (const auto& msg : m_conversationHistory) {
            estimatedHistoryTokens += msg.toObject()["content"].toString().length();
        }
        if (estimatedHistoryTokens > newConfig.maxContextTokens) {
            qWarning() << "LLMAgent: History too long for new model (" << estimatedHistoryTokens
                       << ">" << newConfig.maxContextTokens << "), truncating...";
            while (m_conversationHistory.size() > 20) {
                m_conversationHistory.removeFirst();
            }
        }
    }

    m_config = newConfig;
    if (!newConfig.systemPrompt.isEmpty()) {
        m_systemPrompt = newConfig.systemPrompt;
    }
    qDebug() << "LLMAgent: Model reloaded. Model:" << m_config.model;
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
    if (m_currentProvider) {
        m_currentProvider->abort();
    }
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
    if (!m_modelFactory) {
        emit errorOccurred("未设置 ModelFactory");
        return;
    }

    // 清理旧的 Provider（如果存在）
    if (m_currentProvider) {
        m_currentProvider->deleteLater();
        m_currentProvider = nullptr;
    }

    // 创建新的 Provider 实例（parent = this，自动管理生命周期）
    m_currentProvider = m_modelFactory->createProvider(modelId(), this);
    if (!m_currentProvider) {
        emit errorOccurred("未找到可用模型: " + modelId());
        return;
    }

    // 连接信号（无需手动管理连接，parent-child 关系会自动处理）
    connect(m_currentProvider, &LLMProvider::deltaReceived, this, &LLMAgent::onDeltaReceived);
    connect(m_currentProvider, &LLMProvider::toolCallsReceived, this, &LLMAgent::onToolCallsReceived);
    connect(m_currentProvider, &LLMProvider::streamComplete, this, [this](const QString& c, const LLMUsage&) {
        onClientFinished(c);
    });
    connect(m_currentProvider, &LLMProvider::errorOccurred, this, [this](const LLMError& err) {
        onClientError(err.userMessage);
    });

    // 构建并发送请求
    LLMRequest request;
    request.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    request.traceId = request.requestId;
    request.modelId = modelId();
    request.capabilities << Capability::TextGeneration << Capability::ToolCalling;
    request.stream = true;
    request.messages = messages;
    request.temperature = m_config.temperature;
    request.maxTokens = m_config.maxTokens;
    request.timeoutMs = m_config.timeoutMs;
    for (const Tool& t : m_tools) {
        request.tools.append(t.toJson());
    }

    QJsonObject requestJson;
    requestJson["model"] = request.modelId;
    requestJson["max_tokens"] = request.maxTokens;
    requestJson["temperature"] = request.temperature;
    requestJson["stream"] = request.stream;
    requestJson["messages"] = request.messages;
    if (!request.tools.isEmpty())
        requestJson["tools"] = request.tools;

    recordRequestJson(requestJson, request.requestId, request.modelId);
    m_currentProvider->generateStream(request);
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
    const QString capturedContent = m_fullContent;
    QJsonObject responseJson = buildResponseJson(capturedContent,
                                                 toolCalls,
                                                 QStringLiteral("tool_calls"),
                                                 m_pendingRequestId,
                                                 m_pendingModelId);
    recordResponseJson(responseJson);

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
    QJsonObject responseJson = buildResponseJson(fullContent,
                                                 QJsonArray(),
                                                 QStringLiteral("stop"),
                                                 m_pendingRequestId,
                                                 m_pendingModelId);
    recordResponseJson(responseJson);
    emit finished(fullContent);
}

void LLMAgent::onClientError(const QString& errorMsg)
{
    m_isToolMode = false;
    m_waitingForToolResponse = false;
    recordErrorJson(errorMsg);
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
    if (!m_isToolMode || m_waitingForToolResponse) {
        qDebug() << "LLMAgent: 忽略过期工具结果" << toolId;
        return;
    }

    bool isPending = false;
    for (const auto& call : m_pendingToolCalls) {
        if (call.id == toolId) {
            isPending = true;
            break;
        }
    }
    if (!isPending) {
        qDebug() << "LLMAgent: 忽略未知 tool_call_id" << toolId;
        return;
    }
    if (m_toolResults.contains(toolId)) {
        qDebug() << "LLMAgent: 忽略重复工具结果" << toolId;
        return;
    }

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
    if (m_currentProvider) {
        m_currentProvider->abort();
    }
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
    m_ioHistory = QJsonArray();
    m_pendingIoIndex = -1;
    m_pendingRequestId.clear();
    m_pendingModelId.clear();
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

void LLMAgent::setIoHistory(const QJsonArray& h)
{
    m_ioHistory = h;
    m_pendingIoIndex = -1;
    m_pendingRequestId.clear();
    m_pendingModelId.clear();
}

QJsonArray LLMAgent::getIoHistory() const
{
    return m_ioHistory;
}

QJsonObject LLMAgent::buildResponseJson(const QString& content,
                                        const QJsonArray& toolCalls,
                                        const QString& finishReason,
                                        const QString& requestId,
                                        const QString& modelId) const
{
    QJsonObject response;
    if (!requestId.isEmpty())
        response["id"] = requestId;
    response["object"] = QStringLiteral("chat.completion");
    if (!modelId.isEmpty())
        response["model"] = modelId;

    QJsonArray choices;
    QJsonObject choice;
    choice["index"] = 0;

    QJsonObject message;
    message["role"] = QStringLiteral("assistant");
    if (!content.isEmpty())
        message["content"] = content;
    if (!toolCalls.isEmpty())
        message["tool_calls"] = toolCalls;

    choice["message"] = message;
    choice["finish_reason"] = finishReason;
    choices.append(choice);
    response["choices"] = choices;
    return response;
}

void LLMAgent::recordRequestJson(const QJsonObject& request,
                                 const QString& requestId,
                                 const QString& modelId)
{
    QJsonObject entry;
    entry["request_id"] = requestId;
    entry["request"] = request;
    m_ioHistory.append(entry);
    m_pendingIoIndex = m_ioHistory.size() - 1;
    m_pendingRequestId = requestId;
    m_pendingModelId = modelId;
}

void LLMAgent::recordResponseJson(const QJsonObject& response)
{
    if (m_pendingIoIndex < 0) {
        QJsonObject entry;
        entry["response"] = response;
        m_ioHistory.append(entry);
        return;
    }
    QJsonObject entry = m_ioHistory.at(m_pendingIoIndex).toObject();
    entry["response"] = response;
    m_ioHistory.replace(m_pendingIoIndex, entry);
    m_pendingIoIndex = -1;
    m_pendingRequestId.clear();
    m_pendingModelId.clear();
}

void LLMAgent::recordErrorJson(const QString& errorMsg)
{
    QJsonObject err;
    err["message"] = errorMsg;
    if (m_pendingIoIndex < 0) {
        QJsonObject entry;
        entry["error"] = err;
        m_ioHistory.append(entry);
        return;
    }
    QJsonObject entry = m_ioHistory.at(m_pendingIoIndex).toObject();
    entry["error"] = err;
    m_ioHistory.replace(m_pendingIoIndex, entry);
    m_pendingIoIndex = -1;
    m_pendingRequestId.clear();
    m_pendingModelId.clear();
}

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
