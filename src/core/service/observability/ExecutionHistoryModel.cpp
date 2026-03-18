#include "ExecutionHistoryModel.h"

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QStringList>
#include <QVariant>

namespace {

QString compactText(const QString& text, int maxChars = 120)
{
    const QString simplified = text.simplified();
    if (simplified.size() <= maxChars)
        return simplified;
    return simplified.left(maxChars) + QStringLiteral("...");
}

QString jsonStringField(const QJsonObject& obj, const QString& key)
{
    return obj.value(key).toString().trimmed();
}

QString firstNonEmpty(std::initializer_list<QString> values)
{
    for (const QString& value : values) {
        if (!value.trimmed().isEmpty())
            return value.trimmed();
    }
    return QString();
}

QString joinNonEmpty(const QStringList& parts, const QString& separator = QStringLiteral(" · "))
{
    QStringList filtered;
    for (const QString& part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty())
            filtered << trimmed;
    }
    return filtered.join(separator);
}

QString extractLastUserMessage(const QJsonObject& request)
{
    const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
    for (int i = messages.size() - 1; i >= 0; --i) {
        const QJsonObject msg = messages.at(i).toObject();
        if (msg.value(QStringLiteral("role")).toString() == QLatin1String("user"))
            return msg.value(QStringLiteral("content")).toString().trimmed();
    }
    return QString();
}

QJsonObject firstChoiceMessage(const QJsonObject& response)
{
    const QJsonArray choices = response.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return QJsonObject();
    return choices.first().toObject().value(QStringLiteral("message")).toObject();
}

QString extractAssistantMessage(const QJsonObject& response)
{
    return firstChoiceMessage(response).value(QStringLiteral("content")).toString().trimmed();
}

QString extractFinishReason(const QJsonObject& response)
{
    const QJsonArray choices = response.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty())
        return QString();
    return choices.first().toObject().value(QStringLiteral("finish_reason")).toString().trimmed();
}

QString eventKindLabel(const QString& eventType)
{
    return eventType.isEmpty() ? QStringLiteral("事件") : QStringLiteral("事件 · %1").arg(eventType);
}

QString toneForInteraction(bool hasError, bool hasResponse, int toolCalls)
{
    if (hasError)
        return QStringLiteral("error");
    if (!hasResponse)
        return QStringLiteral("warning");
    if (toolCalls > 0)
        return QStringLiteral("info");
    return QStringLiteral("success");
}

QString statusForInteraction(bool hasError, bool hasResponse, int toolCalls, const QString& finishReason)
{
    if (hasError)
        return QStringLiteral("失败");
    if (!hasResponse)
        return QStringLiteral("处理中");
    if (toolCalls > 0)
        return QStringLiteral("工具调用");
    if (!finishReason.isEmpty())
        return QStringLiteral("完成");
    return QStringLiteral("处理中");
}

QString formatDateTimeDisplay(const QString& isoText)
{
    if (isoText.trimmed().isEmpty())
        return QString();
    QDateTime dt = QDateTime::fromString(isoText, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(isoText, Qt::ISODate);
    if (!dt.isValid())
        return isoText.trimmed();
    return dt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

qint64 durationMsBetween(const QString& startIso, const QString& endIso)
{
    if (startIso.trimmed().isEmpty() || endIso.trimmed().isEmpty())
        return -1;
    QDateTime start = QDateTime::fromString(startIso, Qt::ISODateWithMs);
    if (!start.isValid())
        start = QDateTime::fromString(startIso, Qt::ISODate);
    QDateTime end = QDateTime::fromString(endIso, Qt::ISODateWithMs);
    if (!end.isValid())
        end = QDateTime::fromString(endIso, Qt::ISODate);
    if (!start.isValid() || !end.isValid())
        return -1;
    return start.msecsTo(end);
}

QString formatDuration(qint64 durationMs)
{
    if (durationMs < 0)
        return QString();
    if (durationMs < 1000)
        return QStringLiteral("%1 ms").arg(durationMs);
    if (durationMs < 60 * 1000)
        return QStringLiteral("%1 s").arg(QString::number(durationMs / 1000.0, 'f', durationMs < 10000 ? 1 : 0));
    const qint64 minutes = durationMs / (60 * 1000);
    const qint64 seconds = (durationMs % (60 * 1000)) / 1000;
    return QStringLiteral("%1 分 %2 秒").arg(minutes).arg(seconds);
}

QString requestMessageCountText(int count)
{
    return count > 0 ? QStringLiteral("消息数 %1").arg(count) : QStringLiteral("消息数未记录");
}

QString outputSummaryText(const QString& assistantMsg, const QString& errorMsg, bool hasResponse, int responseToolCount)
{
    if (!assistantMsg.isEmpty())
        return assistantMsg;
    if (!errorMsg.isEmpty())
        return QStringLiteral("本轮未产出模型正文，因为执行过程中发生了错误。");
    if (!hasResponse)
        return QStringLiteral("模型尚未返回结果，这条记录仍在执行中。");
    if (responseToolCount > 0)
        return QStringLiteral("模型本轮先进入工具调用流程，正文尚未直接返回。");
    return QStringLiteral("本轮没有提取到可展示的模型正文。");
}

QVector<ExecutionHistory::ToolActivity> extractToolCallsFromResponse(const QJsonObject& response)
{
    QVector<ExecutionHistory::ToolActivity> tools;
    const QJsonArray toolCalls = firstChoiceMessage(response).value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue& value : toolCalls) {
        const QJsonObject toolObj = value.toObject();
        const QJsonObject functionObj = toolObj.value(QStringLiteral("function")).toObject();
        ExecutionHistory::ToolActivity tool;
        tool.name = functionObj.value(QStringLiteral("name")).toString().trimmed();
        if (tool.name.isEmpty())
            tool.name = QStringLiteral("(未命名工具)");
        tool.stageLabel = QStringLiteral("模型请求");
        tool.statusLabel = QStringLiteral("待执行");
        tool.statusTone = QStringLiteral("info");
        tool.inputSummary = compactText(functionObj.value(QStringLiteral("arguments")).toString().trimmed(), 180);
        if (tool.inputSummary.isEmpty())
            tool.inputSummary = QStringLiteral("未记录参数摘要。");
        tool.outputSummary = QStringLiteral("这条记录只表明模型请求了工具，具体执行结果请结合后续事件记录查看。");
        tools.append(tool);
    }
    return tools;
}

QVector<ExecutionHistory::ToolActivity> extractToolActivitiesFromEvent(const QJsonObject& eventObj)
{
    QVector<ExecutionHistory::ToolActivity> tools;
    const QString toolName = firstNonEmpty({
        eventObj.value(QStringLiteral("toolName")).toString(),
        eventObj.value(QStringLiteral("tool_name")).toString()
    });
    const QString eventType = eventObj.value(QStringLiteral("type")).toString().trimmed();
    if (toolName.isEmpty() && !eventType.contains(QStringLiteral("tool")))
        return tools;

    ExecutionHistory::ToolActivity tool;
    tool.name = toolName.isEmpty() ? QStringLiteral("(工具事件)") : toolName;
    tool.stageLabel = eventObj.value(QStringLiteral("event")).toString().trimmed();
    if (tool.stageLabel.isEmpty())
        tool.stageLabel = eventType;

    const bool success = eventObj.value(QStringLiteral("success")).toBool(true);
    const QString errorText = firstNonEmpty({
        eventObj.value(QStringLiteral("error")).toString(),
        eventObj.value(QStringLiteral("failure_reason")).toString()
    });
    if (!errorText.isEmpty()) {
        tool.statusLabel = QStringLiteral("失败");
        tool.statusTone = QStringLiteral("error");
    } else if (tool.stageLabel.contains(QStringLiteral("started"))) {
        tool.statusLabel = QStringLiteral("进行中");
        tool.statusTone = QStringLiteral("warning");
    } else if (success) {
        tool.statusLabel = QStringLiteral("完成");
        tool.statusTone = QStringLiteral("success");
    } else {
        tool.statusLabel = QStringLiteral("已记录");
        tool.statusTone = QStringLiteral("neutral");
    }

    tool.inputSummary = firstNonEmpty({
        eventObj.value(QStringLiteral("task_preview")).toString(),
        eventObj.value(QStringLiteral("role_prompt_preview")).toString(),
        eventObj.value(QStringLiteral("arguments")).toString()
    });
    if (tool.inputSummary.isEmpty())
        tool.inputSummary = QStringLiteral("未记录工具输入摘要。");

    tool.outputSummary = firstNonEmpty({
        eventObj.value(QStringLiteral("summary")).toString(),
        eventObj.value(QStringLiteral("formatted_result")).toString(),
        eventObj.value(QStringLiteral("formattedResult")).toString(),
        eventObj.value(QStringLiteral("child_finish_reason")).toString()
    });
    if (tool.outputSummary.isEmpty())
        tool.outputSummary = QStringLiteral("这条事件主要说明工具阶段变化。");
    tool.errorSummary = errorText;
    tools.append(tool);
    return tools;
}

QString toolSummaryText(const QVector<ExecutionHistory::ToolActivity>& tools, int requestToolCount, int responseToolCount)
{
    if (!tools.isEmpty())
        return QStringLiteral("已抽取 %1 条工具过程摘要，可在下方“工具过程”中查看。").arg(tools.size());
    if (requestToolCount <= 0 && responseToolCount <= 0)
        return QStringLiteral("本轮没有配置可用工具，也没有工具调用。");
    if (requestToolCount > 0 && responseToolCount <= 0)
        return QStringLiteral("已向模型提供 %1 个可用工具，本轮未实际调用。").arg(requestToolCount);
    return QStringLiteral("已向模型提供 %1 个可用工具，模型实际调用 %2 次。")
        .arg(requestToolCount)
        .arg(responseToolCount);
}

QString metaSummaryWithTime(const QString& baseMeta, const QString& timeSummary)
{
    if (baseMeta.isEmpty())
        return timeSummary;
    if (timeSummary.isEmpty())
        return baseMeta;
    return QStringLiteral("%1 · %2").arg(baseMeta, timeSummary);
}

QJsonObject buildLayerMeta(const QString& layerKey,
                           const QString& title,
                           const QString& source,
                           const QString& purpose,
                           const QString& stability)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), ExecutionHistory::kSchemaVersion);
    obj.insert(QStringLiteral("layer_key"), layerKey);
    obj.insert(QStringLiteral("title"), title);
    obj.insert(QStringLiteral("source"), source);
    obj.insert(QStringLiteral("purpose"), purpose);
    obj.insert(QStringLiteral("stability"), stability);
    return obj;
}

QJsonObject buildSummaryLayer(const ExecutionHistory::Record& record)
{
    QJsonObject basicResult;
    basicResult.insert(QStringLiteral("kind"), record.kindLabel);
    basicResult.insert(QStringLiteral("status"), record.statusLabel);
    basicResult.insert(QStringLiteral("status_tone"), record.statusTone);
    basicResult.insert(QStringLiteral("time_summary"), record.timeSummary);
    basicResult.insert(QStringLiteral("meta_summary"), record.metaSummary);
    if (!record.startedAtDisplay.isEmpty())
        basicResult.insert(QStringLiteral("started_at_display"), record.startedAtDisplay);
    if (!record.finishedAtDisplay.isEmpty())
        basicResult.insert(QStringLiteral("finished_at_display"), record.finishedAtDisplay);
    if (!record.durationDisplay.isEmpty())
        basicResult.insert(QStringLiteral("duration_display"), record.durationDisplay);

    QJsonObject inputObj;
    inputObj.insert(QStringLiteral("summary"), record.inputSummary);

    QJsonObject outputObj;
    outputObj.insert(QStringLiteral("summary"), record.outputSummary);

    QJsonObject toolObj;
    toolObj.insert(QStringLiteral("summary"), record.toolSummary);

    QJsonObject errorObj;
    errorObj.insert(QStringLiteral("summary"), record.errorSummary);

    QJsonObject obj;
    obj.insert(QStringLiteral("_meta"),
               buildLayerMeta(QStringLiteral("summary_layer"),
                              QStringLiteral("展示摘要层"),
                              QStringLiteral("derived_from_runtime_records"),
                              QStringLiteral("先给用户结论、摘要与诊断信息"),
                              QStringLiteral("ui_facing")));
    obj.insert(QStringLiteral("basic_result"), basicResult);
    obj.insert(QStringLiteral("input"), inputObj);
    obj.insert(QStringLiteral("output"), outputObj);
    obj.insert(QStringLiteral("tools"), toolObj);
    obj.insert(QStringLiteral("errors"), errorObj);

    if (!record.toolActivities.isEmpty()) {
        QJsonArray tools;
        for (const ExecutionHistory::ToolActivity& tool : record.toolActivities) {
            QJsonObject toolObj;
            toolObj.insert(QStringLiteral("name"), tool.name);
            toolObj.insert(QStringLiteral("stage"), tool.stageLabel);
            toolObj.insert(QStringLiteral("status"), tool.statusLabel);
            toolObj.insert(QStringLiteral("status_tone"), tool.statusTone);
            toolObj.insert(QStringLiteral("input_summary"), tool.inputSummary);
            toolObj.insert(QStringLiteral("output_summary"), tool.outputSummary);
            if (!tool.errorSummary.isEmpty())
                toolObj.insert(QStringLiteral("error_summary"), tool.errorSummary);
            tools.append(toolObj);
        }
        QJsonObject toolsLayer = obj.value(QStringLiteral("tools")).toObject();
        toolsLayer.insert(QStringLiteral("process"), tools);
        obj.insert(QStringLiteral("tools"), toolsLayer);
    }

    return obj;
}

QJsonObject buildAuditLayer()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("_meta"),
               buildLayerMeta(QStringLiteral("audit_layer"),
                              QStringLiteral("审计层"),
                              QStringLiteral("not_built"),
                              QStringLiteral("预留未来独立协议级审计视图"),
                              QStringLiteral("placeholder")));
    obj.insert(QStringLiteral("status"), QStringLiteral("not_available"));
    obj.insert(QStringLiteral("message"),
               QStringLiteral("完整协议级审计视图尚未建设；当前仅提供运行期执行记录、事件和派生摘要。"));
    obj.insert(QStringLiteral("next_step"),
               QStringLiteral("如需真实收发审计，请单独建设稳定持久化的协议级审计链路。"));
    return obj;
}

QString interactionGroupKey(const QJsonObject& entry)
{
    const QString turnId = entry.value(QStringLiteral("turn_id")).toString().trimmed();
    if (!turnId.isEmpty())
        return QStringLiteral("turn:%1").arg(turnId);
    const QString traceId = entry.value(QStringLiteral("trace_id")).toString().trimmed();
    if (!traceId.isEmpty())
        return QStringLiteral("trace:%1").arg(traceId);
    const QString requestId = entry.value(QStringLiteral("request_id")).toString().trimmed();
    if (!requestId.isEmpty())
        return QStringLiteral("request:%1").arg(requestId);
    return QString();
}

QJsonObject buildInteractionSegment(const QJsonObject& entry)
{
    QJsonObject segment;
    const QString requestId = entry.value(QStringLiteral("request_id")).toString().trimmed();
    if (!requestId.isEmpty())
        segment.insert(QStringLiteral("request_id"), requestId);
    const QString requestRecordedAt = firstNonEmpty({
        entry.value(QStringLiteral("request_recorded_at")).toString(),
        entry.value(QStringLiteral("recorded_at")).toString()
    });
    const QString responseRecordedAt = firstNonEmpty({
        entry.value(QStringLiteral("response_recorded_at")).toString(),
        entry.value(QStringLiteral("error_recorded_at")).toString()
    });
    if (!requestRecordedAt.isEmpty())
        segment.insert(QStringLiteral("request_recorded_at"), requestRecordedAt);
    if (!responseRecordedAt.isEmpty())
        segment.insert(QStringLiteral("response_recorded_at"), responseRecordedAt);
    const QString traceId = entry.value(QStringLiteral("trace_id")).toString().trimmed();
    const QString turnId = entry.value(QStringLiteral("turn_id")).toString().trimmed();
    const QString runId = entry.value(QStringLiteral("run_id")).toString().trimmed();
    if (!traceId.isEmpty())
        segment.insert(QStringLiteral("trace_id"), traceId);
    if (!turnId.isEmpty())
        segment.insert(QStringLiteral("turn_id"), turnId);
    if (!runId.isEmpty())
        segment.insert(QStringLiteral("run_id"), runId);
    if (entry.contains(QStringLiteral("request")))
        segment.insert(QStringLiteral("request"), entry.value(QStringLiteral("request")));
    if (entry.contains(QStringLiteral("response")))
        segment.insert(QStringLiteral("response"), entry.value(QStringLiteral("response")));
    if (entry.contains(QStringLiteral("error")))
        segment.insert(QStringLiteral("error"), entry.value(QStringLiteral("error")));
    return segment;
}

void appendUniqueString(QJsonArray& array, const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return;
    for (const QJsonValue& item : array) {
        if (item.toString().trimmed() == trimmed)
            return;
    }
    array.append(trimmed);
}

void rebuildInteractionRecordPresentation(ExecutionHistory::Record& record)
{
    const QJsonObject layer = record.interactionFactsLayer;
    const QJsonArray segments = layer.value(QStringLiteral("segments")).toArray();
    if (segments.isEmpty())
        return;

    const QJsonObject firstSegment = segments.first().toObject();
    const QJsonObject lastSegment = segments.last().toObject();
    const QJsonObject firstRequest = firstSegment.value(QStringLiteral("request")).toObject();
    const QJsonObject lastRequest = lastSegment.value(QStringLiteral("request")).toObject();
    const QJsonObject lastResponse = lastSegment.value(QStringLiteral("response")).toObject();
    const QJsonObject lastError = lastSegment.value(QStringLiteral("error")).toObject();

    const QString model = firstNonEmpty({
        layer.value(QStringLiteral("model")).toString(),
        firstRequest.value(QStringLiteral("model")).toString(),
        lastRequest.value(QStringLiteral("model")).toString()
    });
    const QString finishReason = extractFinishReason(lastResponse);
    const int requestToolCount = firstRequest.value(QStringLiteral("tools")).toArray().size();
    const int totalToolCallCount = record.toolActivities.size();
    const QString userMsg = extractLastUserMessage(firstRequest);
    const QString assistantMsg = extractAssistantMessage(lastResponse);
    const QString errorMsg = lastError.value(QStringLiteral("message")).toString().trimmed();
    const bool hasResponse = lastSegment.contains(QStringLiteral("response"));

    const bool resolvedToolTurn = hasResponse
        && totalToolCallCount > 0
        && segments.size() > 1
        && finishReason != QLatin1String("tool_calls");
    record.kindLabel = QStringLiteral("模型回合");
    record.statusLabel = resolvedToolTurn
        ? QStringLiteral("完成")
        : statusForInteraction(!errorMsg.isEmpty(), hasResponse, totalToolCallCount, finishReason);
    record.statusTone = resolvedToolTurn
        ? QStringLiteral("success")
        : toneForInteraction(!errorMsg.isEmpty(), hasResponse, totalToolCallCount);
    record.hasError = !errorMsg.isEmpty();
    record.hasToolCalls = totalToolCallCount > 0;
    record.isActive = !record.hasError && !hasResponse;
    if (!userMsg.isEmpty())
        record.inputSummary = userMsg;
    else if (record.inputSummary.trimmed().isEmpty())
        record.inputSummary = QStringLiteral("本轮没有提取到直接用户输入，请结合原文记录确认。");
    record.outputSummary = outputSummaryText(assistantMsg, errorMsg, hasResponse, totalToolCallCount);

    const QString startedAtIso = firstSegment.value(QStringLiteral("request_recorded_at")).toString().trimmed();
    const QString finishedAtIso = firstNonEmpty({
        lastSegment.value(QStringLiteral("response_recorded_at")).toString(),
        lastSegment.value(QStringLiteral("error_recorded_at")).toString()
    });
    record.startedAtDisplay = formatDateTimeDisplay(startedAtIso);
    record.finishedAtDisplay = formatDateTimeDisplay(finishedAtIso);
    record.durationDisplay = formatDuration(durationMsBetween(startedAtIso, finishedAtIso));
    record.timeSummary = joinNonEmpty({
        record.startedAtDisplay.isEmpty() ? QString() : QStringLiteral("开始 %1").arg(record.startedAtDisplay),
        record.finishedAtDisplay.isEmpty() ? QString() : QStringLiteral("结束 %1").arg(record.finishedAtDisplay),
        record.durationDisplay.isEmpty() ? QString() : QStringLiteral("耗时 %1").arg(record.durationDisplay)
    });

    const QJsonArray requestIds = layer.value(QStringLiteral("request_ids")).toArray();
    QString requestIdMeta;
    if (requestIds.size() == 1)
        requestIdMeta = QStringLiteral("request_id=%1").arg(requestIds.first().toString());
    else if (requestIds.size() > 1)
        requestIdMeta = QStringLiteral("request_ids=%1").arg(requestIds.size());
    const QString traceId = layer.value(QStringLiteral("trace_id")).toString().trimmed();
    const QString turnId = layer.value(QStringLiteral("turn_id")).toString().trimmed();
    const QString runId = layer.value(QStringLiteral("run_id")).toString().trimmed();
    const QString exchangeCountText = QStringLiteral("exchange_count=%1").arg(segments.size());
    record.metaSummary = metaSummaryWithTime(
        joinNonEmpty({
            requestIdMeta,
            traceId.isEmpty() ? QString() : QStringLiteral("trace_id=%1").arg(traceId),
            turnId.isEmpty() ? QString() : QStringLiteral("turn_id=%1").arg(turnId),
            runId.isEmpty() ? QString() : QStringLiteral("run_id=%1").arg(runId),
            model.isEmpty() ? QString() : QStringLiteral("model=%1").arg(model),
            finishReason.isEmpty() ? QString() : QStringLiteral("finish_reason=%1").arg(finishReason),
            requestMessageCountText(firstRequest.value(QStringLiteral("messages")).toArray().size()),
            exchangeCountText
        }),
        record.timeSummary);

    if (!record.toolActivities.isEmpty()) {
        record.toolSummary = QStringLiteral("本轮经历 %1 段模型交互，已抽取 %2 条工具过程摘要。")
                                 .arg(segments.size())
                                 .arg(record.toolActivities.size());
    } else {
        record.toolSummary = toolSummaryText(record.toolActivities, requestToolCount, totalToolCallCount);
    }
    record.errorSummary = !errorMsg.isEmpty()
        ? errorMsg
        : (!hasResponse
               ? QStringLiteral("当前只记录到了请求，还没有完整响应。")
               : (!finishReason.isEmpty()
                      ? QStringLiteral("模型已完成本轮返回，finish_reason=%1。").arg(finishReason)
                      : QStringLiteral("这条记录没有明确错误，可结合过程与原文继续排查。")));

    QString detailText;
    detailText += QStringLiteral("记录 #%1\n").arg(record.rawIndex + 1);
    detailText += QStringLiteral("类型：%1\n").arg(record.kindLabel);
    detailText += QStringLiteral("状态：%1\n").arg(record.statusLabel);
    if (!requestIdMeta.isEmpty())
        detailText += QStringLiteral("%1\n").arg(requestIdMeta);
    if (!turnId.isEmpty())
        detailText += QStringLiteral("turn_id：%1\n").arg(turnId);
    if (!runId.isEmpty())
        detailText += QStringLiteral("run_id：%1\n").arg(runId);
    if (!model.isEmpty())
        detailText += QStringLiteral("模型：%1\n").arg(model);
    if (!record.startedAtDisplay.isEmpty())
        detailText += QStringLiteral("开始时间：%1\n").arg(record.startedAtDisplay);
    if (!record.finishedAtDisplay.isEmpty())
        detailText += QStringLiteral("完成时间：%1\n").arg(record.finishedAtDisplay);
    if (!record.durationDisplay.isEmpty())
        detailText += QStringLiteral("持续时间：%1\n").arg(record.durationDisplay);
    detailText += QStringLiteral("交互段数：%1\n").arg(segments.size());
    detailText += QStringLiteral("可用工具：%1\n").arg(requestToolCount);
    detailText += QStringLiteral("工具调用：%1\n").arg(totalToolCallCount);
    if (!errorMsg.isEmpty())
        detailText += QStringLiteral("错误：%1\n").arg(errorMsg);
    else if (!finishReason.isEmpty())
        detailText += QStringLiteral("完成原因：%1\n").arg(finishReason);

    detailText += QStringLiteral("\n输入摘要\n%1\n").arg(record.inputSummary);
    detailText += QStringLiteral("\n输出摘要\n%1\n").arg(record.outputSummary);
    detailText += QStringLiteral("\n工具过程\n%1\n").arg(record.toolSummary);
    detailText += QStringLiteral("\n过程概览\n");
    detailText += QStringLiteral("1) 本轮共经历 %1 段模型交互\n").arg(segments.size());
    detailText += QStringLiteral("2) 首段请求携带了用户输入与可用工具配置\n");
    if (totalToolCallCount > 0)
        detailText += QStringLiteral("3) 中间发生了 %1 次工具调用，最终结果已合并回本轮记录\n").arg(totalToolCallCount);
    if (record.hasError)
        detailText += QStringLiteral("4) 本轮在执行过程中报错\n");
    else if (!hasResponse)
        detailText += QStringLiteral("4) 本轮还在等待模型返回\n");
    else
        detailText += QStringLiteral("4) 本轮已生成最终结果\n");
    detailText += QStringLiteral("\n提示：原文页展示的是运行期记录与派生结构，适合排查，不等同于完整原始传输审计。\n");
    record.detailText = detailText.trimmed();

    QString listTail = compactText(record.inputSummary.isEmpty() ? record.outputSummary : record.inputSummary, 28);
    if (segments.size() > 1)
        listTail += QStringLiteral(" · %1 段").arg(segments.size());
    if (totalToolCallCount > 0)
        listTail += QStringLiteral(" · %1 次工具").arg(totalToolCallCount);
    record.listTitle = listTail.isEmpty()
        ? QStringLiteral("#%1 [%2]").arg(record.rawIndex + 1).arg(record.statusLabel)
        : QStringLiteral("#%1 [%2] %3").arg(record.rawIndex + 1).arg(record.statusLabel, listTail);

    record.summaryLayer = buildSummaryLayer(record);
}

void mergeInteractionEntryIntoRecord(ExecutionHistory::Record& record, const QJsonObject& entry)
{
    QJsonObject layer = record.interactionFactsLayer;
    QJsonArray segments = layer.value(QStringLiteral("segments")).toArray();
    segments.append(buildInteractionSegment(entry));
    layer.insert(QStringLiteral("segments"), segments);

    const QString requestId = entry.value(QStringLiteral("request_id")).toString().trimmed();
    QJsonArray requestIds = layer.value(QStringLiteral("request_ids")).toArray();
    appendUniqueString(requestIds, requestId);
    layer.insert(QStringLiteral("request_ids"), requestIds);

    const QString traceId = entry.value(QStringLiteral("trace_id")).toString().trimmed();
    const QString turnId = entry.value(QStringLiteral("turn_id")).toString().trimmed();
    const QString runId = entry.value(QStringLiteral("run_id")).toString().trimmed();
    if (!traceId.isEmpty())
        layer.insert(QStringLiteral("trace_id"), traceId);
    if (!turnId.isEmpty())
        layer.insert(QStringLiteral("turn_id"), turnId);
    if (!runId.isEmpty())
        layer.insert(QStringLiteral("run_id"), runId);

    const QString model = firstNonEmpty({
        entry.value(QStringLiteral("model_id")).toString(),
        entry.value(QStringLiteral("request")).toObject().value(QStringLiteral("model")).toString(),
        layer.value(QStringLiteral("model")).toString()
    });
    if (!model.isEmpty())
        layer.insert(QStringLiteral("model"), model);

    record.interactionFactsLayer = layer;
    record.rawEntry = layer;

    const QVector<ExecutionHistory::ToolActivity> newToolActivities =
        extractToolCallsFromResponse(entry.value(QStringLiteral("response")).toObject());
    for (const ExecutionHistory::ToolActivity& tool : newToolActivities)
        record.toolActivities.append(tool);

    rebuildInteractionRecordPresentation(record);
}

ExecutionHistory::Record buildInteractionRecord(const QJsonObject& entry, int row)
{
    ExecutionHistory::Record record;
    record.rawIndex = row;
    record.recordId = jsonStringField(entry, QStringLiteral("request_id"));
    QJsonObject factsLayer;
    factsLayer.insert(QStringLiteral("_meta"),
                      buildLayerMeta(QStringLiteral("interaction_facts_layer"),
                                     QStringLiteral("交互事实层"),
                                     QStringLiteral("runtime_io_history"),
                                     QStringLiteral("保存运行期 request/response/error 等交互事实"),
                                     QStringLiteral("runtime_facts")));
    const QString traceId = entry.value(QStringLiteral("trace_id")).toString().trimmed();
    const QString turnId = entry.value(QStringLiteral("turn_id")).toString().trimmed();
    const QString runId = entry.value(QStringLiteral("run_id")).toString().trimmed();
    const QString model = firstNonEmpty({
        entry.value(QStringLiteral("model_id")).toString(),
        entry.value(QStringLiteral("request")).toObject().value(QStringLiteral("model")).toString()
    });
    if (!record.recordId.isEmpty())
        factsLayer.insert(QStringLiteral("request_id"), record.recordId);
    if (!traceId.isEmpty())
        factsLayer.insert(QStringLiteral("trace_id"), traceId);
    if (!turnId.isEmpty())
        factsLayer.insert(QStringLiteral("turn_id"), turnId);
    if (!runId.isEmpty())
        factsLayer.insert(QStringLiteral("run_id"), runId);
    if (!model.isEmpty())
        factsLayer.insert(QStringLiteral("model"), model);
    QJsonArray requestIds;
    appendUniqueString(requestIds, record.recordId);
    factsLayer.insert(QStringLiteral("request_ids"), requestIds);
    QJsonArray segments;
    segments.append(buildInteractionSegment(entry));
    factsLayer.insert(QStringLiteral("segments"), segments);
    record.interactionFactsLayer = factsLayer;
    record.auditLayer = buildAuditLayer();
    record.rawEntry = factsLayer;
    record.toolActivities = extractToolCallsFromResponse(entry.value(QStringLiteral("response")).toObject());
    rebuildInteractionRecordPresentation(record);
    return record;
}

ExecutionHistory::Record buildEventRecord(const QJsonObject& entry, int row)
{
    ExecutionHistory::Record record;
    record.rawIndex = row;
    record.rawEntry = entry;
    record.isEvent = true;
    record.recordId = jsonStringField(entry, QStringLiteral("request_id"));

    const QJsonObject eventObj = entry.value(QStringLiteral("event")).toObject();
    const QString eventType = eventObj.value(QStringLiteral("type")).toString().trimmed();
    const QString timestampIso = firstNonEmpty({
        entry.value(QStringLiteral("recorded_at")).toString(),
        eventObj.value(QStringLiteral("timestamp")).toString()
    });
    const QString traceId = eventObj.value(QStringLiteral("trace_id")).toString().trimmed();
    const QString turnId = eventObj.value(QStringLiteral("turn_id")).toString().trimmed();
    const QString runId = eventObj.value(QStringLiteral("run_id")).toString().trimmed();
    const QString errorMsg = eventObj.value(QStringLiteral("error")).toString().trimmed();

    record.kindLabel = eventKindLabel(eventType);
    record.statusLabel = errorMsg.isEmpty() ? QStringLiteral("已记录") : QStringLiteral("失败");
    record.statusTone = errorMsg.isEmpty() ? QStringLiteral("neutral") : QStringLiteral("error");
    record.hasError = !errorMsg.isEmpty();
    record.inputSummary = QStringLiteral("这是一条运行事件，没有直接用户输入。");
    record.outputSummary = firstNonEmpty({
        eventObj.value(QStringLiteral("summary")).toString(),
        errorMsg,
        QStringLiteral("这是一条运行事件记录，用于说明某个阶段发生了什么。")
    });
    record.toolActivities = extractToolActivitiesFromEvent(eventObj);
    record.hasToolCalls = !record.toolActivities.isEmpty();
    record.toolSummary = record.toolActivities.isEmpty()
        ? QStringLiteral("这条事件主要记录阶段变化，没有单独工具过程。")
        : QStringLiteral("已从事件中提取 %1 条工具过程摘要。").arg(record.toolActivities.size());
    record.errorSummary = errorMsg.isEmpty()
        ? QStringLiteral("事件本身没有报错；它用于补充解释本轮发生了什么。")
        : errorMsg;
    record.startedAtDisplay = formatDateTimeDisplay(timestampIso);
    record.timeSummary = record.startedAtDisplay.isEmpty()
        ? QStringLiteral("时间未记录")
        : QStringLiteral("记录时间 %1").arg(record.startedAtDisplay);
    record.metaSummary = joinNonEmpty({
        record.recordId.isEmpty() ? QString() : QStringLiteral("request_id=%1").arg(record.recordId),
        traceId.isEmpty() ? QString() : QStringLiteral("trace_id=%1").arg(traceId),
        turnId.isEmpty() ? QString() : QStringLiteral("turn_id=%1").arg(turnId),
        runId.isEmpty() ? QString() : QStringLiteral("run_id=%1").arg(runId),
        record.timeSummary
    });

    QString detailText;
    detailText += QStringLiteral("记录 #%1\n").arg(row + 1);
    detailText += QStringLiteral("类型：%1\n").arg(record.kindLabel);
    detailText += QStringLiteral("状态：%1\n").arg(record.statusLabel);
    if (!record.recordId.isEmpty())
        detailText += QStringLiteral("请求 ID：%1\n").arg(record.recordId);
    if (!record.startedAtDisplay.isEmpty())
        detailText += QStringLiteral("记录时间：%1\n").arg(record.startedAtDisplay);
    detailText += QStringLiteral("\n事件摘要\n");
    for (auto it = eventObj.constBegin(); it != eventObj.constEnd(); ++it) {
        if (it.key() == QLatin1String("type") || it.key() == QLatin1String("error"))
            continue;
        QString valueText;
        if (it.value().isObject())
            valueText = QString::fromUtf8(QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact));
        else if (it.value().isArray())
            valueText = QString::fromUtf8(QJsonDocument(it.value().toArray()).toJson(QJsonDocument::Compact));
        else
            valueText = it.value().toVariant().toString();
        detailText += QStringLiteral("- %1：%2\n").arg(it.key(), compactText(valueText, 220));
    }
    detailText += QStringLiteral("\n提示：原文页展示的是运行期记录，便于核对，不等同于完整原始传输审计。\n");
    record.detailText = detailText.trimmed();
    record.listTitle = QStringLiteral("#%1 [%2] %3")
                           .arg(row + 1)
                           .arg(record.kindLabel)
                           .arg(compactText(record.outputSummary, 24));

    record.summaryLayer = buildSummaryLayer(record);

    QJsonObject eventFacts;
    eventFacts.insert(QStringLiteral("_meta"),
                      buildLayerMeta(QStringLiteral("event_facts_layer"),
                                     QStringLiteral("事件层"),
                                     QStringLiteral("runtime_events"),
                                     QStringLiteral("保存工具事件、状态变化与流程事件"),
                                     QStringLiteral("runtime_facts")));
    if (!record.recordId.isEmpty())
        eventFacts.insert(QStringLiteral("request_id"), record.recordId);
    eventFacts.insert(QStringLiteral("event"), eventObj);
    record.eventFactsLayer = eventFacts;
    record.auditLayer = buildAuditLayer();
    return record;
}

} // namespace

namespace ExecutionHistory {

QString filterModeText(FilterMode mode)
{
    switch (mode) {
    case FilterMode::All:
        return QStringLiteral("全部记录");
    case FilterMode::FailuresOnly:
        return QStringLiteral("仅失败");
    case FilterMode::ToolCallsOnly:
        return QStringLiteral("仅工具调用");
    case FilterMode::EventsOnly:
        return QStringLiteral("仅事件");
    case FilterMode::ActiveOnly:
        return QStringLiteral("仅处理中");
    }
    return QStringLiteral("全部记录");
}

QJsonObject schemaDescriptor()
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), kSchemaVersion);
    obj.insert(QStringLiteral("purpose"),
               QStringLiteral("定义执行记录的分层结构：展示摘要层、事件层、交互事实层和审计层（未建设），避免把混合运行期记录误解为真实审计日志。"));

    QJsonArray layers;
    layers.append(buildLayerMeta(QStringLiteral("summary_layer"),
                                 QStringLiteral("展示摘要层"),
                                 QStringLiteral("derived_from_runtime_records"),
                                 QStringLiteral("面向用户快速阅读的结论、摘要和诊断"),
                                 QStringLiteral("ui_facing")));
    layers.append(buildLayerMeta(QStringLiteral("event_facts_layer"),
                                 QStringLiteral("事件层"),
                                 QStringLiteral("runtime_events"),
                                 QStringLiteral("工具调用、状态变化、流程事件等运行期事实"),
                                 QStringLiteral("runtime_facts")));
    layers.append(buildLayerMeta(QStringLiteral("interaction_facts_layer"),
                                 QStringLiteral("交互事实层"),
                                 QStringLiteral("runtime_io_history"),
                                 QStringLiteral("request/response/error 等运行期交互事实"),
                                 QStringLiteral("runtime_facts")));
    layers.append(buildLayerMeta(QStringLiteral("audit_layer"),
                                 QStringLiteral("审计层"),
                                 QStringLiteral("not_built"),
                                 QStringLiteral("预留未来独立协议级审计视图"),
                                 QStringLiteral("placeholder")));
    obj.insert(QStringLiteral("layers"), layers);
    return obj;
}

QVector<Record> buildRecords(const QJsonArray& history)
{
    QVector<Record> records;
    records.reserve(history.size());
    QHash<QString, int> groupedInteractionIndexByKey;
    for (int i = 0; i < history.size(); ++i) {
        const QJsonObject entry = history.at(i).toObject();
        if (entry.contains(QStringLiteral("event"))) {
            records.append(buildEventRecord(entry, i));
            continue;
        }

        const QString groupKey = interactionGroupKey(entry);
        if (!groupKey.isEmpty() && groupedInteractionIndexByKey.contains(groupKey)) {
            mergeInteractionEntryIntoRecord(records[groupedInteractionIndexByKey.value(groupKey)], entry);
            continue;
        }

        records.append(buildInteractionRecord(entry, i));
        if (!groupKey.isEmpty())
            groupedInteractionIndexByKey.insert(groupKey, records.size() - 1);
    }
    return records;
}

QVector<int> filterRecordIndexes(const QVector<Record>& records, FilterMode mode, int recentLimit)
{
    QVector<int> indexes;
    indexes.reserve(records.size());
    for (int i = 0; i < records.size(); ++i) {
        const Record& record = records.at(i);
        bool include = true;
        switch (mode) {
        case FilterMode::All:
            include = true;
            break;
        case FilterMode::FailuresOnly:
            include = record.hasError;
            break;
        case FilterMode::ToolCallsOnly:
            include = record.hasToolCalls;
            break;
        case FilterMode::EventsOnly:
            include = record.isEvent;
            break;
        case FilterMode::ActiveOnly:
            include = record.isActive;
            break;
        }
        if (include)
            indexes.append(i);
    }

    if (recentLimit > 0 && indexes.size() > recentLimit)
        indexes = indexes.mid(indexes.size() - recentLimit);
    return indexes;
}

} // namespace ExecutionHistory
