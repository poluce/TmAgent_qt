#include "CoordinationToolProvider.h"

#include "AgentTool.h"

CoordinationToolProvider::CoordinationToolProvider(TmAgent::IToolPluginHost* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_tools(AgentTool::toolSchemas())
{
    for (const TmAgent::Tool& tool : m_tools) {
        AgentTool* instance = new AgentTool(m_host, LLMConfig {}, nullptr, tool.name, tool.description, this);
        m_toolInstances.insert(tool.name, instance);
    }
}

QList<TmAgent::Tool> CoordinationToolProvider::toolSchemas()
{
    return AgentTool::toolSchemas();
}

QList<TmAgent::Tool> CoordinationToolProvider::listTools() const
{
    return m_tools;
}

TmAgent::ToolResult CoordinationToolProvider::execute(const TmAgent::ToolCall& call)
{
    if (AgentTool* tool = m_toolInstances.value(call.name, nullptr)) {
        QJsonObject input = call.input;
        input.insert(QStringLiteral("_tool_call_id"), call.id);
        return tool->execute(input);
    }

    return TmAgent::ToolResult(
        QStringLiteral("错误: 未知的团队协作工具 %1").arg(call.name),
        QStringLiteral("执行失败"),
        false);
}
