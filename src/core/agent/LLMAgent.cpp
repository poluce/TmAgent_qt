#include "LLMAgent.h"
#include "AgentEventBus.h"
#include "ToolDispatcher.h"
#include "newCore/ModelFactory.h"
#include "newCore/LLMProvider.h"
#include "newCore/LLMTypes.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <algorithm>

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

int estimateMessageChars(const QJsonObject& message)
{
    int chars = message.value(QStringLiteral("role")).toString().size()
                + message.value(QStringLiteral("content")).toString().size();
    if (message.contains(QStringLiteral("tool_call_id")))
        chars += message.value(QStringLiteral("tool_call_id")).toString().size();
    if (message.contains(QStringLiteral("tool_calls"))) {
        chars += QString::fromUtf8(
                     QJsonDocument(message.value(QStringLiteral("tool_calls")).toArray())
                         .toJson(QJsonDocument::Compact))
                     .size();
    }
    return chars;
}

int estimateMessagesChars(const QJsonArray& messages)
{
    int total = 0;
    for (const QJsonValue& msg : messages)
        total += estimateMessageChars(msg.toObject());
    return total;
}

QString toolLoopPolicyPath()
{
    return QDir::home().filePath(QStringLiteral(".tmagent/config/tool_loop_policy.json"));
}

QJsonObject defaultToolLoopPolicyObject()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), 2);
    obj.insert(QStringLiteral("max_tool_rounds_per_turn"), 12);
    obj.insert(QStringLiteral("max_consecutive_same_tool_rounds"), 4);
    obj.insert(QStringLiteral("max_consecutive_no_progress_rounds"), 4);
    obj.insert(QStringLiteral("max_consecutive_failed_tool_rounds"), 3);
    obj.insert(QStringLiteral("max_tool_loop_time_ms"), 60000);
    return obj;
}

bool writeToolLoopPolicyObject(const QJsonObject& obj)
{
    const QString path = toolLoopPolicyPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;

    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;

    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    const bool ok = (file.write(bytes) == bytes.size());
    file.close();
    return ok;
}

QJsonObject loadToolLoopPolicyObject()
{
    const QString path = toolLoopPolicyPath();
    QFile file(path);
    if (!file.exists()) {
        const QJsonObject defaults = defaultToolLoopPolicyObject();
        writeToolLoopPolicyObject(defaults);
        return defaults;
    }
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        qWarning() << "LLMAgent: failed to read tool loop policy, fallback to defaults:" << path;
        return defaultToolLoopPolicyObject();
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "LLMAgent: invalid tool loop policy, reset defaults:" << path
                   << "error=" << err.errorString();
        const QJsonObject defaults = defaultToolLoopPolicyObject();
        writeToolLoopPolicyObject(defaults);
        return defaults;
    }

    const QJsonObject defaults = defaultToolLoopPolicyObject();
    QJsonObject merged = defaults;
    const QJsonObject raw = doc.object();
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it)
        merged.insert(it.key(), it.value());

    bool changed = false;
    const int schemaVersion = raw.value(QStringLiteral("schema_version")).toInt(1);
    if (schemaVersion < 2) {
        // 迁移旧配置：对过低阈值做“向上兼容”，避免开发阶段频繁误熔断。
        const auto liftAtLeast = [&merged, &defaults, &changed](const QString& key) {
            const int current = merged.value(key).toInt(defaults.value(key).toInt());
            const int expectedMin = defaults.value(key).toInt();
            if (current < expectedMin) {
                merged.insert(key, expectedMin);
                changed = true;
            }
        };

        liftAtLeast(QStringLiteral("max_tool_rounds_per_turn"));
        liftAtLeast(QStringLiteral("max_consecutive_same_tool_rounds"));
        liftAtLeast(QStringLiteral("max_consecutive_no_progress_rounds"));
        liftAtLeast(QStringLiteral("max_consecutive_failed_tool_rounds"));

        const qint64 currentLoopMs = merged.value(QStringLiteral("max_tool_loop_time_ms"))
                                       .toVariant()
                                       .toLongLong();
        const qint64 expectedLoopMs = defaults.value(QStringLiteral("max_tool_loop_time_ms"))
                                          .toVariant()
                                          .toLongLong();
        if (currentLoopMs < expectedLoopMs) {
            merged.insert(QStringLiteral("max_tool_loop_time_ms"), expectedLoopMs);
            changed = true;
        }
        merged.insert(QStringLiteral("schema_version"), 2);
        changed = true;
    }

    if (changed)
        writeToolLoopPolicyObject(merged);

    return merged;
}

QJsonValue canonicalizeJsonValue(const QJsonValue& value)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        QStringList keys = obj.keys();
        std::sort(keys.begin(), keys.end());
        QJsonObject sorted;
        for (const QString& key : keys)
            sorted.insert(key, canonicalizeJsonValue(obj.value(key)));
        return sorted;
    }
    if (value.isArray()) {
        QJsonArray normalized;
        const QJsonArray arr = value.toArray();
        for (const QJsonValue& item : arr)
            normalized.append(canonicalizeJsonValue(item));
        return normalized;
    }
    return value;
}

QString buildToolCallSignature(const ToolCall& call)
{
    const QJsonValue canonicalInput = canonicalizeJsonValue(call.input);
    const QByteArray json = QJsonDocument(canonicalInput.toObject()).toJson(QJsonDocument::Compact);
    const QByteArray hash = QCryptographicHash::hash(json, QCryptographicHash::Sha1).toHex();
    return call.name.trimmed() + QStringLiteral(":") + QString::fromLatin1(hash);
}

QStringList extractToolCallIds(const QJsonObject& assistantMsg)
{
    QStringList ids;
    const QJsonArray toolCalls = assistantMsg.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue& value : toolCalls) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString().trimmed();
        if (!id.isEmpty() && !ids.contains(id))
            ids.append(id);
    }
    return ids;
}

void appendMissingToolResults(QJsonArray& messages, const QStringList& missingToolCallIds)
{
    for (const QString& id : missingToolCallIds) {
        if (id.trimmed().isEmpty())
            continue;
        QJsonObject placeholder;
        placeholder.insert(QStringLiteral("role"), QStringLiteral("tool"));
        placeholder.insert(QStringLiteral("tool_call_id"), id);
        placeholder.insert(QStringLiteral("content"), QStringLiteral("[工具结果不可用]"));
        messages.append(placeholder);
    }
}

bool isRawToolResultSuccess(const QString& rawResult)
{
    const QString text = rawResult.trimmed();
    if (text.startsWith(QStringLiteral("错误")))
        return false;

    static const QRegularExpression kExitCodeRe(
        QStringLiteral("(?:^|\\n)\\s*退出码\\s*:\\s*(-?\\d+)"));
    const QRegularExpressionMatch match = kExitCodeRe.match(text);
    if (match.hasMatch())
        return match.captured(1).toInt() == 0;

    return true;
}

QJsonArray normalizeToolMessageSequence(const QJsonArray& messages)
{
    QJsonArray normalized;
    QStringList pendingToolCallIds;

    const auto flushPending = [&normalized, &pendingToolCallIds]() {
        if (pendingToolCallIds.isEmpty())
            return;
        appendMissingToolResults(normalized, pendingToolCallIds);
        pendingToolCallIds.clear();
    };

    for (const QJsonValue& value : messages) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        if (role == QLatin1String("assistant") && message.contains(QStringLiteral("tool_calls"))) {
            flushPending();
            normalized.append(message);
            pendingToolCallIds = extractToolCallIds(message);
            continue;
        }

        if (role == QLatin1String("tool")) {
            if (pendingToolCallIds.isEmpty())
                continue;

            const QString toolCallId = message.value(QStringLiteral("tool_call_id")).toString().trimmed();
            const int idx = pendingToolCallIds.indexOf(toolCallId);
            if (idx < 0)
                continue;

            normalized.append(message);
            pendingToolCallIds.removeAt(idx);
            continue;
        }

        flushPending();
        normalized.append(message);
    }

    flushPending();
    return normalized;
}

QJsonArray trimMessagesForRequest(const QJsonArray& messages, int maxMessages, int maxChars)
{
    const int safeMaxMessages = qMax(2, maxMessages);
    const int safeMaxChars = qMax(2048, maxChars);
    if (messages.size() <= safeMaxMessages && estimateMessagesChars(messages) <= safeMaxChars)
        return messages;

    QJsonArray trimmed = messages;
    while (trimmed.size() > safeMaxMessages || estimateMessagesChars(trimmed) > safeMaxChars) {
        if (trimmed.size() <= 2)
            break;
        trimmed.removeAt(1);
        while (trimmed.size() > 1
               && trimmed.at(1).toObject().value(QStringLiteral("role")).toString()
                   == QLatin1String("tool")) {
            trimmed.removeAt(1);
        }
    }
    return trimmed;
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

void LLMAgent::refreshToolLoopPolicy()
{
    const QJsonObject obj = loadToolLoopPolicyObject();

    const int maxToolRounds = obj.value(QStringLiteral("max_tool_rounds_per_turn")).toInt(
        m_toolLoopPolicy.maxToolRoundsPerTurn);
    m_toolLoopPolicy.maxToolRoundsPerTurn = qBound(
        kPolicyMinToolRounds, maxToolRounds, kPolicyMaxToolRounds);

    const int maxSameToolRounds =
        obj.value(QStringLiteral("max_consecutive_same_tool_rounds")).toInt(
            m_toolLoopPolicy.maxConsecutiveSameToolRounds);
    m_toolLoopPolicy.maxConsecutiveSameToolRounds = qBound(
        kPolicyMinRepeatRounds, maxSameToolRounds, kPolicyMaxRepeatRounds);

    const int maxNoProgressRounds =
        obj.value(QStringLiteral("max_consecutive_no_progress_rounds")).toInt(
            m_toolLoopPolicy.maxConsecutiveNoProgressRounds);
    m_toolLoopPolicy.maxConsecutiveNoProgressRounds = qBound(
        kPolicyMinNoProgressRounds, maxNoProgressRounds, kPolicyMaxNoProgressRounds);

    const int maxFailedRounds =
        obj.value(QStringLiteral("max_consecutive_failed_tool_rounds")).toInt(
            m_toolLoopPolicy.maxConsecutiveFailedToolRounds);
    m_toolLoopPolicy.maxConsecutiveFailedToolRounds = qBound(
        kPolicyMinFailedRounds, maxFailedRounds, kPolicyMaxFailedRounds);

    const qint64 maxLoopTimeMs = obj.value(QStringLiteral("max_tool_loop_time_ms"))
                                     .toVariant()
                                     .toLongLong();
    const qint64 fallbackLoopTimeMs = m_toolLoopPolicy.maxToolLoopTimeMs;
    const qint64 normalizedLoopTimeMs = maxLoopTimeMs > 0 ? maxLoopTimeMs : fallbackLoopTimeMs;
    m_toolLoopPolicy.maxToolLoopTimeMs = qBound(
        kPolicyMinToolLoopTimeMs, normalizedLoopTimeMs, kPolicyMaxToolLoopTimeMs);
}

void LLMAgent::sendRequest(const QString& prompt, bool saveToHistory)
{
    if (m_currentProvider) {
        m_currentProvider->abort();
    }
    m_fullContent.clear();
    m_saveToHistory = saveToHistory;
    refreshToolLoopPolicy();
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
    const QJsonArray normalizedMessages = normalizeToolMessageSequence(messages);
    if (normalizedMessages.size() != messages.size()) {
        qWarning() << "LLMAgent: normalized tool message sequence from" << messages.size()
                   << "to" << normalizedMessages.size() << "messages";
    }
    request.messages = trimMessagesForRequest(normalizedMessages, kMaxRequestMessages, kMaxRequestChars);
    if (request.messages.size() != normalizedMessages.size()) {
        qWarning() << "LLMAgent: request context trimmed from" << normalizedMessages.size()
                   << "to" << request.messages.size() << "messages";
    }
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
    if (!capturedContent.trimmed().isEmpty())
        m_lastAssistantPlan = capturedContent.trimmed();
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
    m_toolResultSuccess.clear();

    if (!m_toolDispatcher)
        return;

    for (const QJsonValue& item : toolCalls) {
        ToolCall call = ToolCall::fromDeepSeekJson(item.toObject());
        const QString workspace = m_config.workspaceDir.trimmed();
        if (!workspace.isEmpty()) {
            call.input.insert(QStringLiteral("_agent_workspace"), workspace);
            if (call.name == QLatin1String("execute_command")
                && call.input.value(QStringLiteral("working_directory")).toString().trimmed().isEmpty()) {
                call.input.insert(QStringLiteral("working_directory"), workspace);
            }
        }
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
    const bool toolSuccess = isRawToolResultSuccess(result);
    m_toolResultSuccess[toolId] = toolSuccess;

    QString toolName;
    for (const auto& call : qAsConst(m_pendingToolCalls)) {
        if (call.id == toolId) {
            toolName = call.name;
            break;
        }
    }

    ++m_totalToolCallsThisTurn;
    if (!toolSuccess)
        ++m_totalToolFailuresThisTurn;

    QString summaryLine = QStringLiteral("%1 %2: %3")
                              .arg(toolSuccess ? QStringLiteral("[OK]") : QStringLiteral("[FAIL]"),
                                   toolName.isEmpty() ? QStringLiteral("tool") : toolName,
                                   summarizeToolResultForGuard(result));
    if (summaryLine.size() > 240)
        summaryLine = summaryLine.left(240) + QStringLiteral("...");
    m_recentToolSummaries.append(summaryLine);
    while (m_recentToolSummaries.size() > 8)
        m_recentToolSummaries.removeFirst();

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
        endEvent.success = isRawToolResultSuccess(result);
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
        QString primaryToolSignature;
        if (!m_pendingToolCalls.isEmpty()) {
            primaryToolName = m_pendingToolCalls.first().name.trimmed();
            primaryToolSignature = buildToolCallSignature(m_pendingToolCalls.first());
        }
        if (m_pendingToolCalls.size() == 1
            && !primaryToolSignature.isEmpty()
            && primaryToolSignature == m_lastPrimaryToolSignature) {
            ++m_consecutiveSameToolRounds;
        } else {
            m_consecutiveSameToolRounds = 1;
        }
        m_lastPrimaryToolSignature = primaryToolSignature;

        bool hasSuccessResult = false;
        for (const auto& call : qAsConst(m_pendingToolCalls)) {
            if (m_toolResultSuccess.value(call.id, true)) {
                hasSuccessResult = true;
                break;
            }
        }
        if (hasSuccessResult)
            m_consecutiveFailedToolRounds = 0;
        else
            ++m_consecutiveFailedToolRounds;

        QString guardReason;
        if (m_toolRoundCount >= m_toolLoopPolicy.maxToolRoundsPerTurn) {
            guardReason = QStringLiteral("[熔断] 工具调用已达 %1 轮上限，自动停止本轮。")
                              .arg(m_toolLoopPolicy.maxToolRoundsPerTurn);
        } else if (m_toolLoopTimer.isValid()
                   && m_toolLoopTimer.elapsed() >= m_toolLoopPolicy.maxToolLoopTimeMs) {
            guardReason = QStringLiteral("[熔断] 工具链执行超时（%1 ms），自动停止本轮。")
                              .arg(m_toolLoopPolicy.maxToolLoopTimeMs);
        } else if (m_consecutiveNoProgressRounds
                   >= m_toolLoopPolicy.maxConsecutiveNoProgressRounds) {
            guardReason = QStringLiteral("[熔断] 连续 %1 轮工具调用无明显进展，自动停止本轮。")
                              .arg(m_toolLoopPolicy.maxConsecutiveNoProgressRounds);
        } else if (m_consecutiveSameToolRounds
                   >= m_toolLoopPolicy.maxConsecutiveSameToolRounds) {
            guardReason = QStringLiteral("[熔断] 工具 %1（同参数）连续调用 %2 轮，自动停止本轮。")
                              .arg(primaryToolName.isEmpty() ? QStringLiteral("unknown")
                                                              : primaryToolName)
                              .arg(m_toolLoopPolicy.maxConsecutiveSameToolRounds);
        } else if (m_consecutiveFailedToolRounds
                   >= m_toolLoopPolicy.maxConsecutiveFailedToolRounds) {
            guardReason = QStringLiteral("[熔断] 工具连续失败 %1 轮，自动停止本轮。")
                              .arg(m_toolLoopPolicy.maxConsecutiveFailedToolRounds);
        }

        if (!guardReason.isEmpty()) {
            const QString guardFinalReply = buildToolGuardFinalReply(guardReason);
            ToolExecutionEvent guardEvent;
            guardEvent.toolName = QStringLiteral("tool_loop_guard");
            guardEvent.status = QStringLiteral("completed");
            guardEvent.success = false;
            guardEvent.rawResult = guardFinalReply;
            guardEvent.formattedResult = guardFinalReply;
            emit toolEvent(guardEvent);
            AgentEventBus::instance()->postToolEvent(guardEvent);

            if (m_saveToHistory) {
                QJsonObject assistantMsg;
                assistantMsg[QStringLiteral("role")] = QStringLiteral("assistant");
                assistantMsg[QStringLiteral("content")] = guardFinalReply;
                m_conversationHistory.append(assistantMsg);
            }

            const QJsonObject guardResponse = buildResponseJson(
                guardFinalReply,
                QJsonArray(),
                QStringLiteral("tool_loop_guard"),
                m_pendingRequestId,
                m_pendingModelId);
            recordResponseJson(guardResponse);

            m_isToolMode = false;
            m_waitingForToolResponse = false;
            resetToolState();
            emit finished(guardFinalReply);
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

    // 为尚未收到结果的 pending tool calls 发射占位 completed 事件，
    // 确保 ChatService 能持久化对应的 ToolResult Message，避免孤立的 ToolCall。
    if (m_isToolMode) {
        for (const auto& call : qAsConst(m_pendingToolCalls)) {
            if (!m_toolResults.contains(call.id)) {
                ToolExecutionEvent abortEvent;
                abortEvent.toolName = call.name;
                abortEvent.toolId = call.id;
                abortEvent.status = QStringLiteral("completed");
                abortEvent.success = false;
                abortEvent.rawResult = QStringLiteral("[工具执行被中断]");
                abortEvent.formattedResult = QStringLiteral("已中断");
                emit toolEvent(abortEvent);
            }
        }
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
    m_consecutiveFailedToolRounds = 0;
    m_lastToolRoundSignature.clear();
    m_lastPrimaryToolSignature.clear();
    m_toolLoopTimer.invalidate();
    m_recentToolSummaries.clear();
    m_totalToolCallsThisTurn = 0;
    m_totalToolFailuresThisTurn = 0;
    m_lastAssistantPlan.clear();
}

void LLMAgent::resetToolState()
{
    m_pendingToolCalls.clear();
    m_deferredToolIds.clear();
    m_toolResults.clear();
    m_toolResultSuccess.clear();
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

QString LLMAgent::summarizeToolResultForGuard(const QString& rawResult) const
{
    QString text = rawResult.trimmed();
    if (text.isEmpty())
        return QStringLiteral("无输出");

    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (trimmed.startsWith(QStringLiteral("退出码")))
            continue;
        if (trimmed.startsWith(QStringLiteral("标准输出")))
            continue;
        if (trimmed.startsWith(QStringLiteral("错误输出")))
            continue;
        return trimmed;
    }

    if (!lines.isEmpty())
        return lines.first().trimmed();
    return QStringLiteral("无输出");
}

QString LLMAgent::buildToolGuardFinalReply(const QString& guardReason) const
{
    QStringList lines;
    lines << QStringLiteral("本轮已触发保护性停止。")
          << guardReason
          << QString();

    if (!m_lastAssistantPlan.trimmed().isEmpty()) {
        lines << QStringLiteral("执行目标（模型给出的计划）：")
              << m_lastAssistantPlan.trimmed()
              << QString();
    }

    lines << QStringLiteral("工具执行统计：")
          << QStringLiteral("- 工具调用总次数: %1").arg(m_totalToolCallsThisTurn)
          << QStringLiteral("- 失败次数: %1").arg(m_totalToolFailuresThisTurn)
          << QStringLiteral("- 成功次数: %1").arg(qMax(0, m_totalToolCallsThisTurn - m_totalToolFailuresThisTurn))
          << QString();

    if (!m_recentToolSummaries.isEmpty()) {
        lines << QStringLiteral("最近工具结果（按时间顺序）：");
        for (const QString& item : m_recentToolSummaries)
            lines << QStringLiteral("- %1").arg(item);
        lines << QString();
    }

    lines << QStringLiteral("建议下一步：")
          << QStringLiteral("1. 如需继续，请给出更明确验收标准（例如“只输出项目结构摘要，不再继续读文件”）。")
          << QStringLiteral("2. 如需放宽限制，可在设置里调整工具循环预算（轮次/重复/失败阈值）。")
          << QStringLiteral("3. 如触发权限或路径问题，优先修正工作空间路径后再继续。");

    return lines.join(QStringLiteral("\n"));
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
