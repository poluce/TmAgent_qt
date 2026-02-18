#ifndef TOOLREGISTRATIONHELPERS_H
#define TOOLREGISTRATIONHELPERS_H

#include "core/agent/ToolTypes.h"
#include "core/utils/ToolSchemaLoader.h"
#include <QJsonArray>
#include <QRegularExpression>

namespace ToolRegistrationHelpers {

inline Tool resolveToolSchema(const QString& name, const QString& fallbackDesc)
{
    Tool tool = ToolSchemaLoader::getToolSchema(name);
    if (tool.name.isEmpty()) {
        tool.name = name;
        tool.description = fallbackDesc;
        QJsonObject schema;
        schema["type"] = "object";
        schema["properties"] = QJsonObject();
        schema["required"] = QJsonArray();
        tool.inputSchema = schema;
    }
    return tool;
}

inline bool isOkResult(const QString& raw)
{
    const QString text = raw.trimmed();
    if (text.startsWith(QStringLiteral("错误"))
        || text.startsWith(QStringLiteral("抓取失败"))
        || text.startsWith(QStringLiteral("搜索失败"))) {
        return false;
    }

    // execute_command 输出会包含 "退出码: N"。存在该字段时以退出码为准。
    static const QRegularExpression kExitCodeRe(
        QStringLiteral("(?:^|\\n)\\s*退出码\\s*:\\s*(-?\\d+)"));
    const QRegularExpressionMatch m = kExitCodeRe.match(text);
    if (m.hasMatch())
        return m.captured(1).toInt() == 0;

    return true;
}

inline ToolResult wrapResult(const QString& raw, const QString& okSummary, const QString& failSummary)
{
    bool ok = isOkResult(raw);
    return ToolResult(raw, ok ? okSummary : failSummary, ok);
}

} // namespace ToolRegistrationHelpers

/**
 * @brief 定义并注册一个简单的工具包装类
 *
 * 用法: DEFINE_SIMPLE_TOOL(ClassName, "tool_name", "fallback desc", StaticFunc, "ok msg", "fail msg")
 * 展开后生成一个 ITool 子类并调用 REGISTER_TOOL_INSTANCE 完成自注册。
 */
#define DEFINE_SIMPLE_TOOL(ClassName, ToolName, FallbackDesc, ExecFunc, OkMsg, FailMsg) \
    class ClassName : public ITool { \
    public: \
        Tool getSchema() const override \
        { \
            return ToolRegistrationHelpers::resolveToolSchema(ToolName, FallbackDesc); \
        } \
        ToolResult execute(const QJsonObject& args) override \
        { \
            QString res = ExecFunc(args); \
            return ToolRegistrationHelpers::wrapResult(res, OkMsg, FailMsg); \
        } \
    }; \
    REGISTER_TOOL_INSTANCE(ClassName, ToolName)

#endif // TOOLREGISTRATIONHELPERS_H
