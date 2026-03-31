#include "ShellToolSchemas.h"

#include "core/tools/ToolSchemaSupport.h"

QList<Tool> shellTools()
{
    return {
        makeToolSchema(
            QStringLiteral("execute_command"),
            QStringLiteral("执行终端命令并返回结果。"),
            QJsonObject {
                { QStringLiteral("command"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要执行的命令，例如: dir, git status, qmake")) },
                { QStringLiteral("working_directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("工作目录（可选）")) }
            },
            QStringList { QStringLiteral("command") })
    };
}
