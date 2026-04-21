#include "MemoryToolProvider.h"

#include "EventLogTool.h"
#include "MemoryTool.h"
#include "SessionSearchTool.h"
#include <tmagent/support/ToolSchemaBuilder.h>

#include <QJsonDocument>
#include <QRegularExpression>

namespace {

QList<TmAgent::Tool> buildMemoryToolSchemas()
{
    using namespace TmAgent;
    QList<Tool> tools;
    
    Tool memorySearch;
    memorySearch.name = QStringLiteral("memory_search");
    memorySearch.description = QStringLiteral("搜索助手记忆文档（memory.md / user_view.md / daily memory）。");
    memorySearch.inputSchema = makeToolSchema(
        QStringLiteral("memory_search"),
        QStringLiteral("搜索助手记忆文档（memory.md / user_view.md / daily memory）。"),
        QJsonObject {
            { QStringLiteral("query"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索关键词")) },
            { QStringLiteral("scope"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索范围：self(默认) / all")) },
            { QStringLiteral("agent_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定助手 ID")) },
            { QStringLiteral("include_daily"), makePropertySchema(QStringLiteral("boolean"), QStringLiteral("是否包含 daily 记忆日志（默认 true）")) },
            { QStringLiteral("max_results"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("最多返回命中条数（默认 10，范围 1-100）")) },
            { QStringLiteral("max_snippet_chars"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("每条命中摘要最大长度（默认 180）")) }
        },
        QStringList { QStringLiteral("query") });
    tools.append(memorySearch);
    
    Tool memoryReindex;
    memoryReindex.name = QStringLiteral("memory_reindex");
    memoryReindex.description = QStringLiteral("重建助手记忆检索索引（SQLite FTS 派生索引）。");
    memoryReindex.inputSchema = makeToolSchema(
        QStringLiteral("memory_reindex"),
        QStringLiteral("重建助手记忆检索索引（SQLite FTS 派生索引）。"),
        QJsonObject {
            { QStringLiteral("scope"), makePropertySchema(QStringLiteral("string"), QStringLiteral("重建范围：self(默认) / all")) },
            { QStringLiteral("agent_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定助手 ID")) }
        });
    tools.append(memoryReindex);
    
    Tool memoryWrite;
    memoryWrite.name = QStringLiteral("memory_write");
    memoryWrite.description = QStringLiteral("主动写入当前助手的长期记忆（memory.md）。");
    memoryWrite.inputSchema = makeToolSchema(
        QStringLiteral("memory_write"),
        QStringLiteral("主动写入当前助手的长期记忆（memory.md）。"),
        QJsonObject {
            { QStringLiteral("memory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要写入长期记忆的内容")) },
            { QStringLiteral("reason"), makePropertySchema(QStringLiteral("string"), QStringLiteral("写入原因（可选，用于审计说明）")) }
        },
        QStringList { QStringLiteral("memory") });
    tools.append(memoryWrite);
    
    Tool sessionSearch;
    sessionSearch.name = QStringLiteral("session_search");
    sessionSearch.description = QStringLiteral("检索会话历史（messages.jsonl）。");
    sessionSearch.inputSchema = makeToolSchema(
        QStringLiteral("session_search"),
        QStringLiteral("检索会话历史（messages.jsonl）。"),
        QJsonObject {
            { QStringLiteral("query"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索关键词")) },
            { QStringLiteral("scope"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索范围：self(默认) / all")) },
            { QStringLiteral("agent_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定助手 ID")) },
            { QStringLiteral("session_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定会话 ID，仅在该会话中检索")) },
            { QStringLiteral("include_tool_messages"), makePropertySchema(QStringLiteral("boolean"), QStringLiteral("是否包含 tool_call/tool_result 消息（默认 false）")) },
            { QStringLiteral("max_results"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("最多返回命中条数（默认 20，范围 1-200）")) },
            { QStringLiteral("max_snippet_chars"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("每条命中摘要最大长度（默认 220）")) }
        },
        QStringList { QStringLiteral("query") });
    tools.append(sessionSearch);
    
    Tool eventLog;
    eventLog.name = QStringLiteral("event_log");
    eventLog.description = QStringLiteral("日志查询。通过 action 选择操作: search、sessions、agents。");
    eventLog.inputSchema = makeToolSchema(
        QStringLiteral("event_log"),
        QStringLiteral("日志查询。通过 action 选择操作: search、sessions、agents。"),
        QJsonObject {
            { QStringLiteral("action"), makePropertySchema(QStringLiteral("string"), QStringLiteral("操作: search(默认) / sessions / agents")) }
        });
    tools.append(eventLog);
    
    return tools;
}

bool isOkResult(const QString& raw)
{
    const QString text = raw.trimmed();
    if (text.startsWith(QStringLiteral("错误"))
        || text.startsWith(QStringLiteral("抓取失败"))
        || text.startsWith(QStringLiteral("搜索失败"))) {
        return false;
    }

    static const QRegularExpression kExitCodeRe(
        QStringLiteral("(?:^|\\n)\\s*退出码\\s*:\\s*(-?\\d+)"));
    const QRegularExpressionMatch match = kExitCodeRe.match(text);
    if (match.hasMatch())
        return match.captured(1).toInt() == 0;

    return true;
}

TmAgent::ToolResult wrapResult(const QString& raw, const QString& okSummary, const QString& failSummary)
{
    const bool ok = isOkResult(raw);
    return TmAgent::ToolResult(raw, ok ? okSummary : failSummary, ok);
}

TmAgent::ToolResult wrapSimpleResult(const QString& raw, const QString& okSummary, const QString& failSummary)
{
    return wrapResult(raw, okSummary, failSummary);
}

} // namespace

MemoryToolProvider::MemoryToolProvider(TmAgent::IToolPluginHost* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_tools(buildMemoryToolSchemas())
{
}

QList<TmAgent::Tool> MemoryToolProvider::toolSchemas()
{
    return buildMemoryToolSchemas();
}

QList<TmAgent::Tool> MemoryToolProvider::listTools() const
{
    return m_tools;
}

TmAgent::ToolResult MemoryToolProvider::execute(const TmAgent::ToolCall& call)
{
    QJsonObject input = call.input;
    input.insert(QStringLiteral("_tool_call_id"), call.id);

    if (call.name == QStringLiteral("memory_search")) {
        return wrapSimpleResult(
            MemoryTool::executeSearch(input),
            QStringLiteral("[OK] 记忆检索完成"),
            QStringLiteral("[FAIL] 记忆检索失败"));
    }
    if (call.name == QStringLiteral("memory_reindex")) {
        return wrapSimpleResult(
            MemoryTool::executeRebuild(input),
            QStringLiteral("[OK] 记忆索引重建完成"),
            QStringLiteral("[FAIL] 记忆索引重建失败"));
    }
    if (call.name == QStringLiteral("memory_write")) {
        return MemoryTool::executeWrite(input);
    }
    if (call.name == QStringLiteral("session_search")) {
        return wrapSimpleResult(
            SessionSearchTool::executeSearch(input),
            QStringLiteral("[OK] 会话检索完成"),
            QStringLiteral("[FAIL] 会话检索失败"));
    }
    if (call.name == QStringLiteral("event_log")) {
        const QString raw = EventLogTool::execute(input);
        const QJsonObject parsed = QJsonDocument::fromJson(raw.toUtf8()).object();
        const bool ok = parsed.value(QStringLiteral("status")).toString() == QLatin1String("successful");
        return TmAgent::ToolResult(
            raw,
            ok ? QStringLiteral("[OK] 日志查询完成") : QStringLiteral("[FAIL] 日志查询失败"),
            ok);
    }

    return TmAgent::ToolResult(
        QStringLiteral("错误: 未知的工具 %1").arg(call.name),
        QStringLiteral("执行失败"),
        false);
}
