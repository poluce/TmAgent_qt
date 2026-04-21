#include "ShellToolSchemas.h"

#include <tmagent/support/ToolSchemaBuilder.h>

using namespace TmAgent;

// Helper function to create a Tool from schema components
static Tool makeTool(const QString& name, const QString& description,
                    const QJsonObject& properties, const QStringList& required = QStringList())
{
    Tool tool;
    tool.name = name;
    tool.description = description;
    tool.inputSchema = makeToolSchema(name, description, properties, required);
    return tool;
}

QList<TmAgent::Tool> shellTools()
{
    return {
        makeTool(
            QStringLiteral("execute_command"),
            QStringLiteral("执行终端命令并返回结果。"),
            QJsonObject {
                { QStringLiteral("command"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要执行的命令，例如: dir, git status, qmake")) },
                { QStringLiteral("working_directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("工作目录（可选）")) }
            },
            QStringList { QStringLiteral("command") })
    };
}
