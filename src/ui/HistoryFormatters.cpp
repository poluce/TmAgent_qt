#include "HistoryFormatters.h"

#include "core/service/ExecutionHistoryModel.h"

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
