#include "LLMAgent.h"
#include "AgentEventBus.h"
#include "ToolDispatcher.h"
#include "newCore/ModelFactory.h"
#include "newCore/LLMProvider.h"
#include "newCore/LLMTypes.h"
#include <QDebug>
#include <QTimer>
#include <QUuid>

namespace {
void appendMessageWithRoleMerge(QJsonArray& messages, const QJsonObject& message)
{
    if (message.isEmpty())
        return;

    const QString role = message.value(QStringLiteral("role")).toString();
    if (role.isEmpty()) {
        messages.append(message);
        return;
    }

    if (!messages.isEmpty()) {
        QJsonObject prev = messages.last().toObject();
        const QString prevRole = prev.value(QStringLiteral("role")).toString();
        const bool isMergeRole = (role == QLatin1String("user") || role == QLatin1String("assistant"));
        const bool prevHasToolMeta = prev.contains(QStringLiteral("tool_calls"))
                                     || prev.contains(QStringLiteral("tool_call_id"));
        const bool curHasToolMeta = message.contains(QStringLiteral("tool_calls"))
                                    || message.contains(QStringLiteral("tool_call_id"));
        if (prevRole == role && isMergeRole && !prevHasToolMeta && !curHasToolMeta) {
            const QString prevContent = prev.value(QStringLiteral("content")).toString();
            const QString curContent = message.value(QStringLiteral("content")).toString();
            if (!curContent.isEmpty() && curContent != prevContent) {
                prev.insert(QStringLiteral("content"),
                            prevContent.isEmpty() ? curContent
                                                  : (prevContent + QStringLiteral("\n") + curContent));
            }
            messages[messages.size() - 1] = prev;
            return;
        }
    }

    messages.append(message);
}
} // namespace

LLMAgent::LLMAgent(QObject* parent)
    : QObject(parent)
{
    connect(AgentEventBus::instance(), &AgentEventBus::toolResultReady, this, &LLMAgent::submitToolResult);
}

void LLMAgent::setModelFactory(ModelFactory* factory)
{
    m_modelFactory = factory;
}

QString LLMAgent::modelId() const
{
    return ModelFactory::resolveModelKey(m_config.model, m_config.customModelId);
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
    if (m_config.uuid.isEmpty()) {
        m_config.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    m_systemPrompt = config.systemPrompt;
}

void LLMAgent::reloadModel(const LLMConfig& newConfig)
{
    // 上下文长度自适应：若新模型上下文更小则裁剪历史
    int contextLimit = 0;
    const QString newModelKey = ModelFactory::resolveModelKey(newConfig.model, newConfig.customModelId);
    if (m_modelFactory && !newModelKey.isEmpty()) {
        ModelConfig modelCfg = m_modelFactory->getModelConfig(newModelKey);
        contextLimit = modelCfg.contextLength;
    }
    if (contextLimit > 0) {
        int estimatedHistoryTokens = 0;
        for (const auto& msg : qAsConst(m_conversationHistory)) {
            estimatedHistoryTokens += msg.toObject()["content"].toString().length();
        }
        if (estimatedHistoryTokens > contextLimit) {
            qWarning() << "LLMAgent: History too long for new model (" << estimatedHistoryTokens
                       << ">" << contextLimit << "), truncating...";
            while (m_conversationHistory.size() > 20) {
                m_conversationHistory.removeFirst();
            }
        }
    }

    m_config = newConfig;
    if (m_config.uuid.isEmpty()) {
        m_config.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (!newConfig.systemPrompt.isEmpty()) {
        m_systemPrompt = newConfig.systemPrompt;
    }
    qDebug() << "LLMAgent: Model reloaded. Model:" << ModelFactory::resolveModelKey(m_config.model, m_config.customModelId);
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
    resetToolLoopGuards();

    QJsonObject userMsg;
    if (!prompt.isEmpty()) {
        userMsg[QStringLiteral("role")] = QStringLiteral("user");
        userMsg[QStringLiteral("content")] = prompt;
    }

    m_currentMessages = buildMessageHistory(userMsg, saveToHistory);
    postRequestToServer(m_currentMessages);
}

QJsonArray LLMAgent::buildMessageHistory(const QJsonObject& userMsg, bool saveToHistory)
{
    QJsonArray messages;
    // 添加 System Prompt
    QJsonObject systemMsg;
    systemMsg[QStringLiteral("role")] = QStringLiteral("system");
    systemMsg[QStringLiteral("content")] = m_systemPrompt;
    messages.append(systemMsg);

    if (saveToHistory) {
        for (const QJsonValue& msg : qAsConst(m_conversationHistory)) {
            QJsonObject cur = msg.toObject();
            appendMessageWithRoleMerge(messages, cur);
        }

        // 会话主链路已在 ChatService 写入最新用户消息并注入到 m_conversationHistory。
        // 这里仅做兜底，避免某些调用路径未注入时丢失当前 prompt。
        const QString prompt = userMsg.value(QStringLiteral("content")).toString();
        if (!prompt.isEmpty()) {
            bool alreadyPresent = false;
            if (!messages.isEmpty()) {
                const QJsonObject last = messages.last().toObject();
                if (last.value(QStringLiteral("role")).toString() == QLatin1String("user")) {
                    const QString lastContent = last.value(QStringLiteral("content")).toString();
                    alreadyPresent = (lastContent == prompt
                                      || lastContent.endsWith(QStringLiteral("\n") + prompt));
                }
            }
            if (!alreadyPresent)
                appendMessageWithRoleMerge(messages, userMsg);
        }
    } else {
        appendMessageWithRoleMerge(messages, userMsg);
    }
    return messages;
}

void LLMAgent::postRequestToServer(const QJsonArray& messages)
{
    // 每次新请求都推进代次，旧 provider 的晚到事件将被丢弃。
    const quint64 dispatchToken = ++m_dispatchToken;

    if (!m_config.isValid()) {
        emit errorOccurred("模型未设置");
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

    // 连接信号（按 dispatchToken 过滤，避免旧请求串流污染新请求）
    connect(m_currentProvider, &LLMProvider::deltaReceived, this, [this, dispatchToken](const QString& delta) {
        if (dispatchToken != m_dispatchToken)
            return;
        onDeltaReceived(delta);
    });
    connect(m_currentProvider, &LLMProvider::toolCallsReceived, this, [this, dispatchToken](const QJsonArray& toolCalls) {
        if (dispatchToken != m_dispatchToken)
            return;
        onToolCallsReceived(toolCalls);
    });
    connect(m_currentProvider, &LLMProvider::streamComplete, this, [this, dispatchToken](const QString& c, const LLMUsage&) {
        if (dispatchToken != m_dispatchToken)
            return;
        onClientFinished(c);
    });
    connect(m_currentProvider, &LLMProvider::errorOccurred, this, [this, dispatchToken](const LLMError& err) {
        if (dispatchToken != m_dispatchToken)
            return;
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
    ModelConfig modelCfg = m_modelFactory->getModelConfig(modelId());
    request.temperature = modelCfg.temperature;
    request.maxTokens = modelCfg.maxTokens;
    request.timeoutMs = modelCfg.timeoutMs;
    for (const Tool& t : qAsConst(m_tools)) {
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
    resetToolLoopGuards();
    emit finished(fullContent);
}

void LLMAgent::onClientError(const QString& errorMsg)
{
    m_isToolMode = false;
    m_waitingForToolResponse = false;
    recordErrorJson(errorMsg);
    resetToolLoopGuards();
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

        if (!isToolEnabled(call.name)) {
            ToolExecutionEvent deniedEvent;
            deniedEvent.toolName = call.name;
            deniedEvent.toolId = call.id;
            deniedEvent.status = "completed";
            deniedEvent.success = false;
            deniedEvent.rawResult = QStringLiteral("错误: 工具未授权或不可用: %1").arg(call.name);
            deniedEvent.formattedResult = QStringLiteral("权限不足");
            emit toolEvent(deniedEvent);
            AgentEventBus::instance()->postToolEvent(deniedEvent);
            submitToolResult(call.id, deniedEvent.rawResult);
            continue;
        }

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
    for (const auto& call : qAsConst(m_pendingToolCalls)) {
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
        for (const auto& call : qAsConst(m_pendingToolCalls)) {
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
    for (const auto& call : qAsConst(m_pendingToolCalls)) {
        if (!m_toolResults.contains(call.id)) {
            allDone = false;
            break;
        }
    }

    if (allDone) {
        // 构建并将工具反馈消息加入历史
        for (const auto& call : qAsConst(m_pendingToolCalls)) {
            QJsonObject toolMsg;
            toolMsg["role"] = "tool";
            toolMsg["tool_call_id"] = call.id;
            toolMsg["content"] = m_toolResults[call.id].left(2000); // 截断保护

            if (m_saveToHistory) {
                m_conversationHistory.append(toolMsg);
            }
        }

        if (!m_toolLoopTimer.isValid())
            m_toolLoopTimer.start();
        ++m_toolRoundCount;

        const QString roundSignature = buildToolRoundSignature(m_pendingToolCalls);
        if (!roundSignature.isEmpty() && roundSignature == m_lastToolRoundSignature)
            ++m_consecutiveNoProgressRounds;
        else
            m_consecutiveNoProgressRounds = 0;
        m_lastToolRoundSignature = roundSignature;

        QString primaryToolName;
        if (!m_pendingToolCalls.isEmpty())
            primaryToolName = m_pendingToolCalls.first().name.trimmed();
        if (m_pendingToolCalls.size() == 1
            && !primaryToolName.isEmpty()
            && primaryToolName == m_lastPrimaryToolName) {
            ++m_consecutiveSameToolRounds;
        } else {
            m_consecutiveSameToolRounds = 1;
        }
        m_lastPrimaryToolName = primaryToolName;

        QString guardReason;
        if (m_toolRoundCount >= kMaxToolRoundsPerTurn) {
            guardReason = QStringLiteral("[熔断] 工具调用已达 %1 轮上限，自动停止本轮。")
                              .arg(kMaxToolRoundsPerTurn);
        } else if (m_toolLoopTimer.isValid() && m_toolLoopTimer.elapsed() >= kMaxToolLoopTimeMs) {
            guardReason = QStringLiteral("[熔断] 工具链执行超时（%1 ms），自动停止本轮。")
                              .arg(kMaxToolLoopTimeMs);
        } else if (m_consecutiveNoProgressRounds >= kMaxConsecutiveNoProgressRounds) {
            guardReason = QStringLiteral("[熔断] 连续 %1 轮工具调用无明显进展，自动停止本轮。")
                              .arg(kMaxConsecutiveNoProgressRounds);
        } else if (m_consecutiveSameToolRounds >= kMaxConsecutiveSameToolRounds) {
            guardReason = QStringLiteral("[熔断] 工具 %1 连续调用 %2 轮，自动停止本轮。")
                              .arg(primaryToolName.isEmpty() ? QStringLiteral("unknown")
                                                              : primaryToolName)
                              .arg(kMaxConsecutiveSameToolRounds);
        }

        if (!guardReason.isEmpty()) {
            ToolExecutionEvent guardEvent;
            guardEvent.toolName = QStringLiteral("tool_loop_guard");
            guardEvent.status = QStringLiteral("completed");
            guardEvent.success = false;
            guardEvent.rawResult = guardReason;
            guardEvent.formattedResult = guardReason;
            emit toolEvent(guardEvent);
            AgentEventBus::instance()->postToolEvent(guardEvent);

            if (m_saveToHistory) {
                QJsonObject assistantMsg;
                assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
                assistantMsg[QStringLiteral("content")] = guardReason;
                m_conversationHistory.append(assistantMsg);
            }

            const QJsonObject guardResponse = buildResponseJson(
                guardReason,
                QJsonArray(),
                QStringLiteral("tool_loop_guard"),
                m_pendingRequestId,
                m_pendingModelId);
            recordResponseJson(guardResponse);

            m_isToolMode = false;
            m_waitingForToolResponse = false;
            resetToolState();
            emit finished(guardReason);
            return;
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
    // 中断时推进代次，确保旧请求后续事件被静默丢弃。
    ++m_dispatchToken;
    if (m_currentProvider) {
        m_currentProvider->abort();
    }
    m_isToolMode = false;
    m_waitingForToolResponse = false;
    resetToolState();
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
    resetToolLoopGuards();
}

void LLMAgent::setHistory(const QJsonArray& h)
{
    m_conversationHistory = h;
    m_currentMessages = QJsonArray();
    m_isToolMode = false;
    resetToolLoopGuards();
}

// ... 其他辅助函数 (getHistory, registerTool 等) 保持微调
QJsonArray LLMAgent::getHistory() const { return m_conversationHistory; }
int LLMAgent::getConversationCount() const { return m_conversationHistory.size() / 2; }
void LLMAgent::registerTool(const Tool& tool)
{
    if (tool.name.trimmed().isEmpty())
        return;
    m_tools.append(tool);
    m_enabledToolNames.insert(tool.name);
}

void LLMAgent::clearTools()
{
    m_tools.clear();
    m_enabledToolNames.clear();
}

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

void LLMAgent::resetToolLoopGuards()
{
    m_toolRoundCount = 0;
    m_consecutiveSameToolRounds = 0;
    m_consecutiveNoProgressRounds = 0;
    m_lastToolRoundSignature.clear();
    m_lastPrimaryToolName.clear();
    m_toolLoopTimer.invalidate();
}

void LLMAgent::resetToolState()
{
    m_pendingToolCalls.clear();
    m_deferredToolIds.clear();
    m_toolResults.clear();
    resetToolLoopGuards();
}

QString LLMAgent::buildToolRoundSignature(const QList<ToolCall>& calls) const
{
    QStringList parts;
    parts.reserve(calls.size());
    for (const ToolCall& call : calls) {
        const QString name = call.name.trimmed();
        const QString args = QString::fromUtf8(
            QJsonDocument(call.input).toJson(QJsonDocument::Compact));
        parts.append(name + QStringLiteral(":") + args);
    }
    return parts.join(QStringLiteral("|"));
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

void LLMAgent::setToolDispatcher(ToolDispatcher* d, const QStringList& allowedTools)
{
    m_toolDispatcher = d;
    clearTools();
    if (!d)
        return;

    QSet<QString> allowSet;
    for (const QString& name : allowedTools) {
        const QString trimmed = name.trimmed();
        if (!trimmed.isEmpty())
            allowSet.insert(trimmed);
    }

    // 动态注册委派工具（是否真正可见由下方 allowSet 控制）。
    d->registerAgentTools(m_config);

    const QList<Tool> allTools = d->getAllToolSchemas();
    const bool useAllowList = !allowSet.isEmpty();
    for (const Tool& tool : allTools) {
        if (!useAllowList || allowSet.contains(tool.name))
            registerTool(tool);
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

bool LLMAgent::isToolEnabled(const QString& toolName) const
{
    return m_enabledToolNames.contains(toolName);
}
