#include "LLMAgent.h"
#include "core/utils/ConfigManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QDebug>
#include <QTimer>

LLMAgent::LLMAgent(QObject *parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(30000);  // 30秒超时
    
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "⚠️ 网络请求超时!";
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

void LLMAgent::ask(const QString& prompt) {
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
    m_saveToHistory = saveToHistory;  // 记录是否需要保存历史
    
    // 优先使用 m_config,如果为空则使用 ConfigManager
    QString apiKey = m_config.apiKey.isEmpty() ? ConfigManager::getApiKey() : m_config.apiKey;
    QString baseUrl = m_config.baseUrl.isEmpty() ? ConfigManager::getBaseUrl() : m_config.baseUrl;
    QString model = m_config.model.isEmpty() ? ConfigManager::getModel() : m_config.model;

    if (apiKey.isEmpty()) {
        emit errorOccurred("API Key is empty! Please configure it first.");
        return;
    }

    // 只有在保存历史时才添加到对话历史
    if (saveToHistory) {
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = prompt;
        m_conversationHistory.append(userMsg);
    }

    QUrl url(baseUrl + "/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());

    // 构造请求 Body
    QJsonObject root;
    root["model"] = model;
    root["stream"] = true; // 强制开启流式

    QJsonArray messages;
    
    // 注入角色设定 (System Prompt)
    if (!m_systemPrompt.isEmpty()) {
        QJsonObject sysObj;
        sysObj["role"] = "system";
        sysObj["content"] = m_systemPrompt;
        messages.append(sysObj);
    }

    if (saveToHistory) {
        // 添加所有历史对话
        for (const QJsonValue& msg : m_conversationHistory) {
            messages.append(msg);
        }
    } else {
        // 不保存历史时,只发送当前问题
        QJsonObject userMsg;
        userMsg["role"] = "user";
        userMsg["content"] = prompt;
        messages.append(userMsg);
    }
    
    root["messages"] = messages;

    m_currentReply = m_manager->post(request, QJsonDocument(root).toJson());

    connect(m_currentReply, &QNetworkReply::readyRead, this, &LLMAgent::onReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &LLMAgent::onFinished);
    connect(m_currentReply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error), this, &LLMAgent::onError);
}

void LLMAgent::abort() {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

void LLMAgent::onReadyRead() {
    if (!m_currentReply) return;

    while (m_currentReply->canReadLine()) {
        QByteArray line = m_currentReply->readLine().trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith("data: ")) {
            QString data = QString::fromUtf8(line.mid(6));
            if (data == "[DONE]") {
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
            if (!doc.isNull()) {
                QJsonObject obj = doc.object();
                QJsonArray choices = obj["choices"].toArray();
                if (!choices.isEmpty()) {
                    QJsonObject delta = choices[0].toObject()["delta"].toObject();
                    if (delta.contains("content")) {
                        QString content = delta["content"].toString();
                        m_fullContent += content;
                        emit chunkReceived(content);
                    }
                }
            }
        }
    }
}

void LLMAgent::onFinished() {
    if (m_currentReply) {
        if (m_currentReply->error() == QNetworkReply::NoError) {
            // 只有在保存历史模式下才将助手回复添加到对话历史
            if (m_saveToHistory) {
                QJsonObject assistantMsg;
                assistantMsg["role"] = "assistant";
                assistantMsg["content"] = m_fullContent;
                m_conversationHistory.append(assistantMsg);
            }
            
            emit finished(m_fullContent);
        }
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

void LLMAgent::onError(QNetworkReply::NetworkError code) {
    if (code != QNetworkReply::OperationCanceledError) {
        emit errorOccurred(m_currentReply->errorString());
    }
}

void LLMAgent::clearHistory() {
    m_conversationHistory = QJsonArray();
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

// ==================== 带工具的问答 ====================

void LLMAgent::askWithTools(const QString& prompt) {
    if (m_currentReply) {
        abort();
    }
    
    m_isToolMode = true;
    m_pendingToolCalls.clear();
    m_toolResults.clear();
    
    // 构造初始消息
    QJsonArray messages;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = prompt;
    messages.append(userMsg);
    
    m_currentMessages = messages;
    sendRequestWithTools(messages);
}

void LLMAgent::sendRequestWithTools(const QJsonArray& messages) {
    // 获取配置
    QString apiKey = m_config.apiKey.isEmpty() 
        ? ConfigManager::getApiKey() : m_config.apiKey;
    QString baseUrl = m_config.baseUrl.isEmpty() 
        ? ConfigManager::getBaseUrl() : m_config.baseUrl;
    QString model = m_config.model.isEmpty() 
        ? ConfigManager::getModel() : m_config.model;
    
    if (apiKey.isEmpty()) {
        emit errorOccurred("API Key is empty! Please configure it first.");
        return;
    }
    
    // 构造请求
    QJsonObject root;
    root["model"] = model;
    root["max_tokens"] = 4096;
    
    // DeepSeek: System Prompt 需要作为第一条消息
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
    
    // 发送请求到 DeepSeek API
    QUrl url(baseUrl + "/chat/completions");  // DeepSeek 使用 /chat/completions
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer %1").arg(apiKey).toUtf8());  // DeepSeek 使用 Bearer
    
    qDebug() << "========== 发送工具请求 ==========";
    qDebug() << "模型:" << model;
    qDebug() << "工具数量:" << m_tools.size();
    qDebug() << "消息数量:" << finalMessages.size();
    
    // 输出完整请求体用于调试
    qDebug() << "---------- 完整请求体 ----------";
    QString requestBody = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
    qDebug().noquote() << requestBody;
    qDebug() << "====================================";
    
    // 清理旧的请求（如果存在）
    if (m_currentReply) {
        m_currentReply->disconnect();
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    
    m_currentReply = m_manager->post(request, QJsonDocument(root).toJson());
    
    // 添加调试：监控数据接收
    connect(m_currentReply, &QNetworkReply::readyRead, this, [this]() {
        qDebug() << "📥 收到数据块,当前可读字节:" << m_currentReply->bytesAvailable();
    });
    
    // 启动超时定时器
    m_timeoutTimer->start();
    qDebug() << "⏱️ 启动30秒超时定时器";
    
    connect(m_currentReply, &QNetworkReply::finished, this, [this]() {
        // 停止超时定时器
        m_timeoutTimer->stop();
        
        qDebug() << "========== 网络请求完成 ==========";
        
        if (!m_currentReply) {
            qDebug() << "错误: m_currentReply 为空";
            return;
        }
        
        if (m_currentReply->error() != QNetworkReply::NoError) {
            // 详细输出错误信息用于调试
            qDebug() << "❌ 网络请求失败!";
            qDebug() << "错误码:" << m_currentReply->error();
            qDebug() << "错误描述:" << m_currentReply->errorString();
            
            // 获取 HTTP 状态码
            int httpStatus = m_currentReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qDebug() << "HTTP 状态码:" << httpStatus;
            
            // 尝试读取错误响应体
            QByteArray errorBody = m_currentReply->readAll();
            if (!errorBody.isEmpty()) {
                qDebug() << "错误响应体:" << QString::fromUtf8(errorBody);
            }
            
            emit errorOccurred(m_currentReply->errorString());
            m_currentReply->deleteLater();
            m_currentReply = nullptr;
            m_isToolMode = false;  // 重置工具模式
            return;
        }
        
        QByteArray data = m_currentReply->readAll();
        qDebug() << "收到响应,大小:" << data.size() << "字节";
        
        if (data.isEmpty()) {
            qDebug() << "错误: 响应数据为空";
            emit errorOccurred("Empty response from server");
            m_currentReply->deleteLater();
            m_currentReply = nullptr;
            m_isToolMode = false;
            return;
        }
        
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull()) {
            qDebug() << "错误: JSON 解析失败";
            emit errorOccurred("Invalid JSON response");
            m_currentReply->deleteLater();
            m_currentReply = nullptr;
            m_isToolMode = false;
            return;
        }
        
        parseNonStreamResponse(doc.object());
        
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    });
}

void LLMAgent::parseNonStreamResponse(const QJsonObject& response) {
    // 输出完整响应 (使用 QString 避免十六进制编码)
    qDebug() << "========== DeepSeek 完整响应 ==========";
    QString jsonStr = QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Indented));
    qDebug().noquote() << jsonStr;
    qDebug() << "======================================";
    
    // DeepSeek 响应格式: choices[0].message
    QJsonArray choices = response["choices"].toArray();
    if (choices.isEmpty()) {
        qDebug() << "错误: 响应中没有 choices";
        emit errorOccurred("Invalid response: no choices");
        return;
    }
    
    QJsonObject choice = choices[0].toObject();
    QJsonObject message = choice["message"].toObject();
    QString finishReason = choice["finish_reason"].toString();
    
    qDebug() << "finish_reason:" << finishReason;
    qDebug() << "role:" << message["role"].toString();
    
    // 检查是否有工具调用
    if (message.contains("tool_calls")) {
        QJsonArray toolCalls = message["tool_calls"].toArray();
        
        qDebug() << "✅ 检测到工具调用,数量:" << toolCalls.size();
        
        if (!toolCalls.isEmpty()) {
            // 将 assistant 的响应添加到消息历史
            m_currentMessages.append(message);
            
            // 处理工具调用
            handleToolUseResponse(toolCalls);
            return;
        }
    } else {
        qDebug() << "⚠️ 没有工具调用,返回普通文本";
    }
    
    // 正常文本回复
    QString content = message["content"].toString();
    qDebug() << "文本回复:" << content;
    qDebug() << "🔄 重置工具模式,触发 finished 信号";
    m_isToolMode = false;
    emit finished(content);
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
            
            qDebug() << "工具调用请求:" << call.name << "ID:" << call.id;
            qDebug() << "参数:" << call.input;
            
            // 触发信号,让外部执行工具
            emit toolCallRequested(call.id, call.name, call.input);
        }
    }
}


void LLMAgent::submitToolResult(const QString& toolId, const QString& result) {
    qDebug() << "提交工具结果, ID:" << toolId << "结果:" << result;
    
    m_toolResults[toolId] = result;
    
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
        // 简化工具结果,移除换行符并限制长度
        QString result = m_toolResults[call.id];
        result.replace("\r\n", " ");  // 移除 Windows 换行符
        result.replace("\n", " ");    // 移除 Unix 换行符
        result.replace("\r", " ");    // 移除旧 Mac 换行符
        result = result.trimmed();    // 移除首尾空格
        
        // 限制长度为200字符
        if (result.length() > 200) {
            result = result.left(200) + "...";
        }
        
        qDebug() << "简化后的工具结果:" << result;
        
        QJsonObject toolMsg;
        toolMsg["role"] = "tool";  // DeepSeek 使用 "tool" 角色
        toolMsg["tool_call_id"] = call.id;
        toolMsg["content"] = result;
        
        m_currentMessages.append(toolMsg);
    }
    
    qDebug() << "继续对话,包含工具结果";
    
    // 使用 QTimer::singleShot 延迟发送，确保当前请求的 finished 处理完全结束
    // 这是因为 submitToolResult 可能在 finished lambda 内部被同步调用
    QTimer::singleShot(0, this, [this]() {
        qDebug() << "🚀 延迟触发: 发送包含工具结果的请求";
        sendRequestWithTools(m_currentMessages);
    });
}
