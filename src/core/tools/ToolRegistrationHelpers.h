#ifndef TOOLREGISTRATIONHELPERS_H
#define TOOLREGISTRATIONHELPERS_H

#include <QJsonArray>
#include "core/agent/ToolTypes.h"
#include "core/utils/ToolSchemaLoader.h"

namespace ToolRegistrationHelpers {

inline Tool resolveToolSchema(const QString& name, const QString& fallbackDesc) {
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

inline bool isOkResult(const QString& raw) {
    return !raw.startsWith("错误") && !raw.startsWith("抓取失败") && !raw.startsWith("搜索失败");
}

inline ToolResult wrapResult(const QString& raw, const QString& okSummary, const QString& failSummary) {
    bool ok = isOkResult(raw);
    return ToolResult(raw, ok ? okSummary : failSummary, ok);
}

} // namespace ToolRegistrationHelpers

#endif // TOOLREGISTRATIONHELPERS_H
