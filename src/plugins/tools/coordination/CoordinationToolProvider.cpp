#include "CoordinationToolProvider.h"

#include "AgentTool.h"

CoordinationToolProvider::CoordinationToolProvider(QObject* parent)
    : QObject(parent)
    , m_tools(AgentTool::toolSchemas())
{
    for (const Tool& tool : m_tools) {
        AgentTool* instance = new AgentTool(LLMConfig {}, nullptr, tool.name, tool.description, this);
        m_toolInstances.insert(tool.name, instance);
    }
}

QList<Tool> CoordinationToolProvider::toolSchemas()
{
    return AgentTool::toolSchemas();
}

QList<Tool> CoordinationToolProvider::listTools() const
{
    return m_tools;
}

ToolResult CoordinationToolProvider::execute(const ToolCall& call)
{
    if (AgentTool* tool = m_toolInstances.value(call.name, nullptr)) {
        QJsonObject input = call.input;
        input.insert(QStringLiteral("_tool_call_id"), call.id);
        return tool->execute(input);
    }

    return ToolResult(
        QStringLiteral("错误: 未知的团队协作工具 %1").arg(call.name),
        QStringLiteral("执行失败"),
        false);
}
