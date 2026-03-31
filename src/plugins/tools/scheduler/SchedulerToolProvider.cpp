#include "SchedulerToolProvider.h"

#include "SchedulerTool.h"

SchedulerToolProvider::SchedulerToolProvider(QObject* parent)
    : QObject(parent)
    , m_tools(SchedulerTool::toolSchemas())
{
}

QList<Tool> SchedulerToolProvider::toolSchemas()
{
    return SchedulerTool::toolSchemas();
}

QList<Tool> SchedulerToolProvider::listTools() const
{
    return m_tools;
}

ToolResult SchedulerToolProvider::execute(const ToolCall& call)
{
    QJsonObject input = call.input;
    input.insert(QStringLiteral("_tool_call_id"), call.id);

    if (call.name == QStringLiteral("scheduler_list"))
        return SchedulerTool::executeList(input);
    if (call.name == QStringLiteral("scheduler_create"))
        return SchedulerTool::executeCreate(input);
    if (call.name == QStringLiteral("scheduler_update"))
        return SchedulerTool::executeUpdate(input);
    if (call.name == QStringLiteral("scheduler_delete"))
        return SchedulerTool::executeDelete(input);
    if (call.name == QStringLiteral("scheduler_run"))
        return SchedulerTool::executeRun(input);

    return ToolResult(
        QStringLiteral("错误: 未知的工具 %1").arg(call.name),
        QStringLiteral("执行失败"),
        false);
}
