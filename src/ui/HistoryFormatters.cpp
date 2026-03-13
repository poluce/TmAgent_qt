#include "HistoryFormatters.h"

#include "core/service/ExecutionHistoryModel.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

namespace HistoryFormatters {

QString historyPanelTitle(int count)
{
    return QStringLiteral("执行记录 (共 %1 条)").arg(count);
}

QString historyPanelIntroText()
{
    return QStringLiteral("这里展示的是运行过程中的执行记录、事件和派生摘要，方便先看结论、再查细节；它并不等同于完整原始收发审计。");
}

QString emptyHistoryText()
{
    return QStringLiteral("暂无执行记录。\n\n说明：这里展示的是运行过程中的执行记录与派生摘要，并非完整的原始收发审计日志。");
}

QString emptySelectionText()
{
    return QStringLiteral("请选择一条执行记录查看详情。");
}

QString summaryTabTitle()
{
    return QStringLiteral("执行摘要");
}

QString rawTabTitle()
{
    return QStringLiteral("记录原文");
}

QString rawTabHintText()
{
    return QStringLiteral("说明：这里的“记录原文”会按分层定义拆成展示摘要层 / 事件层 / 交互事实层 / 审计层（未建设），来源于运行期 ioHistory 与事件派生结构；完整协议级审计视图尚未建设。");
}

QString summaryDetailsTitle()
{
    return QStringLiteral("过程与诊断");
}

QString toolLogWindowTitle()
{
    return QStringLiteral("工具执行日志 - 原始事件");
}

QString rawFieldColumnTitle()
{
    return QStringLiteral("字段");
}

QString rawValueColumnTitle()
{
    return QStringLiteral("内容");
}

QString localizedRawFieldLabel(const QString& key)
{
    static const QHash<QString, QString> labels = {
        { QStringLiteral("_meta"), QStringLiteral("说明") },
        { QStringLiteral("schema_version"), QStringLiteral("结构版本") },
        { QStringLiteral("layer_key"), QStringLiteral("层标识") },
        { QStringLiteral("summary_layer"), QStringLiteral("展示摘要层") },
        { QStringLiteral("event_facts_layer"), QStringLiteral("事件层") },
        { QStringLiteral("interaction_facts_layer"), QStringLiteral("交互事实层") },
        { QStringLiteral("audit_layer"), QStringLiteral("审计层") },
        { QStringLiteral("title"), QStringLiteral("标题") },
        { QStringLiteral("source"), QStringLiteral("来源") },
        { QStringLiteral("purpose"), QStringLiteral("用途") },
        { QStringLiteral("stability"), QStringLiteral("稳定性") },
        { QStringLiteral("basic_result"), QStringLiteral("基本结果") },
        { QStringLiteral("kind"), QStringLiteral("类型") },
        { QStringLiteral("status"), QStringLiteral("状态") },
        { QStringLiteral("status_tone"), QStringLiteral("状态色") },
        { QStringLiteral("time_summary"), QStringLiteral("时间摘要") },
        { QStringLiteral("meta_summary"), QStringLiteral("关键信息摘要") },
        { QStringLiteral("started_at_display"), QStringLiteral("开始时间") },
        { QStringLiteral("finished_at_display"), QStringLiteral("完成时间") },
        { QStringLiteral("duration_display"), QStringLiteral("持续时间") },
        { QStringLiteral("input"), QStringLiteral("输入") },
        { QStringLiteral("output"), QStringLiteral("输出") },
        { QStringLiteral("summary"), QStringLiteral("摘要") },
        { QStringLiteral("tools"), QStringLiteral("工具") },
        { QStringLiteral("process"), QStringLiteral("工具过程") },
        { QStringLiteral("errors"), QStringLiteral("错误") },
        { QStringLiteral("name"), QStringLiteral("名称") },
        { QStringLiteral("stage"), QStringLiteral("阶段") },
        { QStringLiteral("input_summary"), QStringLiteral("输入摘要") },
        { QStringLiteral("output_summary"), QStringLiteral("输出摘要") },
        { QStringLiteral("error_summary"), QStringLiteral("错误摘要") },
        { QStringLiteral("message"), QStringLiteral("说明") },
        { QStringLiteral("next_step"), QStringLiteral("下一步") },
        { QStringLiteral("request_id"), QStringLiteral("请求 ID") },
        { QStringLiteral("request_ids"), QStringLiteral("请求 ID 列表") },
        { QStringLiteral("request_recorded_at"), QStringLiteral("请求记录时间") },
        { QStringLiteral("response_recorded_at"), QStringLiteral("响应记录时间") },
        { QStringLiteral("error_recorded_at"), QStringLiteral("错误记录时间") },
        { QStringLiteral("recorded_at"), QStringLiteral("记录时间") },
        { QStringLiteral("trace_id"), QStringLiteral("追踪 ID") },
        { QStringLiteral("turn_id"), QStringLiteral("回合 ID") },
        { QStringLiteral("run_id"), QStringLiteral("运行 ID") },
        { QStringLiteral("client_message_id"), QStringLiteral("客户端消息 ID") },
        { QStringLiteral("agent_id"), QStringLiteral("助手 ID") },
        { QStringLiteral("model"), QStringLiteral("模型") },
        { QStringLiteral("request"), QStringLiteral("请求") },
        { QStringLiteral("response"), QStringLiteral("响应") },
        { QStringLiteral("error"), QStringLiteral("错误") },
        { QStringLiteral("segments"), QStringLiteral("交互片段") },
        { QStringLiteral("event"), QStringLiteral("事件") },
        { QStringLiteral("type"), QStringLiteral("事件类型") },
        { QStringLiteral("session_id"), QStringLiteral("会话 ID") },
        { QStringLiteral("timestamp"), QStringLiteral("时间戳") },
        { QStringLiteral("toolName"), QStringLiteral("工具名称") },
        { QStringLiteral("tool_name"), QStringLiteral("工具名称") },
        { QStringLiteral("toolId"), QStringLiteral("工具调用 ID") },
        { QStringLiteral("status_label"), QStringLiteral("状态标签") },
        { QStringLiteral("success"), QStringLiteral("是否成功") },
        { QStringLiteral("data"), QStringLiteral("数据") },
        { QStringLiteral("rawResult"), QStringLiteral("原始结果") },
        { QStringLiteral("formattedResult"), QStringLiteral("格式化结果") },
        { QStringLiteral("task_preview"), QStringLiteral("任务摘要") },
        { QStringLiteral("role_prompt_preview"), QStringLiteral("角色提示词摘要") },
        { QStringLiteral("parent_agent_id"), QStringLiteral("父助手 ID") },
        { QStringLiteral("child_request_id"), QStringLiteral("子请求 ID") },
        { QStringLiteral("child_trace_id"), QStringLiteral("子追踪 ID") },
        { QStringLiteral("child_agent_id"), QStringLiteral("子助手 ID") },
        { QStringLiteral("child_model"), QStringLiteral("子模型") },
        { QStringLiteral("child_finish_reason"), QStringLiteral("子完成原因") },
        { QStringLiteral("failure_reason"), QStringLiteral("失败原因") },
        { QStringLiteral("durationMs"), QStringLiteral("耗时毫秒") },
        { QStringLiteral("delegate_total"), QStringLiteral("委派总数") },
        { QStringLiteral("delegate_success"), QStringLiteral("委派成功数") },
        { QStringLiteral("delegate_failed"), QStringLiteral("委派失败数") },
        { QStringLiteral("delegate_avg_duration_ms"), QStringLiteral("委派平均耗时毫秒") },
        { QStringLiteral("exchange_count"), QStringLiteral("交互段数") },
        { QStringLiteral("messages"), QStringLiteral("消息列表") },
        { QStringLiteral("role"), QStringLiteral("角色") },
        { QStringLiteral("content"), QStringLiteral("内容") },
        { QStringLiteral("tool_calls"), QStringLiteral("工具调用") },
        { QStringLiteral("tool_call_id"), QStringLiteral("工具调用 ID") },
        { QStringLiteral("function"), QStringLiteral("函数") },
        { QStringLiteral("arguments"), QStringLiteral("参数") },
        { QStringLiteral("finish_reason"), QStringLiteral("完成原因") },
        { QStringLiteral("choices"), QStringLiteral("候选结果") },
        { QStringLiteral("max_tokens"), QStringLiteral("最大输出令牌") },
        { QStringLiteral("temperature"), QStringLiteral("温度") },
        { QStringLiteral("stream"), QStringLiteral("流式返回") },
        { QStringLiteral("not_available"), QStringLiteral("未建设") }
    };

    return labels.value(key, key);
}

QString localizedRawScalarText(const QJsonValue& value)
{
    switch (value.type()) {
    case QJsonValue::Null:
        return QStringLiteral("空值");
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("是") : QStringLiteral("否");
    case QJsonValue::Double:
        return QString::number(value.toDouble());
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Array:
        return QStringLiteral("数组（%1 项）").arg(value.toArray().size());
    case QJsonValue::Object:
        return QStringLiteral("对象（%1 项）").arg(value.toObject().size());
    case QJsonValue::Undefined:
        return QStringLiteral("未定义");
    }
    return QString();
}

EntrySummary summarizeEntry(const QJsonObject& entry, int row)
{
    EntrySummary summary;
    const QVector<ExecutionHistory::Record> records = ExecutionHistory::buildRecords(QJsonArray{ entry });
    if (records.isEmpty())
        return summary;

    const ExecutionHistory::Record& record = records.first();
    summary.listTitle = record.listTitle;
    summary.listTitle.replace(QStringLiteral("#1"), QStringLiteral("#%1").arg(row + 1));
    summary.kindLabel = record.kindLabel;
    summary.statusLabel = record.statusLabel;
    summary.statusTone = record.statusTone;
    summary.inputSummary = record.inputSummary;
    summary.outputSummary = record.outputSummary;
    summary.toolSummary = record.toolSummary;
    summary.metaSummary = record.metaSummary;
    summary.errorSummary = record.errorSummary;
    summary.detailText = record.detailText;
    summary.detailText.replace(QStringLiteral("记录 #1"), QStringLiteral("记录 #%1").arg(row + 1));
    return summary;
}

QString buildTurnListTitle(const QJsonObject& entry, int row)
{
    return summarizeEntry(entry, row).listTitle;
}

QString buildTurnSummaryText(const QJsonObject& entry, int row)
{
    return summarizeEntry(entry, row).detailText;
}

} // namespace HistoryFormatters
