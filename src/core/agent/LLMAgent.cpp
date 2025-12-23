#include "LLMAgent.h"
#include "core/utils/ConfigManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QDebug>
#include <QTimer>
#include <QRegularExpression>
#include <QFileInfo>

LLMAgent::LLMAgent(QObject *parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(30000);  // 30秒超时
    
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
    m_systemPrompt = prompt;
}

void LLMAgent::setConfig(const LLMConfig& config) {
    m_config = config;
}

LLMConfig LLMAgent::getConfig() const {
    return m_config;
}

void LLMAgent::sendMessage(const QString& prompt) {
    sendPromptInternal(prompt, true);  // 保存历史
}

void LLMAgent::askOnce(const QString& prompt) {
    sendPromptInternal(prompt, false);  // 不保存历史
}

void LLMAgent::sendPromptInternal(const QString& prompt, bool saveToHistory) {
    if (m_currentReply) {
        abort();
    }

    m_fullContent.clear();
    m_saveToHistory = saveToHistory;
    const bool hasTools = !m_tools.isEmpty();
    m_isToolMode = hasTools;
    
    // 构造用户消息
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt;
    
    QJsonArray messages;
    
    if (saveToHistory) {
        // 添加到对话历史
        m_conversationHistory.append(userMsg);
    }
    
    if (hasTools) {
        m_pendingToolCalls.clear();
        m_toolResults.clear();
        if (saveToHistory) {
            m_currentMessages.append(userMsg);
        } else {
            m_currentMessages = QJsonArray();
            m_currentMessages.append(userMsg);
        }
        messages = m_currentMessages;
    } else if (saveToHistory) {
        // 使用完整对话历史
        for (const QJsonValue& msg : m_conversationHistory) {
            messages.append(msg);
        }
    } else {
        // 单次问答，只发送当前消息
        messages.append(userMsg);
    }
    
    // 委托给统一的发送函数，按已注册工具自动附带
    sendMessageInternal(messages);
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
    qDebug() << "注册工具:" << tool.name;
}

void LLMAgent::clearTools() {
    m_tools.clear();
    qDebug() << "清空所有工具";
}

QList<Tool> LLMAgent::getTools() const {
    return m_tools;
}

// 阶段三: 输出模式设置
void LLMAgent::setOutputMode(OutputMode mode) {
    m_outputMode = mode;
    qDebug() << "输出模式切换为:" << (mode == Debug ? "Debug" : "UserFriendly");
}

void LLMAgent::sendMessageInternal(const QJsonArray& messages) {
    // 获取配置
    QString apiKey = m_config.apiKey.isEmpty() 
        ? ConfigManager::getApiKey() : m_config.apiKey;
    QString baseUrl = m_config.baseUrl.isEmpty() 
        ? ConfigManager::getBaseUrl() : m_config.baseUrl;
    
    if (apiKey.isEmpty()) {
        emit errorOccurred("API Key is empty! Please configure it first.");
        return;
    }
    
    // 构造请求（已注册工具会自动附带）
    QJsonObject root = buildRequestJson(messages);
    
    // 发送请求到 DeepSeek API
    QUrl url(baseUrl + "/chat/completions");  // DeepSeek 使用 /chat/completions
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());  // DeepSeek 使用 Bearer
    

    // 清理旧的请求（如果存在）
    if (m_currentReply) {
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    
    // 创建新请求
    m_currentReply = m_manager->post(request, QJsonDocument(root).toJson());
    
    // NOTE: 流式数据处理 - 委托给 processStreamChunk
    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_currentReply) return;
        while (m_currentReply->canReadLine()) {
            QByteArray line = m_currentReply->readLine().trimmed();
            if (!line.isEmpty()) {
                processStreamChunk(line);
            }
        }
    });
    
    // 启动超时定时器
    m_timeoutTimer->start();
    
    // NOTE: 请求完成处理 - 委托给 handleStreamFinished
    connect(m_currentReply, &QNetworkReply::finished, this, 
            &LLMAgent::handleStreamFinished);
}



void LLMAgent::handleToolUseResponse(const QJsonArray& toolCalls) {
    m_pendingToolCalls.clear();
    
    // 解析所有工具调用请求 (DeepSeek 格式)
    for (const QJsonValue& item : toolCalls) {
        QJsonObject obj = item.toObject();
        
        // DeepSeek 格式: {id, type: "function", function: {name, arguments}}
        QString type = obj["type"].toString();
        if (type == "function") {
            QJsonObject functionObj = obj["function"].toObject();
            
            ToolCall call;
            call.id = obj["id"].toString();
            call.name = functionObj["name"].toString();
            
            // arguments 是 JSON 字符串,需要解析
            QString argsStr = functionObj["arguments"].toString();
            QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
            call.input = argsDoc.object();
            
            m_pendingToolCalls.append(call);
            
            // NOTE: 发射工具执行开始信号
            QString description = QString("执行 %1").arg(call.name);
            emit toolExecutionStarted(call.name, description);
            
            // NOTE: 发射结构化 toolEvent 信号
            ToolExecutionEvent event;
            event.toolName = call.name;
            event.status = "started";
            event.success = true;
            event.userMessage = QString("[Tool] 正在执行 %1...").arg(call.name);
            event.debugMessage = QString("工具调用 ID: %1, 参数: %2")
                .arg(call.id)
                .arg(QString::fromUtf8(QJsonDocument(call.input).toJson(QJsonDocument::Compact)));
            event.data = call.input;
            emit toolEvent(event);
            
            // 触发信号,让外部执行工具
            emit toolCallRequested(call.id, call.name, call.input);
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
    QString formattedResult = formatToolResultForUser(toolName, result);
    
    // 发射工具执行完成信号 (UI 显示用)
    bool success = !result.contains("失败") && !result.contains("错误");
    emit toolExecutionCompleted(toolName, success, formattedResult);
    
    // 发射结构化 toolEvent 信号
    ToolExecutionEvent event;
    event.toolName = toolName;
    event.status = "completed";
    event.success = success;
    event.userMessage = formattedResult;  // UI 显示用摘要
    event.debugMessage = QString("工具执行完成, ID: %1\n原始结果: %2").arg(toolId, result);
    event.data = QJsonObject{{"result", result}, {"formatted", formattedResult}};
    emit toolEvent(event);
    
    // 检查是否所有工具都已返回结果
    bool allCompleted = true;
    for (const ToolCall& call : m_pendingToolCalls) {
        if (!m_toolResults.contains(call.id)) {
            allCompleted = false;
            break;
        }
    }
    
    if (allCompleted) {
        continueConversationWithToolResults();
    }
}

void LLMAgent::continueConversationWithToolResults() {
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
        sendMessageInternal(m_currentMessages);
    });
}

// ==================== 阶段一:结果格式化和智能摘要 ====================

QString LLMAgent::formatToolResultForUser(const QString& toolName, const QString& rawResult) {
    if (toolName == "execute_command") {
        return extractCommandSummary(rawResult);
    } else if (toolName == "create_file") {
        return extractFileSummary(rawResult);
    }
    
    // 未知工具,返回原始结果
    return rawResult;
}

QString LLMAgent::extractCommandSummary(const QString& cmdOutput) {
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
            // 成功执行,提取关键信息
            
            // 检测是否是目录列表
            if (stdOutput.contains("Makefile") || 
                stdOutput.contains("Directory") ||
                stdOutput.contains(".exe") ||
                stdOutput.contains("debug") ||
                stdOutput.contains("release")) {
                
                // 统计文件和目录数量
                QStringList items = stdOutput.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                int count = items.size();
                return QString("[OK] 找到 %1 个文件/目录").arg(count);
            }
            
            // 检测是否是路径信息
            if (stdOutput.startsWith("/") || stdOutput.contains(":\\")) {
                return QString("📂 当前路径: %1").arg(stdOutput);
            }
            
            // 其他情况,显示前 100 字符
            if (stdOutput.length() > 100) {
                return QString("[OK] 执行成功\n%1...").arg(stdOutput.left(100));
            } else {
                return QString("[OK] 执行成功\n%1").arg(stdOutput);
            }
        } else if (exitCode == 0) {
            return "[OK] 命令执行成功";
        } else {
            return QString("[FAIL] 命令执行失败 (退出码: %1)").arg(exitCode);
        }
    }
    
    // 无法解析,返回原始结果
    return cmdOutput;
}

QString LLMAgent::extractFileSummary(const QString& fileResult) {
    // 解析文件操作结果
    
    if (fileResult.contains("成功")) {
        // 提取文件路径
        QRegularExpression re("文件已创建:\\s*(.+)");
        QRegularExpressionMatch match = re.match(fileResult);
        
        if (match.hasMatch()) {
            QString filePath = match.captured(1).trimmed();
            // 只显示文件名
            QFileInfo fileInfo(filePath);
            return QString("[OK] 文件 %1 创建成功").arg(fileInfo.fileName());
        }
        
        return "[OK] 文件创建成功";
    } else if (fileResult.contains("失败") || fileResult.contains("错误")) {
        return "[FAIL] 文件创建失败";
    }
    
    return fileResult;
}

// ==================== SSE 流处理辅助函数 ====================

void LLMAgent::processStreamChunk(const QByteArray& line) {
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
    if (choice.contains("finish_reason") && !choice["finish_reason"].isNull()) {
        m_lastFinishReason = choice["finish_reason"].toString();
        qDebug() << "[Detect] 检测到 finish_reason:" << m_lastFinishReason;
    }
    
    // 流式输出文本内容
    if (delta.contains("content")) {
        QString content = delta["content"].toString();
        m_fullContent += content;
        emit chunkReceived(content);
    }
    
    // 累积 tool_calls
    if (delta.contains("tool_calls")) {
        QJsonArray toolCallsArray = delta["tool_calls"].toArray();
        for (const QJsonValue& tc : toolCallsArray) {
            m_streamingToolCallsJson.append(tc);
        }
    }
}

void LLMAgent::handleStreamFinished() {
    m_timeoutTimer->stop();
    
    if (!m_currentReply) {
        qDebug() << "错误: m_currentReply 为空";
        return;
    }
    
    // 处理网络错误
    if (m_currentReply->error() != QNetworkReply::NoError) {
        qDebug() << "[FAIL] 网络请求失败:" << m_currentReply->errorString();
        
        emit errorOccurred(m_currentReply->errorString());
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
        m_isToolMode = false;
        return;
    }
    
    m_currentReply->readAll();
    // 根据 finish_reason 判断响应类型
    if (m_lastFinishReason == "tool_calls") {
        if (!m_streamingToolCallsJson.isEmpty()) {
            QJsonArray assembledToolCalls = this->assembleToolCalls();
            
            // 构造 assistant message 并添加到历史
            QJsonObject assistantMsg;
            assistantMsg["role"] = "assistant";
            if (!m_fullContent.isEmpty()) {
                assistantMsg["content"] = m_fullContent;
            }
            assistantMsg["tool_calls"] = assembledToolCalls;
            m_currentMessages.append(assistantMsg);
            
            handleToolUseResponse(assembledToolCalls);
        } else {
            qDebug() << "WARNING: finish_reason 是 tool_calls 但没有累积到工具调用数据";
            m_isToolMode = false;
            emit finished(m_fullContent);
        }
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

QJsonArray LLMAgent::assembleToolCalls() {
    QMap<int, QJsonObject> toolCallsMap;
    
    for (const QJsonValue& tcVal : m_streamingToolCallsJson) {
        QJsonObject tc = tcVal.toObject();
        int index = tc["index"].toInt();
        
        if (!toolCallsMap.contains(index)) {
            toolCallsMap[index] = QJsonObject();
        }
        
        // 合并字段
        QJsonObject& current = toolCallsMap[index];
        if (tc.contains("id")) current["id"] = tc["id"];
        if (tc.contains("type")) current["type"] = tc["type"];
        
        if (tc.contains("function")) {
            QJsonObject funcObj = tc["function"].toObject();
            QJsonObject currentFunc = current["function"].toObject();
            
            if (funcObj.contains("name")) {
                currentFunc["name"] = funcObj["name"];
            }
            if (funcObj.contains("arguments")) {
                QString args = currentFunc["arguments"].toString();
                args += funcObj["arguments"].toString();
                currentFunc["arguments"] = args;
            }
            
            current["function"] = currentFunc;
        }
    }
    
    // 转换为数组
    QJsonArray result;
    for (const QJsonObject& tc : toolCallsMap.values()) {
        result.append(tc);
    }
    return result;
}

QJsonObject LLMAgent::buildRequestJson(const QJsonArray& messages) {
    QString model = m_config.model.isEmpty() 
        ? ConfigManager::getModel() : m_config.model;
    
    QJsonObject root;
    root["model"] = model;
    root["max_tokens"] = 4096;
    root["stream"] = true;
    
    // System Prompt 作为第一条消息
    QJsonArray finalMessages;
    if (!m_systemPrompt.isEmpty()) {
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = m_systemPrompt;
        finalMessages.append(systemMsg);
    }
    
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
