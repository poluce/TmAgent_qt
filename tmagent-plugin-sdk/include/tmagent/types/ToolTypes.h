#ifndef TMAGENT_TYPES_TOOLTYPES_H
#define TMAGENT_TYPES_TOOLTYPES_H

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>

namespace TmAgent {

/**
 * @brief Tool 结构 - 描述工具定义
 * 
 * 用于定义一个可被 AI Agent 调用的工具，包含工具名称、描述和参数 Schema。
 * 符合 OpenAI 工具调用协议。
 */
struct Tool {
    QString name;                // 工具名称
    QString description;         // 工具描述
    QJsonObject inputSchema;     // 输入参数的 JSON Schema
    
    /**
     * @brief 序列化为 JSON 对象（OpenAI 格式）
     * @return 包含 type 和 function 字段的 JSON 对象
     */
    QJsonObject toJson() const {
        QJsonObject functionObj;
        functionObj["name"] = name;
        functionObj["description"] = description;
        functionObj["parameters"] = inputSchema;
        
        QJsonObject obj;
        obj["type"] = "function";
        obj["function"] = functionObj;
        return obj;
    }
};

/**
 * @brief ToolCall 结构 - 工具调用请求
 * 
 * 表示 LLM 发起的工具调用请求，包含调用 ID、工具名称和输入参数。
 */
struct ToolCall {
    QString id;              // 调用 ID（唯一标识）
    QString name;            // 工具名称
    QJsonObject input;       // 输入参数
    
    /**
     * @brief 从 JSON 对象反序列化（OpenAI 格式）
     * @param json LLM 返回的 tool_call JSON 对象
     * @return ToolCall 实例
     */
    static ToolCall fromJson(const QJsonObject& json) {
        ToolCall call;
        call.id = json.value("id").toString();
        
        QJsonObject functionObj = json.value("function").toObject();
        call.name = functionObj.value("name").toString();
        
        // 解析 arguments 字符串为 JSON 对象
        QString argsStr = functionObj.value("arguments").toString();
        QJsonDocument argsDoc = QJsonDocument::fromJson(argsStr.toUtf8());
        if (argsDoc.isObject()) {
            call.input = argsDoc.object();
        }
        
        return call;
    }
};

/**
 * @brief ToolResult 结构 - 工具执行结果
 * 
 * 表示工具执行的结果，包含给 LLM 的完整数据、给用户的摘要、执行状态和元数据。
 */
struct ToolResult {
    QString rawContent;      // 给 LLM 的完整数据
    QString userSummary;     // 给用户的简短摘要
    bool success;            // 执行状态（true=成功，false=失败）
    QJsonObject data;        // 结构化元数据（错误码、诊断信息等）
    
    // 默认构造函数
    ToolResult() : success(true) {}
    
    // 便捷构造函数
    ToolResult(const QString& raw, const QString& summary = QString(), 
               bool ok = true, const QJsonObject& extraData = QJsonObject())
        : rawContent(raw)
        , userSummary(summary.isEmpty() ? raw : summary)
        , success(ok)
        , data(extraData) 
    {}
};

} // namespace TmAgent

#endif // TMAGENT_TYPES_TOOLTYPES_H
