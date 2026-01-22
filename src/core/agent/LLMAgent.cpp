#include "LLMAgent.h"
#include "ToolDispatcher.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QDebug>
#include <QTimer>
#include <QRegularExpression>
#include <QFileInfo>
#include "AgentEventBus.h"

LLMAgent::LLMAgent(QObject *parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(180000);  // 3分钟超时
    
    // 默认角色定义
    m_systemPrompt = "你是一个专业的 AI 助手，能够帮助用户完成各种任务。"
                     "你可以使用工具来执行文件操作和命令行操作。"
                     "请简洁、准确地回答用户的问题。";
    
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "WARNING: 网络请求超时!";
        if (m_currentReply) {
            m_currentReply->abort();
        }
        m_isToolMode = false;
        emit errorOccurred("请求超时,请检查网络连接或稍后重试");
    });
}

void LLMAgent::setSystemPrompt(const QString& prompt) {
    if (!prompt.isEmpty()) {
        m_systemPrompt += "\n" + prompt;  // 追加用户设置的提示词
    }
}

void LLMAgent::setConfig(const LLMConfig& config) {
    m_config = config;
    
    // 同步更新相关成员变量
    m_systemPrompt = config.systemPrompt;
    m_timeoutTimer->setInterval(config.timeoutMs);
}

void LLMAgent::sendMessage(const QString& prompt) {
    sendRequest(prompt, true);  // 保存历史
}

void LLMAgent::askOnce(const QString& prompt) {
    sendRequest(prompt, false);  // 不保存历史
}

void LLMAgent::sendRequest(const QString& prompt, bool saveToHistory) {
    if (m_currentReply) {
        abort();
    }

    m_fullContent.clear();
    m_saveToHistory = saveToHistory;
    m_isToolMode = !m_tools.isEmpty();
    
    // 构造用户消息
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt;
    
    if (saveToHistory) {
        m_conversationHistory.append(userMsg);
    }
    
    // 准备消息列表并发送
    QJsonArray messages = buildMessageHistory(userMsg, saveToHistory);
    postRequestToServer(messages);
}

QJsonArray LLMAgent::buildMessageHistory(const QJsonObject& userMsg, bool saveToHistory) {
    QJsonArray messages;
    
    if (m_isToolMode) {
        // 工具模式：使用独立的消息历史
        m_pendingToolCalls.clear();
        m_toolResults.clear();
        
        if (!saveToHistory) {
            m_currentMessages = QJsonArray();  // 单次调用，清空历史
        }
        m_currentMessages.append(userMsg);
        messages = m_currentMessages;
    } else if (saveToHistory) {
        // 多轮对话：使用完整对话历史
        for (const QJsonValue& msg : m_conversationHistory) {
            messages.append(msg);
        }
    } else {
        // 单次问答：只发送当前消息
        messages.append(userMsg);
    }
    
    return messages;
}


void LLMAgent::abort() {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}



void LLMAgent::clearHistory() {
    m_conversationHistory = QJsonArray();
    m_currentMessages = QJsonArray();  // NOTE: 同时清空工具模式的对话历史
}

QJsonArray LLMAgent::getHistory() const {
    return m_conversationHistory;
}

int LLMAgent::getConversationCount() const {
    int count = 0;
    for (const QJsonValue& msg : m_conversationHistory) {
        if (msg.toObject()["role"].toString() == "user") {
            count++;
        }
    }
    return count;
}

// ==================== 工具管理函数 ====================

void LLMAgent::registerTool(const Tool& tool) {
    m_tools.append(tool);
}

void LLMAgent::clearTools() {
    m_tools.clear();
}

QList<Tool> LLMAgent::getTools() const {
    return m_tools;
}

void LLMAgent::setToolDispatcher(ToolDispatcher* dispatcher) {
    m_toolDispatcher = dispatcher;
    
    // 自动从 dispatcher 获取并注册所有工具 Schema
    if (dispatcher) {
        clearTools();  // 清空旧的工具
        QList<Tool> tools = dispatcher->getAllToolSchemas();
        for (const Tool& tool : tools) {
            registerTool(tool);
        }
    }
}

void LLMAgent::postRequestToServer(const QJsonArray& messages) {
    // 从 m_config 获取配置
    if (!m_config.isValid()) {
        emit errorOccurred("API Key is empty! Please configure it first.");
        return;
    }
    
    // 构造请求（已注册工具会自动附带）
    QJsonObject root = buildApiRequestBody(messages);
    
    // 发送请求到 LLM API
    QUrl url(m_config.baseUrl + m_config.endpoint);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // 根据 authType 设置认证头
    if (m_config.authType == "Bearer") {
        request.setRawHeader("Authorization", QString("Bearer %1").arg(m_config.apiKey).toUtf8());
    } else if (m_config.authType == "X-API-Key") {
        request.setRawHeader("X-API-Key", m_config.apiKey.toUtf8());
    } else if (m_config.authType == "api-key") {
        request.setRawHeader("api-key", m_config.apiKey.toUtf8());
    } else {
        // 默认使用 Bearer
        request.setRawHeader("Authorization", QString("Bearer %1").arg(m_config.apiKey).toUtf8());
    }
    

    // 清理旧的请求（如果存在）
    if (m_currentReply) {
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    
    // 创建新请求
    m_currentReply = m_manager->post(request, QJsonDocument(root).toJson());
    
    // NOTE: 流式数据处理 - 委托给 parseStreamEventLine
    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_currentReply) return;
        while (m_currentReply->canReadLine()) {
            QByteArray line = m_currentReply->readLine().trimmed();
            if (!line.isEmpty()) {
                parseStreamEventLine(line);
            }
        }
    });
    
    // 启动超时定时器
    m_timeoutTimer->start();
    
    // NOTE: 请求完成处理 - 委托给 onStreamFinished
    connect(m_currentReply, &QNetworkReply::finished, this, 
            &LLMAgent::onStreamFinished);
}



void LLMAgent::executeToolCalls(const QJsonArray& toolCalls) {
    m_pendingToolCalls.clear();
    
    // 检查是否设置了工具调度器
    if (!m_toolDispatcher) {
        qDebug() << "错误: 未设置 ToolDispatcher，无法执行工具调用";
        emit errorOccurred("内部错误: 未配置工具调度器");
        return;
    }
    
    // 解析所有工具调用请求 (DeepSeek 格式)
    for (const QJsonValue& item : toolCalls) {
        QJsonObject obj = item.toObject();
        
        // DeepSeek 格式: {id, type: "function", function: {name, arguments}}
        QString type = obj["type"].toString();
        if (type == "function") {
            ToolCall call = ToolCall::fromDeepSeekJson(obj);
            
            m_pendingToolCalls.append(call);
            
            // NOTE: 发射工具事件信号
            ToolExecutionEvent event(call);
            emit toolEvent(event);
            
            // NOTE: 向全局事件总线投递
            AgentEventBus::instance()->postToolEvent(event);
            
            // NOTE: Agent 自治执行 - 直接调用 ToolDispatcher
            QString result = m_toolDispatcher->dispatch(call);
            
            // NOTE: 自动提交结果，完成闭环
            submitToolResult(call.id, result);
        }
    }
}


void LLMAgent::submitToolResult(const QString& toolId, const QString& result) {
    // 找到对应的工具名
    QString toolName;
    for (const ToolCall& call : m_pendingToolCalls) {
        if (call.id == toolId) {
            toolName = call.name;
            break;
        }
    }
    
    // NOTE: 保存原始结果给 LLM，而不是摘要
    m_toolResults[toolId] = result;
    
    // 格式化结果(仅用于 UI 显示)
    QString formattedResult = formatToolResultSummary(toolName, result);
    
    // 发射工具事件信号
    bool success = !result.contains("失败") && !result.contains("错误");
    ToolExecutionEvent event;
    event.toolName = toolName;
    event.toolId = toolId;
    event.status = "completed";
    event.rawResult = result;
    event.formattedResult = formattedResult;
    event.success = success;
    emit toolEvent(event);
    
    // NOTE: 向全局事件总线投递
    AgentEventBus::instance()->postToolEvent(event);
    
    // 检查是否所有工具都已返回结果
    bool allCompleted = true;
    for (const ToolCall& call : m_pendingToolCalls) {
        if (!m_toolResults.contains(call.id)) {
            allCompleted = false;
            break;
        }
    }
    
    if (allCompleted) {
        resumeAfterToolExecution();
    }
}

void LLMAgent::resumeAfterToolExecution() {
    // DeepSeek 格式: 每个工具结果作为单独的消息
    for (const ToolCall& call : m_pendingToolCalls) {
        QString result = m_toolResults[call.id];
        
        // NOTE: 保留完整结果，仅限制最大长度为 2000 字符
        // 这样 LLM 能看到足够的信息来做出正确决策
        if (result.length() > 2000) {
            result = result.left(2000) + "\n...(输出过长，已截断)";
        }
        
        QJsonObject toolMsg;
        toolMsg["role"] = "tool";  // DeepSeek 使用 "tool" 角色
        toolMsg["tool_call_id"] = call.id;
        toolMsg["content"] = result;
        
        m_currentMessages.append(toolMsg);
    }
    
    
    // 使用 QTimer::singleShot 延迟发送，确保当前请求的 finished 处理完全结束
    QTimer::singleShot(0, this, [this]() {
        postRequestToServer(m_currentMessages);
    });
}

// ==================== 阶段一:结果格式化和智能摘要 ====================

QString LLMAgent::formatToolResultSummary(const QString& toolName, const QString& rawResult) {
    if (toolName == "execute_command") {
        return summarizeCommandOutput(rawResult);
    } else if (toolName == "create_file") {
        return summarizeFileOperation(rawResult);
    }
    
    // 未知工具,返回原始结果
    return rawResult;
}

QString LLMAgent::summarizeCommandOutput(const QString& cmdOutput) {
    // 解析命令输出,提取关键信息
    
    // 检查是否包含 "退出码"
    if (cmdOutput.contains("退出码:")) {
        QStringList lines = cmdOutput.split('\n', Qt::SkipEmptyParts);
        
        int exitCode = -1;
        QString stdOutput;
        
        for (const QString& line : lines) {
            if (line.contains("退出码:")) {
                // 提取退出码
                QRegularExpression re("退出码:\\s*(\\d+)");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) {
                    exitCode = match.captured(1).toInt();
                }
            } else if (line.contains("标准输出:")) {
                continue;
            } else if (!line.trimmed().isEmpty()) {
                stdOutput += line + "\n";
            }
        }
        
        stdOutput = stdOutput.trimmed();
        
        // 根据输出类型生成摘要
        if (exitCode == 0 && !stdOutput.isEmpty()) {
            // 截断过长的输出供预览
            QString preview = stdOutput.left(150).trimmed();
            if (stdOutput.length() > 150) preview += "...";
            return QString("[OK] 执行成功: %1").arg(preview);
        } else if (exitCode == 0) {
            return "[OK] 命令执行成功";
        } else {
            return QString("[FAIL] 命令执行失败 (退出码: %1)").arg(exitCode);
        }
    }
    
    // 无法解析,返回原始结果
    return cmdOutput;
}

QString LLMAgent::summarizeFileOperation(const QString& fileResult) {
    // 解析文件操作结果
    
    if (fileResult.contains("成功")) {
        return "[OK] 操作成功";
    } else if (fileResult.contains("失败") || fileResult.contains("错误")) {
        return "[FAIL] 操作失败";
    }
    
    return fileResult;
}

// ==================== SSE 流处理辅助函数 ====================
/* 
data: {"id":"f8ae835f-7db0-45cd-8582-302476f993b3","object":"chat.completion.chunk","created":1766478438,"model":"deepseek-chat","system_fingerprint":"fp_eaab8d114b_prod0820_fp8_kvcache"
,"choices":[{"index":0,"delta":{"content":"我来"},"logprobs":null,"finish_reason":null}]}
*/

void LLMAgent::parseStreamEventLine(const QByteArray& line) {
    if (!line.startsWith("data: ")) return;
    
    QString data = QString::fromUtf8(line.mid(6));
    if (data == "[DONE]") return;
    
    QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (doc.isNull()) return;
    
    QJsonObject obj = doc.object();
    QJsonArray choices = obj["choices"].toArray();
    if (choices.isEmpty()) return;
    
    QJsonObject choice = choices[0].toObject();
    QJsonObject delta = choice["delta"].toObject();
    
    // 累积 finish_reason
    if (choice.contains("finish_reason") && !choice["finish_reason"].isNull()) {//如果有字段，且不是null代表结束了，且如果携带工具调用的时候会显示“tool_calls”
        m_lastFinishReason = choice["finish_reason"].toString();
    }
    
    // 流式输出文本内容
    if (delta.contains("content")) {
        QString content = delta["content"].toString();
        m_fullContent += content;
        // NOTE: 只有非空内容才发射信号，避免 UI 层处理空 chunk 的边界情况
        if (!content.isEmpty()) {
            emit streamDataReceived(content);
        }
    }
    
    /*
    data: {"id":"f8ae835f-7db0-45cd-8582-302476f993b3","object":"chat.completion.chunk","created":1766478438,"model":"deepseek-chat","system_fingerprint":"fp_eaab8d114b_prod0820_fp8_kvcache"
    ,"choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"call_00_DvuHu0LSMPedPY4cTMP0s0D5","type":"function","function":{"name":"create_file","arguments":""}}]},"logprobs":null,"finish_reason":null}]}
    */
    // 累积 tool_calls
    if (delta.contains("tool_calls")) {
        QJsonArray toolCallsArray = delta["tool_calls"].toArray();
        for (const QJsonValue& tc : toolCallsArray) {
            m_streamingToolCallsJson.append(tc);
        }
    }
}

void LLMAgent::onStreamFinished() {
    m_timeoutTimer->stop();
    
    if (!m_currentReply) {
        return;
    }
    // 无论成功失败，先清空缓冲区
    m_currentReply->readAll();
    // 处理网络错误
    if (m_currentReply->error() != QNetworkReply::NoError) {
        handleNetworkError(m_currentReply->errorString());
        return;
    }
    

    const bool hasToolCalls = (m_lastFinishReason == "tool_calls");
    if (hasToolCalls && !m_streamingToolCallsJson.isEmpty()) {
        QJsonArray assembledToolCalls = mergeStreamingToolCalls(m_streamingToolCallsJson);
        
        QJsonObject assistantMsg;
        assistantMsg["role"] = "assistant";
        if (!m_fullContent.isEmpty()) {
            assistantMsg["content"] = m_fullContent;
        }
        assistantMsg["tool_calls"] = assembledToolCalls;
        m_currentMessages.append(assistantMsg);
        
        executeToolCalls(assembledToolCalls);
    } else {
        m_isToolMode = false;
        emit finished(m_fullContent);
    }
    
    // 清空临时变量
    m_fullContent.clear();
    m_lastFinishReason.clear();
    m_streamingToolCallsJson = QJsonArray();
    
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
}

void LLMAgent::handleNetworkError(const QString& errorMsg) {
    qDebug() << "[FAIL] 网络请求失败:" << errorMsg;
    emit errorOccurred(errorMsg);
    if (m_currentReply) {
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_isToolMode = false;
}

QJsonArray LLMAgent::mergeStreamingToolCalls(const QJsonArray& streamingToolCallsJson) {
    QMap<int, QJsonObject> toolCallsMap;
    
    for (const QJsonValue& tcVal : streamingToolCallsJson) {
        const QJsonObject toolObject = tcVal.toObject();
        const int index = toolObject["index"].toInt();
        QJsonObject& current = toolCallsMap[index];
        if (toolObject.contains("id")) current["id"] = toolObject["id"];
        if (toolObject.contains("type")) current["type"] = toolObject["type"];
        
        const QJsonObject funcObj = toolObject["function"].toObject();
        if (!funcObj.isEmpty()) {
            QJsonObject currentFunc = current["function"].toObject();
            if (funcObj.contains("name")) currentFunc["name"] = funcObj["name"];
            if (funcObj.contains("arguments")) {
                currentFunc["arguments"] = currentFunc["arguments"].toString()+funcObj["arguments"].toString();
            }
            current["function"] = currentFunc;
        }
    }
    
    QJsonArray result;
    for (const QJsonObject& tc : toolCallsMap.values()) {
        result.append(tc);
    }
    return result;
}

QJsonObject LLMAgent::buildApiRequestBody(const QJsonArray& messages) {
    QJsonObject root;
    root["model"] = m_config.model;
    root["max_tokens"] = m_config.maxTokens;
    root["stream"] = true;
    
    // System Prompt 作为第一条消息（始终存在）
    QJsonArray finalMessages;
    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = m_systemPrompt;
    finalMessages.append(systemMsg);
    
    // 添加用户消息
    for (const QJsonValue& msg : messages) {
        finalMessages.append(msg);
    }
    
    root["messages"] = finalMessages;
    
    // 添加工具定义
    if (!m_tools.isEmpty()) {
        QJsonArray tools;
        for (const Tool& tool : m_tools) {
            tools.append(tool.toJson());
        }
        root["tools"] = tools;
    }    
    return root;
}
