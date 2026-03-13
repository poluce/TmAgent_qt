#include <QtTest>

#include "HistoryFormattersTest.h"
#include "core/service/ExecutionHistoryModel.h"
#include "ui/HistoryFormatters.h"

void HistoryFormattersTest::historyPanelTitle_usesExecutionRecordWording()
{
    QCOMPARE(HistoryFormatters::historyPanelTitle(0), QStringLiteral("执行记录 (共 0 条)"));
    QCOMPARE(HistoryFormatters::summaryTabTitle(), QStringLiteral("执行摘要"));
    QCOMPARE(HistoryFormatters::rawTabTitle(), QStringLiteral("记录原文"));
    QCOMPARE(HistoryFormatters::toolLogWindowTitle(), QStringLiteral("工具执行日志 - 原始事件"));
}

void HistoryFormattersTest::emptyTexts_explainDerivedRuntimeSemantics()
{
    QVERIFY(HistoryFormatters::emptyHistoryText().contains(QStringLiteral("暂无执行记录")));
    QVERIFY(HistoryFormatters::emptyHistoryText().contains(QStringLiteral("并非完整的原始收发审计日志")));
    QCOMPARE(HistoryFormatters::emptySelectionText(), QStringLiteral("请选择一条执行记录查看详情。"));
}

void HistoryFormattersTest::helperTexts_clarifyRuntimeVsAuditBoundary()
{
    QVERIFY(HistoryFormatters::historyPanelIntroText().contains(QStringLiteral("派生摘要")));
    QVERIFY(HistoryFormatters::historyPanelIntroText().contains(QStringLiteral("不等同于完整原始收发审计")));
    QVERIFY(HistoryFormatters::rawTabHintText().contains(QStringLiteral("ioHistory")));
    QVERIFY(HistoryFormatters::rawTabHintText().contains(QStringLiteral("审计层")));
    QCOMPARE(HistoryFormatters::summaryDetailsTitle(), QStringLiteral("过程与诊断"));
}

void HistoryFormattersTest::rawFieldLabels_areLocalizedForUserFacingFields()
{
    QCOMPARE(HistoryFormatters::rawFieldColumnTitle(), QStringLiteral("字段"));
    QCOMPARE(HistoryFormatters::rawValueColumnTitle(), QStringLiteral("内容"));
    QCOMPARE(HistoryFormatters::localizedRawFieldLabel(QStringLiteral("request_id")), QStringLiteral("请求 ID"));
    QCOMPARE(HistoryFormatters::localizedRawFieldLabel(QStringLiteral("response")), QStringLiteral("响应"));
    QCOMPARE(HistoryFormatters::localizedRawFieldLabel(QStringLiteral("interaction_facts_layer")), QStringLiteral("交互事实层"));
    QCOMPARE(HistoryFormatters::localizedRawScalarText(QJsonValue()), QStringLiteral("空值"));
    QCOMPARE(HistoryFormatters::localizedRawScalarText(QJsonValue(true)), QStringLiteral("是"));
}

void HistoryFormattersTest::buildTurnListTitle_prefersClearUserFacingStatus()
{
    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), QStringLiteral("帮我看看这个请求是不是失败了"));

    QJsonArray messages;
    messages.append(userMessage);

    QJsonObject request;
    request.insert(QStringLiteral("messages"), messages);

    QJsonObject choiceMessage;
    choiceMessage.insert(QStringLiteral("content"), QStringLiteral("我已经检查过了"));

    QJsonObject choice;
    choice.insert(QStringLiteral("message"), choiceMessage);
    choice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));

    QJsonArray choices;
    choices.append(choice);

    QJsonObject response;
    response.insert(QStringLiteral("choices"), choices);

    QJsonObject entry;
    entry.insert(QStringLiteral("request"), request);
    entry.insert(QStringLiteral("response"), response);

    const QString title = HistoryFormatters::buildTurnListTitle(entry, 0);
    QVERIFY(title.contains(QStringLiteral("#1")));
    QVERIFY(title.contains(QStringLiteral("完成")));
    QVERIFY(title.contains(QStringLiteral("帮我看看这个请求是不是失败了")));
}

void HistoryFormattersTest::summarizeEntry_buildsFixedSummaryFields()
{
    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), QStringLiteral("请帮我检查今天的执行情况"));

    QJsonArray messages;
    messages.append(userMessage);

    QJsonObject toolCall;
    toolCall.insert(QStringLiteral("id"), QStringLiteral("tool-1"));
    toolCall.insert(QStringLiteral("type"), QStringLiteral("function"));

    QJsonArray toolCalls;
    toolCalls.append(toolCall);

    QJsonObject request;
    request.insert(QStringLiteral("messages"), messages);
    request.insert(QStringLiteral("model"), QStringLiteral("claude-sonnet"));
    request.insert(QStringLiteral("tools"), toolCalls);

    QJsonObject assistantMessage;
    assistantMessage.insert(QStringLiteral("content"), QStringLiteral("我已经整理好了执行结果"));
    assistantMessage.insert(QStringLiteral("tool_calls"), toolCalls);

    QJsonObject choice;
    choice.insert(QStringLiteral("message"), assistantMessage);
    choice.insert(QStringLiteral("finish_reason"), QStringLiteral("tool_calls"));

    QJsonArray choices;
    choices.append(choice);

    QJsonObject response;
    response.insert(QStringLiteral("choices"), choices);

    QJsonObject entry;
    entry.insert(QStringLiteral("request_id"), QStringLiteral("req-42"));
    entry.insert(QStringLiteral("request"), request);
    entry.insert(QStringLiteral("response"), response);

    const HistoryFormatters::EntrySummary summary = HistoryFormatters::summarizeEntry(entry, 0);
    QCOMPARE(summary.kindLabel, QStringLiteral("模型回合"));
    QCOMPARE(summary.statusLabel, QStringLiteral("工具调用"));
    QVERIFY(summary.inputSummary.contains(QStringLiteral("请帮我检查今天的执行情况")));
    QVERIFY(summary.outputSummary.contains(QStringLiteral("我已经整理好了执行结果")));
    QVERIFY(summary.toolSummary.contains(QStringLiteral("1 条工具过程摘要")));
    QVERIFY(summary.metaSummary.contains(QStringLiteral("request_id=req-42")));
    QVERIFY(summary.detailText.contains(QStringLiteral("过程概览")));
}

void HistoryFormattersTest::executionHistoryModel_extractsTimeAndFilters()
{
    QJsonObject responseMessage;
    responseMessage.insert(QStringLiteral("content"), QStringLiteral("执行完成"));

    QJsonObject responseChoice;
    responseChoice.insert(QStringLiteral("message"), responseMessage);
    responseChoice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));

    QJsonArray responseChoices;
    responseChoices.append(responseChoice);

    QJsonObject request;
    request.insert(QStringLiteral("model"), QStringLiteral("claude-sonnet"));
    request.insert(QStringLiteral("messages"), QJsonArray { QJsonObject {
        { QStringLiteral("role"), QStringLiteral("user") },
        { QStringLiteral("content"), QStringLiteral("帮我收尾这轮任务") }
    } });

    QJsonObject interaction;
    interaction.insert(QStringLiteral("request_id"), QStringLiteral("req-run-1"));
    interaction.insert(QStringLiteral("request_recorded_at"), QStringLiteral("2026-03-13T10:00:00.000Z"));
    interaction.insert(QStringLiteral("response_recorded_at"), QStringLiteral("2026-03-13T10:00:01.500Z"));
    interaction.insert(QStringLiteral("request"), request);
    interaction.insert(QStringLiteral("response"), QJsonObject { { QStringLiteral("choices"), responseChoices } });

    QJsonObject eventObj;
    eventObj.insert(QStringLiteral("type"), QStringLiteral("delegate.tool_completed"));
    eventObj.insert(QStringLiteral("timestamp"), QStringLiteral("2026-03-13T10:00:02.000Z"));
    eventObj.insert(QStringLiteral("toolName"), QStringLiteral("delegate_task"));
    eventObj.insert(QStringLiteral("summary"), QStringLiteral("后台子代理已返回结果"));

    QJsonObject eventEntry;
    eventEntry.insert(QStringLiteral("request_id"), QStringLiteral("event:delegate"));
    eventEntry.insert(QStringLiteral("event"), eventObj);

    QJsonArray history;
    history.append(interaction);
    history.append(eventEntry);

    const QVector<ExecutionHistory::Record> records = ExecutionHistory::buildRecords(history);
    QCOMPARE(records.size(), 2);
    QVERIFY(records.at(0).timeSummary.contains(QStringLiteral("耗时")));
    QVERIFY(records.at(0).durationDisplay.contains(QStringLiteral("1.5")));
    QVERIFY(records.at(0).summaryLayer.contains(QStringLiteral("basic_result")));
    QVERIFY(records.at(0).summaryLayer.value(QStringLiteral("input")).toObject().contains(QStringLiteral("summary")));
    QVERIFY(records.at(0).interactionFactsLayer.contains(QStringLiteral("segments")));
    QVERIFY(records.at(0).eventFactsLayer.isEmpty());
    QVERIFY(records.at(0).auditLayer.value(QStringLiteral("status")).toString() == QStringLiteral("not_available"));
    QVERIFY(records.at(1).isEvent);
    QVERIFY(records.at(1).hasToolCalls);
    QVERIFY(records.at(1).eventFactsLayer.contains(QStringLiteral("event")));
    QVERIFY(records.at(1).interactionFactsLayer.isEmpty());

    const QVector<int> onlyEvents = ExecutionHistory::filterRecordIndexes(
        records,
        ExecutionHistory::FilterMode::EventsOnly,
        0);
    QCOMPARE(onlyEvents.size(), 1);
    QCOMPARE(onlyEvents.first(), 1);

    const QVector<int> recentOne = ExecutionHistory::filterRecordIndexes(
        records,
        ExecutionHistory::FilterMode::All,
        1);
    QCOMPARE(recentOne.size(), 1);
    QCOMPARE(recentOne.first(), 1);
}

void HistoryFormattersTest::executionHistoryModel_exposesSchemaDescriptor()
{
    const QJsonObject descriptor = ExecutionHistory::schemaDescriptor();
    QCOMPARE(descriptor.value(QStringLiteral("schema_version")).toInt(), ExecutionHistory::kSchemaVersion);
    QVERIFY(descriptor.value(QStringLiteral("purpose")).toString().contains(QStringLiteral("分层结构")));

    const QJsonArray layers = descriptor.value(QStringLiteral("layers")).toArray();
    QCOMPARE(layers.size(), 4);
    QCOMPARE(layers.at(0).toObject().value(QStringLiteral("layer_key")).toString(), QStringLiteral("summary_layer"));
    QCOMPARE(layers.at(1).toObject().value(QStringLiteral("layer_key")).toString(), QStringLiteral("event_facts_layer"));
    QCOMPARE(layers.at(2).toObject().value(QStringLiteral("layer_key")).toString(), QStringLiteral("interaction_facts_layer"));
    QCOMPARE(layers.at(3).toObject().value(QStringLiteral("layer_key")).toString(), QStringLiteral("audit_layer"));
}

void HistoryFormattersTest::executionHistoryModel_mergesToolFollowupRequestsByTurn()
{
    const QString turnId = QStringLiteral("turn-123");
    const QString traceId = QStringLiteral("trace-123");
    const QString runId = QStringLiteral("run-123");

    QJsonObject firstRequest;
    firstRequest.insert(QStringLiteral("model"), QStringLiteral("claude-sonnet"));
    firstRequest.insert(QStringLiteral("messages"), QJsonArray {
        QJsonObject {
            { QStringLiteral("role"), QStringLiteral("user") },
            { QStringLiteral("content"), QStringLiteral("帮我执行一次带工具的任务") }
        }
    });
    firstRequest.insert(QStringLiteral("tools"), QJsonArray {
        QJsonObject {
            { QStringLiteral("name"), QStringLiteral("execute_command") }
        }
    });

    QJsonObject toolFunction;
    toolFunction.insert(QStringLiteral("name"), QStringLiteral("execute_command"));
    toolFunction.insert(QStringLiteral("arguments"), QStringLiteral("{\"command\":\"dir\"}"));
    QJsonObject toolCall;
    toolCall.insert(QStringLiteral("id"), QStringLiteral("tool-1"));
    toolCall.insert(QStringLiteral("type"), QStringLiteral("function"));
    toolCall.insert(QStringLiteral("function"), toolFunction);

    QJsonObject toolChoiceMessage;
    toolChoiceMessage.insert(QStringLiteral("tool_calls"), QJsonArray { toolCall });
    QJsonObject toolChoice;
    toolChoice.insert(QStringLiteral("message"), toolChoiceMessage);
    toolChoice.insert(QStringLiteral("finish_reason"), QStringLiteral("tool_calls"));

    QJsonObject firstEntry;
    firstEntry.insert(QStringLiteral("request_id"), QStringLiteral("req-1"));
    firstEntry.insert(QStringLiteral("turn_id"), turnId);
    firstEntry.insert(QStringLiteral("trace_id"), traceId);
    firstEntry.insert(QStringLiteral("run_id"), runId);
    firstEntry.insert(QStringLiteral("request_recorded_at"), QStringLiteral("2026-03-13T10:00:00.000Z"));
    firstEntry.insert(QStringLiteral("response_recorded_at"), QStringLiteral("2026-03-13T10:00:01.000Z"));
    firstEntry.insert(QStringLiteral("request"), firstRequest);
    firstEntry.insert(QStringLiteral("response"), QJsonObject {
        { QStringLiteral("choices"), QJsonArray { toolChoice } }
    });

    QJsonObject secondRequest;
    secondRequest.insert(QStringLiteral("model"), QStringLiteral("claude-sonnet"));
    secondRequest.insert(QStringLiteral("messages"), QJsonArray {
        QJsonObject {
            { QStringLiteral("role"), QStringLiteral("user") },
            { QStringLiteral("content"), QStringLiteral("帮我执行一次带工具的任务") }
        },
        QJsonObject {
            { QStringLiteral("role"), QStringLiteral("assistant") },
            { QStringLiteral("tool_calls"), QJsonArray { toolCall } }
        },
        QJsonObject {
            { QStringLiteral("role"), QStringLiteral("tool") },
            { QStringLiteral("tool_call_id"), QStringLiteral("tool-1") },
            { QStringLiteral("content"), QStringLiteral("目录输出") }
        }
    });

    QJsonObject finalChoiceMessage;
    finalChoiceMessage.insert(QStringLiteral("content"), QStringLiteral("工具执行完成，我已经整理好了结果"));
    QJsonObject finalChoice;
    finalChoice.insert(QStringLiteral("message"), finalChoiceMessage);
    finalChoice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));

    QJsonObject secondEntry;
    secondEntry.insert(QStringLiteral("request_id"), QStringLiteral("req-2"));
    secondEntry.insert(QStringLiteral("turn_id"), turnId);
    secondEntry.insert(QStringLiteral("trace_id"), traceId);
    secondEntry.insert(QStringLiteral("run_id"), runId);
    secondEntry.insert(QStringLiteral("request_recorded_at"), QStringLiteral("2026-03-13T10:00:01.200Z"));
    secondEntry.insert(QStringLiteral("response_recorded_at"), QStringLiteral("2026-03-13T10:00:02.000Z"));
    secondEntry.insert(QStringLiteral("request"), secondRequest);
    secondEntry.insert(QStringLiteral("response"), QJsonObject {
        { QStringLiteral("choices"), QJsonArray { finalChoice } }
    });

    const QVector<ExecutionHistory::Record> records = ExecutionHistory::buildRecords(QJsonArray { firstEntry, secondEntry });
    QCOMPARE(records.size(), 1);
    const ExecutionHistory::Record& record = records.first();
    QCOMPARE(record.statusLabel, QStringLiteral("完成"));
    QVERIFY(record.outputSummary.contains(QStringLiteral("工具执行完成")));
    QCOMPARE(record.toolActivities.size(), 1);
    QVERIFY(record.toolSummary.contains(QStringLiteral("2 段模型交互")));
    const QJsonArray segments = record.interactionFactsLayer.value(QStringLiteral("segments")).toArray();
    QCOMPARE(segments.size(), 2);
    QVERIFY(record.metaSummary.contains(QStringLiteral("turn_id=turn-123")));
    QVERIFY(record.listTitle.contains(QStringLiteral("2 段")));
}

void HistoryFormattersTest::buildTurnSummaryText_containsStructuredSectionsAndDisclaimer()
{
    QJsonObject userMessage;
    userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
    userMessage.insert(QStringLiteral("content"), QStringLiteral("请总结这轮执行"));

    QJsonArray messages;
    messages.append(userMessage);

    QJsonObject request;
    request.insert(QStringLiteral("messages"), messages);
    request.insert(QStringLiteral("model"), QStringLiteral("claude-sonnet"));
    request.insert(QStringLiteral("tools"), QJsonArray());

    QJsonObject assistantMessage;
    assistantMessage.insert(QStringLiteral("content"), QStringLiteral("这是本轮总结"));

    QJsonObject choice;
    choice.insert(QStringLiteral("message"), assistantMessage);
    choice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));

    QJsonArray choices;
    choices.append(choice);

    QJsonObject response;
    response.insert(QStringLiteral("choices"), choices);

    QJsonObject entry;
    entry.insert(QStringLiteral("request_id"), QStringLiteral("req-1"));
    entry.insert(QStringLiteral("request"), request);
    entry.insert(QStringLiteral("response"), response);

    const QString summary = HistoryFormatters::buildTurnSummaryText(entry, 0);
    QVERIFY(summary.contains(QStringLiteral("记录 #1")));
    QVERIFY(summary.contains(QStringLiteral("输入摘要")));
    QVERIFY(summary.contains(QStringLiteral("输出摘要")));
    QVERIFY(summary.contains(QStringLiteral("过程概览")));
    QVERIFY(summary.contains(QStringLiteral("不等同于完整原始传输审计")));
}

QTEST_MAIN(HistoryFormattersTest)
