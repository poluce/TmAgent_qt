#ifndef TOOLTYPES_H
#define TOOLTYPES_H

#include "core/utils/DefaultPrompts.h"
#include "newCore/ModelId.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

/**
 * @brief 工具相关的数据结构定义
 *
 * 此文件包含工具系统中使用的所有 POD（Plain Old Data）结构体，
 * 独立于具体的业务类，避免循环依赖。
 */

// 工具定义结构体
struct Tool {
    QString name;            // 工具名称
    QString description;     // 工具描述
    QJsonObject inputSchema; // 输入参数 JSON Schema

    // 转换为 DeepSeek API 格式 (OpenAI 兼容)
    QJsonObject toJson() const
    {
        QJsonObject functionObj;
        functionObj["name"] = name;
        functionObj["description"] = description;
        functionObj["parameters"] = inputSchema; // DeepSeek 使用 parameters

        QJsonObject obj;
        obj["type"] = "function"; // DeepSeek 需要包装在 function 中
        obj["function"] = functionObj;
        return obj;
    }
};

// 工具调用请求结构体
struct ToolCall {
    QString id;        // 工具调用 ID
    QString name;      // 工具名称
    QJsonObject input; // 输入参数

    /**
     * @brief 从 DeepSeek API 格式的 JSON 解析 ToolCall
     * @param json DeepSeek 返回的工具调用 JSON 对象
     *             格式: {id, type: "function", function: {name, arguments}}
     * @return 解析后的 ToolCall 对象
     */
    static ToolCall fromDeepSeekJson(const QJsonObject& json)
    {
        ToolCall call;
        const QJsonObject functionObj = json.value(QStringLiteral("function")).toObject();

        call.id = json.value(QStringLiteral("id")).toString();
        call.name = functionObj.value(QStringLiteral("name")).toString();

        // 兼容不同 Provider 的参数格式：
        // 1) OpenAI/DeepSeek 风格: function.arguments = JSON string
        // 2) 部分中转/兼容层: function.arguments = object
        // 3) 兜底兼容: function.input = object
        const QJsonValue argsValue = functionObj.value(QStringLiteral("arguments"));
        if (argsValue.isObject()) {
            call.input = argsValue.toObject();
        } else if (argsValue.isString()) {
            const QString argsStr = argsValue.toString();
            if (!argsStr.trimmed().isEmpty()) {
                const QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
                if (argsDoc.isObject())
                    call.input = argsDoc.object();
            }
        }

        if (call.input.isEmpty()) {
            const QJsonValue inputValue = functionObj.value(QStringLiteral("input"));
            if (inputValue.isObject())
                call.input = inputValue.toObject();
        }

        return call;
    }
};

// 工具执行事件结构体（供信号传递）
struct ToolExecutionEvent {
    QString toolName;    // 工具名称
    QString toolId;      // 工具调用 ID（用于调试信息）
    QString status;      // 状态: "started", "progress", "completed"
    bool success = true; // 是否成功 (仅 completed 时有效)
    QJsonObject data;    // 附加数据（started 时为入参，completed 时可选）

    // 原始结果（仅 completed 时使用）
    QString rawResult;
    QString formattedResult;

    // 默认构造函数
    ToolExecutionEvent() = default;

    /**
     * @brief 从 ToolCall 创建 started 事件
     * @param call 工具调用信息
     */
    explicit ToolExecutionEvent(const ToolCall& call)
        : toolName(call.name)
        , toolId(call.id)
        , status("started")
        , success(true)
        , data(call.input)
    {
    }

    /**
     * @brief 根据当前状态生成用户友好消息
     */
    QString userMessage() const
    {
        if (status == "started") {
            return QString("正在执行 %1...").arg(toolName);
        } else if (status == "completed") {
            return formattedResult;
        }
        return QString();
    }

    /**
     * @brief 根据当前状态生成调试消息
     */
    QString debugMessage() const
    {
        if (status == "started") {
            return QString("正在执行 ID: %1, 参数: %2")
                .arg(toolId)
                .arg(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
        } else if (status == "completed") {
            return QString("工具执行完成, ID: %1\n原始结果: %2").arg(toolId, rawResult);
        }
        return QString();
    }
};

// Agent 配置结构体（仅包含角色相关信息）
struct LLMConfig {
    // === Agent 标识 ===
    QString uuid;     // 唯一代号 (UUID)
    QString userName; // 显示名称 (如 "代码专家")

    // === 模型与角色 ===
    QString configId;                 // 优先用 configId 查找 ModelFactory
    ModelId model = ModelId::Unknown; // 模型枚举（向后兼容）
    QString customModelId;            // 自定义模型 ID（当 model = Custom）
    QString systemPrompt = DefaultPrompts::codingAssistantSystemPrompt();
    QString workspaceDir; // Agent 独立工作空间（默认由 ChatService 注入）

    // === 递归控制 ===
    // 3 = 主 Agent (可以委派给 Depth 2)
    // 2 = 子 Agent (可以委派给 Depth 1)
    // ...
    // 0 = 叶子 Agent (禁止委派)
    int recursionDepth = 3;

    // === 辅助方法 ===
    bool isValid() const
    {
        // configId 优先：有 configId 即可
        if (!configId.trimmed().isEmpty())
            return true;
        // 向后兼容：无 configId 时走旧逻辑
        if (model == ModelId::Unknown)
            return false;
        if (model == ModelId::Custom)
            return !customModelId.trimmed().isEmpty();
        return true;
    }
    bool canDelegate() const { return recursionDepth > 0; } // 深度大于0才允许委派
};

/**
 * @brief 工具执行结果结构化对象
 */
struct ToolResult {
    QString rawContent;  // 给 LLM 看的完整数据
    QString userSummary; // 给用户看的简短摘要
    bool success = true; // 执行状态
    QJsonObject data;    // 给运行时/观测侧的结构化元数据

    ToolResult() = default;
    ToolResult(const QString& raw, const QString& summary, bool ok = true, const QJsonObject& extraData = QJsonObject())
        : rawContent(raw), userSummary(summary), success(ok), data(extraData) { }
};

// 工具延迟完成标记（用于异步工具）
static const QString kDeferredToolPrefix = QStringLiteral("__DEFERRED__");
inline QString makeDeferredToolResult(const QString& message) { return kDeferredToolPrefix + message; }
inline bool isDeferredToolResult(const QString& raw) { return raw.startsWith(kDeferredToolPrefix); }
inline QString stripDeferredToolPrefix(const QString& raw)
{
    return isDeferredToolResult(raw) ? raw.mid(kDeferredToolPrefix.size()) : raw;
}

/**
 * @brief 工具接口基类 (ITool)
 */
class ITool {
public:
    virtual ~ITool() = default;
    virtual Tool getSchema() const = 0;
    virtual ToolResult execute(const QJsonObject& args) = 0;
};

#endif // TOOLTYPES_H
