#include "ToolDispatcher.h"
#include "ToolFailureSupport.h"
#include "JsonSchemaValidator.h"
#include "core/backend/BackendPluginManager.h"
#include <QJsonDocument>
#include <QDebug>
#include <QDateTime>
#include <QElapsedTimer>
#include <exception>

ToolDispatcher* ToolDispatcher::instance()
{
    static ToolDispatcher dispatcher;
    return &dispatcher;
}

ToolDispatcher::ToolDispatcher(QObject* parent)
    : QObject(parent)
{
}

ToolDispatcher::~ToolDispatcher() = default;

void ToolDispatcher::registerProvider(IToolProvider* provider, const QString& name)
{
    if (!provider)
        return;

    const QString providerName = name.isEmpty() ? QString("provider") : name;
    
    // 15.4: 实现工具名称唯一性检查 - Provider 名称冲突检查
    if (m_providers.contains(providerName)) {
        qWarning() << "[ToolDispatcher] Provider 名称冲突:" << providerName 
                   << "- 拒绝注册重复的 Provider";
        return;
    }

    m_providers.insert(providerName, provider);
    
    // 15.5: 记录 Provider 注册日志
    qInfo() << "[ToolDispatcher] 注册 Provider:" << providerName;
    
    indexProviderTools(provider, providerName);
    
    // 15.2: 实现异步工具支持 - 监听 Provider 的 toolCompleted 信号
    // 尝试连接 toolCompleted 信号（如果 Provider 支持异步工具）
    QObject* providerObj = dynamic_cast<QObject*>(provider);
    if (providerObj) {
        // 使用 Qt 的元对象系统检查信号是否存在
        const QMetaObject* metaObj = providerObj->metaObject();
        int signalIndex = metaObj->indexOfSignal("toolCompleted(QString,ToolResult)");
        
        if (signalIndex >= 0) {
            // 连接异步工具完成信号
            connect(providerObj, SIGNAL(toolCompleted(QString,ToolResult)),
                    this, SLOT(onProviderToolCompleted(QString,ToolResult)));
            
            qDebug() << "[ToolDispatcher] Provider 支持异步工具:" << providerName;
        }
    }
}

void ToolDispatcher::refreshProvider(const QString& name)
{
    if (!m_providers.contains(name))
        return;

    const QList<QString> toolNames = m_toolOwners.keys();
    for (const QString& toolName : toolNames) {
        if (m_toolOwners.value(toolName) == name) {
            m_toolOwners.remove(toolName);
            m_toolIndex.remove(toolName);
            m_toolSchemas.remove(toolName);
        }
    }

    indexProviderTools(m_providers.value(name), name);
}

QList<Tool> ToolDispatcher::getAllToolSchemas() const
{
    return m_toolSchemas.values();
}

QStringList ToolDispatcher::toolNamesForProvider(const QString& providerName) const
{
    QStringList names;
    for (auto it = m_toolOwners.constBegin(); it != m_toolOwners.constEnd(); ++it) {
        if (it.value() == providerName)
            names.append(it.key());
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool ToolDispatcher::hasToolSchema(const QString& name) const
{
    return m_toolSchemas.contains(name);
}

ToolResult ToolDispatcher::dispatch(const ToolCall& call)
{
    const QString& toolName = call.name;
    const QString& callId = call.id;
    
    // 15.5: 记录调用开始日志（时间戳、工具名称、调用 ID）
    const qint64 startTimeMs = QDateTime::currentMSecsSinceEpoch();
    qInfo() << "[ToolDispatcher] 工具调用开始 -"
            << "时间:" << QDateTime::currentDateTime().toString(Qt::ISODate)
            << "工具:" << toolName
            << "调用ID:" << callId;
    
    QJsonObject input = call.input;
    input["_tool_call_id"] = callId;
    QString inputStr = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));

    // 16.1: 实现工具调用参数验证 - 验证工具名称存在
    // 15.1: 实现工具调用路由 - 根据工具名称查找对应的 Provider
    if (!m_toolIndex.contains(toolName)) {
        // 15.5: 记录错误日志（错误码、错误信息）
        qWarning() << "[ToolDispatcher] 工具调用失败 -"
                   << "工具:" << toolName
                   << "调用ID:" << callId
                   << "错误: 未知工具";
        
        return ToolResult(
            QString("错误: 未知的工具 %1").arg(toolName), 
            "执行失败", 
            false,
            QJsonObject{{"errorCode", "unknown_tool"}}
        );
    }

    const Tool& schema = m_toolSchemas[toolName];
    const QString desc = schema.description.isEmpty() ? toolName : schema.description;
    const QString providerName = m_toolOwners.value(toolName, "unknown");
    
    // 15.5: 记录插件 ID
    qDebug() << "[ToolDispatcher] 路由到 Provider:" << providerName;
    
    // 16.1: 使用 JSON Schema 验证器验证参数
    if (!schema.inputSchema.isEmpty()) {
        JsonSchemaValidator validator(schema.inputSchema);
        
        // 将 call.input 转换为 QJsonValue 进行验证
        QJsonValue inputValue(call.input);
        
        if (!validator.validate(inputValue)) {
            const QString validationError = validator.errorString();
            
            qWarning() << "[ToolDispatcher] 工具调用参数验证失败 -"
                       << "工具:" << toolName
                       << "调用ID:" << callId
                       << "错误:" << validationError;
            
            return ToolResult(
                QString("参数验证失败: %1").arg(validationError),
                "参数错误",
                false,
                QJsonObject{
                    {"errorCode", "invalid_parameter"},
                    {"validationErrors", validationError}
                }
            );
        }
    }
    
    emit toolStarted(desc, inputStr);

    ToolCall enriched = call;
    enriched.input = input;
    
    // 15.1: 调用 Provider::execute() 方法
    // 15.3: 实现异常处理 - 在插件边界捕获所有 C++ 异常
    ToolResult result;
    QElapsedTimer timer;
    timer.start();
    
    try {
        IToolProvider* provider = m_toolIndex.value(toolName);
        result = provider->execute(enriched);
        
        // 15.2: 实现异步工具支持 - 检测 ToolResult 是否包含 "__DEFERRED__" 前缀
        if (isDeferredToolResult(result.rawContent)) {
            qInfo() << "[ToolDispatcher] 工具返回延迟标记 -"
                    << "工具:" << toolName
                    << "调用ID:" << callId
                    << "消息:" << stripDeferredToolPrefix(result.rawContent);
            
            // 注意: 异步工具完成时，Provider 应发出 toolCompleted 信号
            // 主应用需要监听该信号并将结果传递给 Agent
            // 这部分逻辑在 LLMAgent 中处理
        }
        
    } catch (const std::exception& e) {
        // 15.3: 转换异常为 ToolResult{success=false}
        // 15.5: 记录详细堆栈信息到日志
        qCritical() << "[ToolDispatcher] 插件抛出异常 -"
                    << "工具:" << toolName
                    << "调用ID:" << callId
                    << "Provider:" << providerName
                    << "异常:" << e.what();
        
        result = ToolResult(
            QString("内部错误：插件执行异常 - %1").arg(e.what()),
            "插件执行异常",
            false,
            QJsonObject{
                {"errorCode", "plugin_exception"},
                {"exception", e.what()},
                {"pluginId", providerName}
            }
        );
        
        // 15.3: 确保其他插件继续正常工作（通过捕获异常实现）
        
    } catch (...) {
        // 15.3: 捕获未知异常
        qCritical() << "[ToolDispatcher] 插件抛出未知异常 -"
                    << "工具:" << toolName
                    << "调用ID:" << callId
                    << "Provider:" << providerName;
        
        result = ToolResult(
            "内部错误：插件执行异常（未知类型）",
            "插件执行异常",
            false,
            QJsonObject{
                {"errorCode", "unknown_exception"},
                {"pluginId", providerName}
            }
        );
    }
    
    const qint64 elapsedMs = timer.elapsed();
    const qint64 endTimeMs = QDateTime::currentMSecsSinceEpoch();
    
    // 15.5: 记录完成日志（执行时间、成功状态、结果大小）
    if (result.success) {
        qInfo() << "[ToolDispatcher] 工具调用成功 -"
                << "工具:" << toolName
                << "调用ID:" << callId
                << "执行时间:" << elapsedMs << "ms"
                << "结果大小:" << result.rawContent.size() << "字符";
    } else {
        // 15.5: 记录错误日志（错误码、错误信息）
        const QString errorCode = result.data.value("errorCode").toString("unknown_error");
        qWarning() << "[ToolDispatcher] 工具调用失败 -"
                   << "工具:" << toolName
                   << "调用ID:" << callId
                   << "执行时间:" << elapsedMs << "ms"
                   << "错误码:" << errorCode
                   << "错误信息:" << result.rawContent.left(200); // 限制日志长度
    }
    
    // 15.1: 返回 ToolResult 结构
    return ToolFailureSupport::enrichFailureResult(toolName, call.input, result);
}

void ToolDispatcher::registerAgentTools(const LLMConfig& config)
{
    Q_UNUSED(config);
}

void ToolDispatcher::clearProviders()
{
    m_providers.clear();
    m_toolIndex.clear();
    m_toolSchemas.clear();
    m_toolOwners.clear();
}

void ToolDispatcher::setDefaultAgentConfig(const LLMConfig& config)
{
    m_defaultAgentConfig = config;
}

QStringList ToolDispatcher::availableTeammateBackendIds() const
{
    return BackendPluginManager::instance()->teammateBackendIds();
}

void ToolDispatcher::indexProviderTools(IToolProvider* provider, const QString& providerName)
{
    const QList<Tool> tools = provider->listTools();
    for (const Tool& tool : tools) {
        indexToolSchema(tool, provider, providerName);
    }
}

void ToolDispatcher::indexToolSchema(const Tool& tool, IToolProvider* provider, const QString& providerName)
{
    if (!provider || tool.name.isEmpty())
        return;

    // 15.4: 实现工具名称唯一性检查 - 检查工具名称是否已存在
    if (m_toolIndex.contains(tool.name)) {
        const QString existingOwner = m_toolOwners.value(tool.name);
        
        // 如果是同一个 Provider 刷新工具，允许更新
        if (existingOwner == providerName) {
            m_toolSchemas.insert(tool.name, tool);
            qDebug() << "[ToolDispatcher] 更新工具 Schema:" << tool.name 
                     << "Provider:" << providerName;
            return;
        }
        
        // 15.4: 记录冲突警告并拒绝注册
        qWarning() << "[ToolDispatcher] 工具名冲突，拒绝注册 -" 
                   << "工具:" << tool.name 
                   << "新Provider:" << providerName 
                   << "已存在Provider:" << existingOwner;
        return;
    }

    // 15.4: 维护工具名称到提供者的映射表
    m_toolIndex.insert(tool.name, provider);
    m_toolSchemas.insert(tool.name, tool);
    m_toolOwners.insert(tool.name, providerName);
    
    // 15.5: 记录工具注册日志
    qDebug() << "[ToolDispatcher] 注册工具:" << tool.name 
             << "Provider:" << providerName;
}

void ToolDispatcher::onProviderToolCompleted(const QString& callId, const ToolResult& result)
{
    // 15.2: 实现异步工具支持 - 将完成结果传递给 Agent
    qInfo() << "[ToolDispatcher] 异步工具完成 -"
            << "调用ID:" << callId
            << "成功:" << result.success
            << "结果大小:" << result.rawContent.size() << "字符";
    
    // 发出信号通知 Agent 异步工具已完成
    emit asyncToolCompleted(callId, result);
}
