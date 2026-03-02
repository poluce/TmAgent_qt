#include "DelegateTaskScheduler.h"
#include "LLMAgent.h"
#include "ToolDispatcher.h"
#include "core/utils/DefaultPrompts.h"
#include "llm/ModelFactory.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace {
static constexpr int kMinExpectedTimeoutMs = 2000;
static constexpr int kMaxExpectedTimeoutMs = 300000;
static constexpr int kMaxHardTimeoutMs = 900000;
static constexpr int kChildTimelineLimit = 32;
static constexpr int kMaxSnapshots = 256;
static constexpr int kMaxAsyncJobs = 512;
static constexpr int kMinPlanTimeoutMs = 5000;
static constexpr int kMaxPlanTimeoutMs = 45000;
static constexpr int kMinReviewTimeoutMs = 4000;
static constexpr int kMaxReviewTimeoutMs = 30000;
static constexpr int kPlanOutputMaxChars = 1200;
static constexpr int kReviewOutputMaxChars = 900;
static constexpr int kStructuredRawOutputMaxChars = 1200;
static constexpr int kProgressForwardMinIntervalMs = 1500;
static constexpr int kMaxSameToolSummaryRepeats = 4;
static constexpr int kMaxConsecutiveToolFailures = 5;

struct QuickPassResult {
    bool success = false;
    bool softTimeoutNotified = false;
    bool hardTimeout = false;
    bool stalled = false;
    QString output;
    QString error;
    qint64 durationMs = 0;
    int streamChunkCount = 0;
    int streamChars = 0;
};

QuickPassResult runQuickPass(
    const LLMConfig& config,
    const QString& prompt,
    int softTimeoutMs,
    int hardTimeoutMs,
    int stallNoProgressMs)
{
    QuickPassResult out;
    LLMAgent agent;
    agent.setModelFactory(ModelFactory::instance());
    agent.setConfig(config);

    QEventLoop loop;
    QTimer watchdog;
    watchdog.setInterval(1000);
    watchdog.setSingleShot(false);

    bool settled = false;
    const qint64 startedAtMs = QDateTime::currentMSecsSinceEpoch();
    qint64 lastProgressAtMs = startedAtMs;

    const QMetaObject::Connection streamConn = QObject::connect(
        &agent,
        &LLMAgent::streamDataReceived,
        &loop,
        [&](const QString& chunk) {
            ++out.streamChunkCount;
            out.streamChars += chunk.size();
            lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
        });

    const QMetaObject::Connection finishedConn = QObject::connect(
        &agent,
        &LLMAgent::finished,
        &loop,
        [&](const QString& content) {
            if (settled)
                return;
            settled = true;
            out.success = true;
            out.output = content;
            watchdog.stop();
            loop.quit();
        });

    const QMetaObject::Connection errorConn = QObject::connect(
        &agent,
        &LLMAgent::errorOccurred,
        &loop,
        [&](const QString& msg) {
            if (settled)
                return;
            settled = true;
            out.success = false;
            out.error = msg.trimmed().isEmpty() ? QStringLiteral("sub-agent phase error") : msg.trimmed();
            watchdog.stop();
            loop.quit();
        });

    const QMetaObject::Connection watchdogConn = QObject::connect(
        &watchdog,
        &QTimer::timeout,
        &loop,
        [&]() {
            if (settled)
                return;
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 elapsedMs = nowMs - startedAtMs;
            const qint64 idleMs = nowMs - lastProgressAtMs;

            if (!out.softTimeoutNotified && elapsedMs >= softTimeoutMs)
                out.softTimeoutNotified = true;

            if (elapsedMs >= hardTimeoutMs) {
                settled = true;
                out.success = false;
                out.hardTimeout = true;
                out.error = QStringLiteral("phase hard timeout");
                agent.abort();
                watchdog.stop();
                loop.quit();
                return;
            }

            if (out.softTimeoutNotified && idleMs >= stallNoProgressMs) {
                settled = true;
                out.success = false;
                out.stalled = true;
                out.error = QStringLiteral("phase stalled");
                agent.abort();
                watchdog.stop();
                loop.quit();
            }
        });

    agent.askOnce(prompt);
    watchdog.start();
    loop.exec();
    QObject::disconnect(streamConn);
    QObject::disconnect(finishedConn);
    QObject::disconnect(errorConn);
    QObject::disconnect(watchdogConn);

    out.durationMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAtMs);
    if (!out.success && out.error.trimmed().isEmpty())
        out.error = QStringLiteral("phase failed");
    return out;
}

QString truncateForData(const QString& text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars) + QStringLiteral("\n...[truncated]...");
}

QString buildPlanPrompt(const QString& task)
{
    return QStringLiteral(
               "你现在是执行子代理，请只做任务规划，不要调用任何工具。\n"
               "任务：\n%1\n\n"
               "请输出以下结构（纯文本即可）：\n"
               "PLAN_OBJECTIVE: 目标\n"
               "PLAN_STEPS:\n"
               "1. ...\n2. ...\n"
               "DONE_CRITERIA: 完成判定\n"
               "RISKS: 风险与阻塞\n")
        .arg(task);
}

QString buildExecutionPrompt(const QString& task, const QString& planText)
{
    QString prompt = QStringLiteral("请执行以下任务，并在必要时使用工具。\n任务：\n%1\n").arg(task);
    if (!planText.trimmed().isEmpty()) {
        prompt += QStringLiteral("\n你之前形成的计划（可调整）:\n%1\n")
                      .arg(truncateForData(planText.trimmed(), kPlanOutputMaxChars));
    }
    prompt += QStringLiteral(
        "\n执行要求：\n"
        "1) 先验证路径，再扩展；避免无证据重复调用同一工具。\n"
        "2) 遇到错误先修正前提，不要盲目重试。\n"
        "3) 结束时必须输出结构化报告（严格按以下标签，不要省略）：\n"
        "STATUS: COMPLETED 或 PARTIAL 或 BLOCKED\n"
        "DONE:\n"
        "- 已完成项\n"
        "PENDING:\n"
        "- 未完成项\n"
        "EVIDENCE:\n"
        "- 关键证据（命令/输出/路径）\n"
        "RISKS:\n"
        "- 风险/阻塞\n"
        "NEXT:\n"
        "- 下一步建议\n");
    return prompt;
}

QString buildReviewPrompt(const QString& task, const QString& executionOutput)
{
    return QStringLiteral(
               "你现在做复盘评审，不要调用工具。\n"
               "原任务：\n%1\n\n"
               "执行输出：\n%2\n\n"
               "请只输出：\n"
               "STATUS: COMPLETED 或 PARTIAL 或 BLOCKED\n"
               "REASON: 不超过80字说明\n")
        .arg(task, truncateForData(executionOutput, 2400));
}

QString extractStatusTag(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral("(?im)^\\s*STATUS\\s*:\\s*(COMPLETED|PARTIAL|BLOCKED)\\b"));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return QString();
    return match.captured(1).trimmed().toUpper();
}

bool isBlockedStatus(const QString& statusTag)
{
    return statusTag == QLatin1String("BLOCKED");
}

QString canonicalStatusTag(const QString& rawStatus)
{
    const QString status = rawStatus.trimmed().toUpper();
    if (status == QLatin1String("COMPLETED")
        || status == QLatin1String("PARTIAL")
        || status == QLatin1String("BLOCKED")) {
        return status;
    }
    return QStringLiteral("PARTIAL");
}

QStringList collectReportLines(const QString& text, int maxLines)
{
    QStringList out;
    const QStringList sourceLines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (QString line : sourceLines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QStringLiteral("STATUS:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("DONE:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("PENDING:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("EVIDENCE:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("RISKS:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("NEXT:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("RAW_OUTPUT:"), Qt::CaseInsensitive)) {
            continue;
        }
        if (line.startsWith(QLatin1Char('-')))
            line = line.mid(1).trimmed();
        if (line.size() > 200)
            line = line.left(200) + QStringLiteral("...");
        out.append(line);
        if (out.size() >= maxLines)
            break;
    }
    return out;
}

QString ensureStructuredDelegateOutput(
    const QString& task,
    const QString& rawText,
    const QString& statusHint,
    bool* normalizedByScheduler = nullptr)
{
    if (normalizedByScheduler)
        *normalizedByScheduler = false;

    const QString text = rawText.trimmed();
    const QString status = canonicalStatusTag(statusHint.isEmpty() ? extractStatusTag(text) : statusHint);
    const bool hasStatus = QRegularExpression(
                               QStringLiteral("(?im)^\\s*STATUS\\s*:\\s*(COMPLETED|PARTIAL|BLOCKED)\\b"))
                               .match(text)
                               .hasMatch();
    const bool hasEvidence = QRegularExpression(QStringLiteral("(?im)^\\s*EVIDENCE\\s*:")).match(text).hasMatch();
    const bool hasNext = QRegularExpression(QStringLiteral("(?im)^\\s*NEXT\\s*:")).match(text).hasMatch();
    if (hasStatus && hasEvidence && hasNext)
        return text;

    if (normalizedByScheduler)
        *normalizedByScheduler = true;

    const QStringList lines = collectReportLines(text, 6);
    QStringList report;
    report << QStringLiteral("[Sub-agent Report]");
    report << QStringLiteral("STATUS: %1").arg(status);
    if (!task.trimmed().isEmpty())
        report << QStringLiteral("TASK: %1").arg(task.trimmed().left(240));
    report << QStringLiteral("DONE:");
    if (!lines.isEmpty()) {
        for (const QString& line : lines)
            report << QStringLiteral("- %1").arg(line);
    } else {
        report << QStringLiteral("- (未提取到明确完成项)");
    }
    report << QStringLiteral("PENDING:");
    if (status == QLatin1String("COMPLETED"))
        report << QStringLiteral("- (无)");
    else
        report << QStringLiteral("- 需要主代理补充约束或继续拆分任务。");
    report << QStringLiteral("EVIDENCE:");
    if (!lines.isEmpty()) {
        for (const QString& line : lines.mid(0, 3))
            report << QStringLiteral("- %1").arg(line);
    } else if (!text.isEmpty()) {
        report << QStringLiteral("- 已返回文本输出，详见 RAW_OUTPUT。");
    } else {
        report << QStringLiteral("- 子代理未返回有效正文。");
    }
    report << QStringLiteral("RISKS:");
    if (status == QLatin1String("BLOCKED"))
        report << QStringLiteral("- 当前存在阻塞，需要补充信息/权限/路径。");
    else if (status == QLatin1String("PARTIAL"))
        report << QStringLiteral("- 当前结果可能不完整，建议复核后继续。");
    else
        report << QStringLiteral("- (无明显风险)");
    report << QStringLiteral("NEXT:");
    if (status == QLatin1String("COMPLETED"))
        report << QStringLiteral("- 主代理可汇总结果并向用户确认是否继续深化。");
    else
        report << QStringLiteral("- 主代理应先补齐阻塞条件，再发起下一轮委派。");
    if (!text.isEmpty()) {
        report << QStringLiteral("RAW_OUTPUT:");
        report << truncateForData(text, kStructuredRawOutputMaxChars);
    }
    return report.join(QStringLiteral("\n"));
}

LLMConfig buildChildConfig(const DelegateTaskScheduler::Request& request, QJsonObject* data)
{
    LLMConfig child = request.useOverrideConfig ? request.overrideConfig : request.parentConfig;
    if (child.workspaceDir.trimmed().isEmpty())
        child.workspaceDir = request.parentConfig.workspaceDir;
    if (child.configId.trimmed().isEmpty())
        child.configId = request.parentConfig.configId;

    child.recursionDepth = request.restrictDelegation
        ? 0
        : qMax(0, request.parentConfig.recursionDepth - 1);

    QString rolePrompt = request.rolePrompt.trimmed();
    if (rolePrompt.isEmpty())
        rolePrompt = DefaultPrompts::subAgentWorkerSystemPrompt();
    child.systemPrompt = DefaultPrompts::ensureExecutionDiscipline(rolePrompt);

    child.userName = request.delegateToolName.trimmed().isEmpty()
        ? QStringLiteral("delegate_task")
        : request.delegateToolName.trimmed();
    child.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);

    if (data) {
        data->insert(QStringLiteral("child_agent_id"), child.uuid);
        data->insert(QStringLiteral("child_model"), ModelFactory::resolveConfigKey(child));
        if (!child.workspaceDir.trimmed().isEmpty())
            data->insert(QStringLiteral("child_workspace"), child.workspaceDir.trimmed());
    }
    return child;
}
} // namespace

struct DelegateTaskScheduler::AsyncJobRuntime {
    QString jobId;
    QString ownerAgentId;
    QString status;
    QString summary;
    QString failureReason;
    QString task;
    QString result;

    qint64 createdAtMs = 0;
    qint64 startedAtMs = 0;
    qint64 lastProgressAtMs = 0;
    qint64 finishedAtMs = 0;
    int expectedTimeoutMs = 0;
    int hardTimeoutMs = 0;
    int stallNoProgressMs = 0;
    int maxResponseChars = 0;

    int childToolStartedCount = 0;
    int childToolProgressCount = 0;
    int childToolCompletedCount = 0;
    int childToolSuccessCount = 0;
    int childToolFailureCount = 0;
    int childStreamChunkCount = 0;
    int childStreamChars = 0;
    int childTimelineDropped = 0;
    QStringList childTools;
    QJsonArray childTimeline;

    QString childAgentId;
    QString childModel;
    QString childRequestId;
    QString childTraceId;
    QString childFinishReason;
    QString childError;

    bool settled = false;
    bool cancelRequested = false;
    bool softTimeoutNotified = false;
    bool stallNoticeEmitted = false;

    QPointer<LLMAgent> childAgent;
    QPointer<QTimer> watchdog;
};

DelegateTaskScheduler* DelegateTaskScheduler::instance()
{
    static DelegateTaskScheduler scheduler;
    return &scheduler;
}

DelegateTaskScheduler::Result DelegateTaskScheduler::executeSync(const Request& request)
{
    Result result;
    QJsonObject data;
    data.insert(
        QStringLiteral("delegate_tool"),
        request.delegateToolName.trimmed().isEmpty()
            ? QStringLiteral("delegate_task")
            : request.delegateToolName.trimmed());
    data.insert(
        QStringLiteral("requested_at"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    const QString task = request.task.trimmed();
    if (task.isEmpty()) {
        data.insert(QStringLiteral("status"), QStringLiteral("failed"));
        data.insert(QStringLiteral("failure_reason"), QStringLiteral("missing_task"));
        result.success = false;
        result.rawResult = QStringLiteral("错误: delegate_task 必须显式提供非空 task");
        result.userSummary = QStringLiteral("子智能体执行失败：委派参数缺失");
        result.data = data;
        return result;
    }

    if (request.parentConfig.recursionDepth <= 0) {
        data.insert(QStringLiteral("status"), QStringLiteral("failed"));
        data.insert(
            QStringLiteral("failure_reason"),
            QStringLiteral("recursion_depth_exhausted"));
        result.success = false;
        result.rawResult = QStringLiteral("错误: 当前递归深度已耗尽，不能继续委派");
        result.userSummary = QStringLiteral("子智能体执行失败：递归深度不足");
        result.data = data;
        return result;
    }

    const int softTimeoutMs = qBound(
        kMinExpectedTimeoutMs,
        request.expectedTimeoutMs,
        kMaxExpectedTimeoutMs);
    const int hardTimeoutMs = calcHardTimeoutMs(softTimeoutMs);
    const int stallNoProgressMs = calcStallNoProgressMs(softTimeoutMs);
    const int maxResponseChars = qBound(500, request.maxResponseChars, 20000);
    data.insert(QStringLiteral("expected_timeout_ms"), softTimeoutMs);
    data.insert(QStringLiteral("child_timeout_ms"), softTimeoutMs);
    data.insert(QStringLiteral("child_hard_timeout_ms"), hardTimeoutMs);
    data.insert(QStringLiteral("child_stall_no_progress_ms"), stallNoProgressMs);
    data.insert(QStringLiteral("max_response_chars"), maxResponseChars);
    data.insert(QStringLiteral("restrict_delegation"), request.restrictDelegation);
    data.insert(
        QStringLiteral("inherited_allowed_tools_count"),
        request.inheritedAllowedTools.size());

    LLMConfig childConfig = buildChildConfig(request, &data);
    const int planTimeoutMs = qBound(kMinPlanTimeoutMs, softTimeoutMs / 3, kMaxPlanTimeoutMs);
    const int reviewTimeoutMs = qBound(kMinReviewTimeoutMs, softTimeoutMs / 4, kMaxReviewTimeoutMs);
    const int planHardTimeoutMs = calcHardTimeoutMs(planTimeoutMs);
    const int reviewHardTimeoutMs = calcHardTimeoutMs(reviewTimeoutMs);
    const int planStallMs = calcStallNoProgressMs(planTimeoutMs);
    const int reviewStallMs = calcStallNoProgressMs(reviewTimeoutMs);

    data.insert(QStringLiteral("plan_timeout_ms"), planTimeoutMs);
    data.insert(QStringLiteral("review_timeout_ms"), reviewTimeoutMs);

    const QuickPassResult planPass = runQuickPass(
        childConfig,
        buildPlanPrompt(task),
        planTimeoutMs,
        planHardTimeoutMs,
        planStallMs);
    const QString planOutput = truncateForData(planPass.output.trimmed(), kPlanOutputMaxChars);
    data.insert(QStringLiteral("plan_status"), planPass.success ? QStringLiteral("completed") : QStringLiteral("failed"));
    data.insert(QStringLiteral("plan_duration_ms"), static_cast<double>(planPass.durationMs));
    data.insert(QStringLiteral("plan_stream_chunk_count"), planPass.streamChunkCount);
    data.insert(QStringLiteral("plan_stream_chars"), planPass.streamChars);
    if (!planOutput.isEmpty())
        data.insert(QStringLiteral("plan_output"), planOutput);
    if (!planPass.success)
        data.insert(QStringLiteral("plan_error"), planPass.error);

    const QString executionPrompt = buildExecutionPrompt(task, planOutput);

    Snapshot snapshotValue;
    snapshotValue.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    snapshotValue.status = QStringLiteral("running");
    snapshotValue.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    snapshotValue.lastProgressAtMs = snapshotValue.startedAtMs;
    snapshotValue.expectedTimeoutMs = softTimeoutMs;
    snapshotValue.softTimeoutMs = softTimeoutMs;
    snapshotValue.hardTimeoutMs = hardTimeoutMs;
    snapshotValue.stallNoProgressMs = stallNoProgressMs;
    snapshotValue.childAgentId = childConfig.uuid;
    snapshotValue.childModel = ModelFactory::resolveConfigKey(childConfig);
    upsertSnapshot(snapshotValue);
    data.insert(QStringLiteral("scheduler_task_id"), snapshotValue.taskId);

    LLMAgent childAgent;
    childAgent.setModelFactory(ModelFactory::instance());
    childAgent.setConfig(childConfig);
    if (request.toolDispatcher) {
        QStringList childAllowedTools = request.inheritedAllowedTools;
        if (request.restrictDelegation)
            childAllowedTools.removeAll(QStringLiteral("delegate_task"));
        if (childAllowedTools.isEmpty())
            childAgent.setToolDispatcher(request.toolDispatcher);
        else
            childAgent.setToolDispatcher(request.toolDispatcher, childAllowedTools);
    }

    QEventLoop loop;
    QTimer watchdog;
    watchdog.setInterval(1000);
    watchdog.setSingleShot(false);

    QString finalResult;
    QString errorMessage;
    bool settled = false;
    bool success = true;
    bool softTimeoutNotified = false;
    bool stallNotified = false;
    qint64 lastForwardProgressEventMs = 0;
    qint64 lastForwardStreamMs = 0;
    QString streamForwardBuffer;
    QString lastToolSummary;
    int sameToolSummaryRepeatCount = 0;
    int consecutiveToolFailures = 0;

    auto markProgress = [&](const QString& summary) {
        snapshotValue.lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
        if (!summary.trimmed().isEmpty())
            snapshotValue.summary = summary.left(240);
        stallNotified = false;
        if (snapshotValue.status != QLatin1String("running"))
            snapshotValue.status = QStringLiteral("running");
        upsertSnapshot(snapshotValue);
    };

    const QMetaObject::Connection toolConn = QObject::connect(
        &childAgent,
        &LLMAgent::toolEvent,
        &loop,
        [&](const ToolExecutionEvent& event) {
            const QString toolName = event.toolName.trimmed();
            if (!toolName.isEmpty()
                && !snapshotValue.childTools.contains(toolName)) {
                snapshotValue.childTools.append(toolName);
            }

            if (event.status == QLatin1String("started")) {
                ++snapshotValue.childToolStartedCount;
            } else if (event.status == QLatin1String("progress")) {
                ++snapshotValue.childToolProgressCount;
            } else if (event.status == QLatin1String("completed")) {
                ++snapshotValue.childToolCompletedCount;
                if (event.success)
                    ++snapshotValue.childToolSuccessCount;
                else
                    ++snapshotValue.childToolFailureCount;
            }

            if (snapshotValue.childTimeline.size() < kChildTimelineLimit) {
                QJsonObject row;
                row.insert(
                    QStringLiteral("timestamp"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                row.insert(QStringLiteral("status"), event.status.trimmed());
                if (!toolName.isEmpty())
                    row.insert(QStringLiteral("tool_name"), toolName);
                if (!event.toolId.trimmed().isEmpty())
                    row.insert(QStringLiteral("tool_id"), event.toolId.trimmed());
                if (event.status == QLatin1String("completed"))
                    row.insert(QStringLiteral("success"), event.success);
                const QString summary = summarizeTimelineEntry(event);
                if (!summary.isEmpty())
                    row.insert(QStringLiteral("summary"), summary);
                snapshotValue.childTimeline.append(row);
            } else {
                ++snapshotValue.childTimelineDropped;
            }

            const QString summary = summarizeTimelineEntry(event);
            markProgress(summary);

            if (event.status == QLatin1String("completed")) {
                if (!summary.isEmpty() && summary == lastToolSummary) {
                    ++sameToolSummaryRepeatCount;
                } else {
                    sameToolSummaryRepeatCount = 1;
                    lastToolSummary = summary;
                }
                if (event.success)
                    consecutiveToolFailures = 0;
                else
                    ++consecutiveToolFailures;

                const bool tooManyRepeats = sameToolSummaryRepeatCount >= kMaxSameToolSummaryRepeats;
                const bool tooManyFailures = consecutiveToolFailures >= kMaxConsecutiveToolFailures;
                if (tooManyRepeats || tooManyFailures) {
                    settled = true;
                    success = false;
                    errorMessage = tooManyRepeats
                        ? QStringLiteral("sub-agent no_progress_guard")
                        : QStringLiteral("sub-agent too_many_failures");
                    snapshotValue.failureReason = errorMessage;
                    snapshotValue.status = QStringLiteral("failed");
                    snapshotValue.summary = QStringLiteral("子代理触发无进展保护，已停止");
                    upsertSnapshot(snapshotValue);
                    childAgent.abort();
                    watchdog.stop();
                    loop.quit();
                    return;
                }
            }

            if (request.onChildToolEvent) {
                if (event.status != QLatin1String("progress")) {
                    request.onChildToolEvent(event);
                } else {
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    if (lastForwardProgressEventMs == 0
                        || (nowMs - lastForwardProgressEventMs) >= kProgressForwardMinIntervalMs) {
                        lastForwardProgressEventMs = nowMs;
                        request.onChildToolEvent(event);
                    }
                }
            }
        });

    const QMetaObject::Connection streamConn = QObject::connect(
        &childAgent,
        &LLMAgent::streamDataReceived,
        &loop,
        [&](const QString& dataChunk) {
            ++snapshotValue.childStreamChunkCount;
            snapshotValue.childStreamChars += dataChunk.size();
            markProgress(QStringLiteral("子代理流式输出中"));
            if (request.onChildStreamData) {
                streamForwardBuffer += dataChunk;
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                const bool enoughChars = streamForwardBuffer.size() >= 120;
                const bool enoughTime = (lastForwardStreamMs == 0)
                    || ((nowMs - lastForwardStreamMs) >= kProgressForwardMinIntervalMs);
                if (enoughChars || enoughTime) {
                    lastForwardStreamMs = nowMs;
                    request.onChildStreamData(streamForwardBuffer.right(160));
                    streamForwardBuffer.clear();
                }
            }
        });

    const QMetaObject::Connection finishedConn = QObject::connect(
        &childAgent,
        &LLMAgent::finished,
        &loop,
        [&](const QString& content) {
            if (settled)
                return;
            settled = true;
            finalResult = content;
            watchdog.stop();
            loop.quit();
        });

    const QMetaObject::Connection errorConn = QObject::connect(
        &childAgent,
        &LLMAgent::errorOccurred,
        &loop,
        [&](const QString& errorMsg) {
            if (settled)
                return;
            settled = true;
            success = false;
            errorMessage = errorMsg.trimmed().isEmpty()
                ? QStringLiteral("sub-agent error")
                : errorMsg.trimmed();
            watchdog.stop();
            loop.quit();
        });

    const QMetaObject::Connection watchdogConn = QObject::connect(
        &watchdog,
        &QTimer::timeout,
        &loop,
        [&]() {
            if (settled)
                return;

            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 elapsedMs = nowMs - snapshotValue.startedAtMs;
            const qint64 idleMs = nowMs - snapshotValue.lastProgressAtMs;

            if (!softTimeoutNotified && elapsedMs >= softTimeoutMs) {
                softTimeoutNotified = true;
                snapshotValue.status = QStringLiteral("soft_timeout");
                snapshotValue.summary =
                    QStringLiteral("子代理超过预计时间，继续等待中");
                upsertSnapshot(snapshotValue);

                if (request.onChildToolEvent) {
                    ToolExecutionEvent e;
                    e.toolName = request.delegateToolName.trimmed().isEmpty()
                        ? QStringLiteral("delegate_task")
                        : request.delegateToolName.trimmed();
                    e.status = QStringLiteral("progress");
                    e.success = true;
                    e.formattedResult = QStringLiteral(
                        "子代理已超过预计时间，正在继续执行并等待结果");
                    e.rawResult = e.formattedResult;
                    request.onChildToolEvent(e);
                }
            }

            if (elapsedMs >= hardTimeoutMs) {
                settled = true;
                success = false;
                errorMessage = QStringLiteral("sub-agent hard timeout");
                snapshotValue.failureReason = errorMessage;
                snapshotValue.status = QStringLiteral("failed");
                upsertSnapshot(snapshotValue);
                childAgent.abort();
                watchdog.stop();
                loop.quit();
                return;
            }

            if (softTimeoutNotified && idleMs >= stallNoProgressMs) {
                if (stallNotified)
                    return;
                stallNotified = true;
                snapshotValue.status = QStringLiteral("soft_timeout");
                snapshotValue.summary =
                    QStringLiteral("子代理长时间无新进展，继续等待中（可取消）");
                upsertSnapshot(snapshotValue);
                if (request.onChildToolEvent) {
                    ToolExecutionEvent e;
                    e.toolName = request.delegateToolName.trimmed().isEmpty()
                        ? QStringLiteral("delegate_task")
                        : request.delegateToolName.trimmed();
                    e.status = QStringLiteral("progress");
                    e.success = true;
                    e.formattedResult = QStringLiteral(
                        "子代理长时间无新进展，仍在继续执行；可继续等待或取消任务");
                    e.rawResult = e.formattedResult;
                    request.onChildToolEvent(e);
                }
            }
        });

    childAgent.askOnce(executionPrompt);
    watchdog.start();
    loop.exec();
    if (request.onChildStreamData && !streamForwardBuffer.isEmpty())
        request.onChildStreamData(streamForwardBuffer.right(160));
    QObject::disconnect(toolConn);
    QObject::disconnect(streamConn);
    QObject::disconnect(finishedConn);
    QObject::disconnect(errorConn);
    QObject::disconnect(watchdogConn);

    const qint64 finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 durationMs = qMax<qint64>(0, finishedAtMs - snapshotValue.startedAtMs);

    data.insert(QStringLiteral("child_duration_ms"), static_cast<double>(durationMs));
    data.insert(QStringLiteral("child_tool_started_count"), snapshotValue.childToolStartedCount);
    data.insert(QStringLiteral("child_tool_progress_count"), snapshotValue.childToolProgressCount);
    data.insert(QStringLiteral("child_tool_completed_count"), snapshotValue.childToolCompletedCount);
    data.insert(QStringLiteral("child_tool_success_count"), snapshotValue.childToolSuccessCount);
    data.insert(QStringLiteral("child_tool_failure_count"), snapshotValue.childToolFailureCount);
    data.insert(QStringLiteral("child_stream_chunk_count"), snapshotValue.childStreamChunkCount);
    data.insert(QStringLiteral("child_stream_chars"), snapshotValue.childStreamChars);
    data.insert(QStringLiteral("child_timeline_dropped"), snapshotValue.childTimelineDropped);
    if (!snapshotValue.childTimeline.isEmpty())
        data.insert(QStringLiteral("child_timeline"), snapshotValue.childTimeline);
    if (!snapshotValue.childTools.isEmpty()) {
        QStringList sortedTools = snapshotValue.childTools;
        sortedTools.sort();
        data.insert(
            QStringLiteral("child_tools"),
            QJsonArray::fromStringList(sortedTools));
    }

    const QJsonObject childRunData = collectChildRunData(childAgent.getIoHistory());
    for (auto it = childRunData.constBegin(); it != childRunData.constEnd(); ++it)
        data.insert(it.key(), it.value());
    snapshotValue.childRequestId = childRunData.value(QStringLiteral("child_request_id")).toString();
    snapshotValue.childTraceId = childRunData.value(QStringLiteral("child_trace_id")).toString();
    snapshotValue.childFinishReason = childRunData.value(QStringLiteral("child_finish_reason")).toString();
    snapshotValue.childError = childRunData.value(QStringLiteral("child_error")).toString();

    if (!snapshotValue.childRequestId.isEmpty())
        data.insert(QStringLiteral("child_request_id"), snapshotValue.childRequestId);
    if (!snapshotValue.childTraceId.isEmpty())
        data.insert(QStringLiteral("child_trace_id"), snapshotValue.childTraceId);
    if (!snapshotValue.childFinishReason.isEmpty())
        data.insert(QStringLiteral("child_finish_reason"), snapshotValue.childFinishReason);
    if (!snapshotValue.childError.isEmpty())
        data.insert(QStringLiteral("child_error"), snapshotValue.childError);

    if (!success) {
        data.insert(QStringLiteral("status"), QStringLiteral("failed"));
        data.insert(QStringLiteral("failure_reason"), errorMessage);
        result.success = false;
        result.rawResult = QStringLiteral("Sub-agent error: ") + errorMessage;
        result.userSummary = QStringLiteral("子智能体执行出错");
        result.data = data;

        snapshotValue.status = QStringLiteral("failed");
        snapshotValue.failureReason = errorMessage;
        snapshotValue.summary = result.userSummary;
        snapshotValue.finishedAtMs = finishedAtMs;
        upsertSnapshot(snapshotValue);
        return result;
    }

    if (isChildGuarded(snapshotValue.childFinishReason, finalResult)) {
        data.insert(QStringLiteral("status"), QStringLiteral("failed"));
        data.insert(QStringLiteral("failure_reason"), QStringLiteral("child_tool_loop_guard"));
        result.success = false;
        result.rawResult = finalResult.trimmed().isEmpty()
            ? QStringLiteral("Sub-agent error: tool_loop_guard")
            : finalResult;
        result.userSummary = QStringLiteral("子智能体触发熔断，任务未完成");
        result.data = data;

        snapshotValue.status = QStringLiteral("failed");
        snapshotValue.failureReason = QStringLiteral("child_tool_loop_guard");
        snapshotValue.summary = result.userSummary;
        snapshotValue.finishedAtMs = finishedAtMs;
        upsertSnapshot(snapshotValue);
        return result;
    }

    QString normalized = finalResult.trimmed();
    const QString executeStatus = extractStatusTag(normalized);
    if (!executeStatus.isEmpty())
        data.insert(QStringLiteral("child_execute_status"), executeStatus);

    const QuickPassResult reviewPass = runQuickPass(
        childConfig,
        buildReviewPrompt(task, normalized),
        reviewTimeoutMs,
        reviewHardTimeoutMs,
        reviewStallMs);
    const QString reviewOutput = truncateForData(reviewPass.output.trimmed(), kReviewOutputMaxChars);
    QString finalStatus = extractStatusTag(reviewOutput);
    if (finalStatus.isEmpty())
        finalStatus = executeStatus;

    data.insert(QStringLiteral("review_status"), reviewPass.success ? QStringLiteral("completed") : QStringLiteral("failed"));
    data.insert(QStringLiteral("review_duration_ms"), static_cast<double>(reviewPass.durationMs));
    if (!reviewOutput.isEmpty())
        data.insert(QStringLiteral("review_output"), reviewOutput);
    if (!reviewPass.success)
        data.insert(QStringLiteral("review_error"), reviewPass.error);

    bool normalizedByScheduler = false;
    normalized = ensureStructuredDelegateOutput(task, normalized, finalStatus, &normalizedByScheduler);
    if (normalizedByScheduler)
        data.insert(QStringLiteral("child_output_normalized"), true);
    const QString normalizedStatus = extractStatusTag(normalized);
    if (!normalizedStatus.isEmpty())
        finalStatus = normalizedStatus;
    if (!finalStatus.isEmpty())
        data.insert(QStringLiteral("child_final_status"), finalStatus);

    if (normalized.size() > maxResponseChars) {
        normalized =
            normalized.left(maxResponseChars)
            + QStringLiteral("\n...[delegate response truncated]...");
        data.insert(QStringLiteral("truncated"), true);
    }

    if (isBlockedStatus(finalStatus)) {
        data.insert(QStringLiteral("status"), QStringLiteral("failed"));
        data.insert(QStringLiteral("failure_reason"), QStringLiteral("child_blocked"));
        result.success = false;
        result.rawResult = normalized.isEmpty()
            ? QStringLiteral("Sub-agent error: blocked")
            : normalized;
        result.userSummary = QStringLiteral("子智能体受阻，任务未完成");
        result.data = data;

        snapshotValue.status = QStringLiteral("failed");
        snapshotValue.failureReason = QStringLiteral("child_blocked");
        snapshotValue.summary = result.userSummary;
        snapshotValue.finishedAtMs = finishedAtMs;
        upsertSnapshot(snapshotValue);
        return result;
    }

    data.insert(QStringLiteral("status"), QStringLiteral("completed"));
    result.success = true;
    result.rawResult = normalized;
    if (finalStatus == QLatin1String("PARTIAL")) {
        result.userSummary = QStringLiteral("子智能体部分完成");
    } else {
        result.userSummary = QStringLiteral("子智能体任务完成");
    }
    result.data = data;

    snapshotValue.status = QStringLiteral("completed");
    snapshotValue.summary = result.userSummary;
    snapshotValue.finishedAtMs = finishedAtMs;
    upsertSnapshot(snapshotValue);
    return result;
}

DelegateTaskScheduler::Snapshot DelegateTaskScheduler::snapshot(const QString& taskId) const
{
    const QString id = taskId.trimmed();
    QReadLocker locker(&m_lock);
    return m_snapshots.value(id);
}

DelegateTaskScheduler::JobInfo DelegateTaskScheduler::toJobInfo(const QSharedPointer<AsyncJobRuntime>& runtime)
{
    JobInfo info;
    if (!runtime)
        return info;

    info.jobId = runtime->jobId;
    info.ownerAgentId = runtime->ownerAgentId;
    info.status = runtime->status;
    info.summary = runtime->summary;
    info.failureReason = runtime->failureReason;
    info.task = runtime->task;
    info.result = runtime->result;
    info.createdAtMs = runtime->createdAtMs;
    info.startedAtMs = runtime->startedAtMs;
    info.lastProgressAtMs = runtime->lastProgressAtMs;
    info.finishedAtMs = runtime->finishedAtMs;
    info.expectedTimeoutMs = runtime->expectedTimeoutMs;
    info.hardTimeoutMs = runtime->hardTimeoutMs;
    info.stallNoProgressMs = runtime->stallNoProgressMs;
    info.childToolStartedCount = runtime->childToolStartedCount;
    info.childToolProgressCount = runtime->childToolProgressCount;
    info.childToolCompletedCount = runtime->childToolCompletedCount;
    info.childToolSuccessCount = runtime->childToolSuccessCount;
    info.childToolFailureCount = runtime->childToolFailureCount;
    info.childStreamChunkCount = runtime->childStreamChunkCount;
    info.childStreamChars = runtime->childStreamChars;
    info.childTools = runtime->childTools;
    return info;
}

void DelegateTaskScheduler::pruneJobsLocked()
{
    if (m_asyncJobOrder.size() <= kMaxAsyncJobs)
        return;

    auto isFinished = [](const QSharedPointer<AsyncJobRuntime>& runtime) {
        if (!runtime)
            return true;
        const QString status = runtime->status;
        return status == QLatin1String("completed")
            || status == QLatin1String("failed")
            || status == QLatin1String("cancelled");
    };

    while (m_asyncJobOrder.size() > kMaxAsyncJobs) {
        int removeIndex = -1;
        for (int i = 0; i < m_asyncJobOrder.size(); ++i) {
            const QSharedPointer<AsyncJobRuntime> runtime = m_asyncJobs.value(m_asyncJobOrder.at(i));
            if (isFinished(runtime)) {
                removeIndex = i;
                break;
            }
        }
        if (removeIndex < 0)
            removeIndex = 0;
        const QString jobId = m_asyncJobOrder.takeAt(removeIndex);
        m_asyncJobs.remove(jobId);
    }
}

void DelegateTaskScheduler::settleAsyncJob(
    const QSharedPointer<AsyncJobRuntime>& runtime,
    bool success,
    const QString& result,
    const QString& failureReason)
{
    if (!runtime)
        return;

    QPointer<QTimer> watchdog;
    QPointer<LLMAgent> childAgent;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    {
        QWriteLocker locker(&m_lock);
        if (runtime->settled)
            return;
        runtime->settled = true;
        runtime->finishedAtMs = nowMs;
        runtime->lastProgressAtMs = nowMs;
        runtime->result = result;
        if (success) {
            const QString childStatus = extractStatusTag(result);
            runtime->status = QStringLiteral("completed");
            if (childStatus == QLatin1String("PARTIAL")) {
                runtime->summary = QStringLiteral("后台子代理任务部分完成");
            } else {
                runtime->summary = QStringLiteral("后台子代理任务完成");
            }
            runtime->failureReason.clear();
        } else {
            runtime->status = runtime->cancelRequested
                ? QStringLiteral("cancelled")
                : QStringLiteral("failed");
            runtime->summary = runtime->cancelRequested
                ? QStringLiteral("后台子代理任务已取消")
                : QStringLiteral("后台子代理任务失败");
            runtime->failureReason = failureReason.trimmed();
        }
        watchdog = runtime->watchdog;
        childAgent = runtime->childAgent;
        runtime->watchdog = nullptr;
        runtime->childAgent = nullptr;
    }

    if (watchdog) {
        watchdog->stop();
        watchdog->deleteLater();
    }
    if (childAgent) {
        childAgent->deleteLater();
    }
}

DelegateTaskScheduler::Result DelegateTaskScheduler::submitAsync(const Request& request, const QString& ownerAgentId)
{
    Result result;
    QJsonObject data;
    data.insert(
        QStringLiteral("delegate_tool"),
        request.delegateToolName.trimmed().isEmpty()
            ? QStringLiteral("delegate_task")
            : request.delegateToolName.trimmed());
    data.insert(
        QStringLiteral("requested_at"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    const QString task = request.task.trimmed();
    if (task.isEmpty()) {
        data.insert(QStringLiteral("status"), QStringLiteral("failed"));
        data.insert(QStringLiteral("failure_reason"), QStringLiteral("missing_task"));
        result.success = false;
        result.rawResult = QStringLiteral("错误: delegate_task 必须显式提供非空 task");
        result.userSummary = QStringLiteral("后台子代理任务提交失败：委派参数缺失");
        result.data = data;
        return result;
    }

    if (request.parentConfig.recursionDepth <= 0) {
        data.insert(QStringLiteral("status"), QStringLiteral("failed"));
        data.insert(QStringLiteral("failure_reason"), QStringLiteral("recursion_depth_exhausted"));
        result.success = false;
        result.rawResult = QStringLiteral("错误: 当前递归深度已耗尽，不能继续委派");
        result.userSummary = QStringLiteral("后台子代理任务提交失败：递归深度不足");
        result.data = data;
        return result;
    }

    const int softTimeoutMs = qBound(kMinExpectedTimeoutMs, request.expectedTimeoutMs, kMaxExpectedTimeoutMs);
    const int hardTimeoutMs = calcHardTimeoutMs(softTimeoutMs);
    const int stallNoProgressMs = calcStallNoProgressMs(softTimeoutMs);
    const int maxResponseChars = qBound(500, request.maxResponseChars, 20000);
    const QString normalizedOwnerId = ownerAgentId.trimmed();
    LLMConfig childConfig = buildChildConfig(request, &data);
    const QString executionPrompt = buildExecutionPrompt(task, QString());

    const QSharedPointer<AsyncJobRuntime> runtime(new AsyncJobRuntime());
    runtime->jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    runtime->ownerAgentId = normalizedOwnerId;
    runtime->status = QStringLiteral("running");
    runtime->summary = QStringLiteral("后台子代理任务已启动");
    runtime->task = task.left(4000);
    runtime->createdAtMs = QDateTime::currentMSecsSinceEpoch();
    runtime->startedAtMs = runtime->createdAtMs;
    runtime->lastProgressAtMs = runtime->createdAtMs;
    runtime->expectedTimeoutMs = softTimeoutMs;
    runtime->hardTimeoutMs = hardTimeoutMs;
    runtime->stallNoProgressMs = stallNoProgressMs;
    runtime->maxResponseChars = maxResponseChars;
    runtime->childAgentId = childConfig.uuid;
    runtime->childModel = ModelFactory::resolveConfigKey(childConfig);
    const QString taskForResult = runtime->task;

    LLMAgent* childAgent = new LLMAgent(QCoreApplication::instance());
    childAgent->setModelFactory(ModelFactory::instance());
    childAgent->setConfig(childConfig);
    if (request.toolDispatcher) {
        QStringList childAllowedTools = request.inheritedAllowedTools;
        if (request.restrictDelegation)
            childAllowedTools.removeAll(QStringLiteral("delegate_task"));
        childAllowedTools.removeAll(QStringLiteral("delegate_status"));
        childAllowedTools.removeAll(QStringLiteral("delegate_cancel"));
        childAllowedTools.removeAll(QStringLiteral("delegate_list_active"));
        if (childAllowedTools.isEmpty())
            childAgent->setToolDispatcher(request.toolDispatcher);
        else
            childAgent->setToolDispatcher(request.toolDispatcher, childAllowedTools);
    }
    runtime->childAgent = childAgent;

    QTimer* watchdog = new QTimer(QCoreApplication::instance());
    watchdog->setInterval(1000);
    watchdog->setSingleShot(false);
    runtime->watchdog = watchdog;

    {
        QWriteLocker locker(&m_lock);
        m_asyncJobs.insert(runtime->jobId, runtime);
        m_asyncJobOrder.append(runtime->jobId);
        pruneJobsLocked();
    }

    QObject::connect(
        childAgent,
        &LLMAgent::toolEvent,
        childAgent,
        [this, runtime](const ToolExecutionEvent& event) {
            QWriteLocker locker(&m_lock);
            if (runtime->settled)
                return;

            runtime->lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
            runtime->stallNoticeEmitted = false;
            if (runtime->softTimeoutNotified)
                runtime->status = QStringLiteral("running");
            const QString toolName = event.toolName.trimmed();
            if (!toolName.isEmpty() && !runtime->childTools.contains(toolName))
                runtime->childTools.append(toolName);

            if (event.status == QLatin1String("started")) {
                ++runtime->childToolStartedCount;
            } else if (event.status == QLatin1String("progress")) {
                ++runtime->childToolProgressCount;
            } else if (event.status == QLatin1String("completed")) {
                ++runtime->childToolCompletedCount;
                if (event.success)
                    ++runtime->childToolSuccessCount;
                else
                    ++runtime->childToolFailureCount;
            }

            if (runtime->childTimeline.size() < kChildTimelineLimit) {
                QJsonObject row;
                row.insert(
                    QStringLiteral("timestamp"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                row.insert(QStringLiteral("status"), event.status.trimmed());
                if (!toolName.isEmpty())
                    row.insert(QStringLiteral("tool_name"), toolName);
                if (!event.toolId.trimmed().isEmpty())
                    row.insert(QStringLiteral("tool_id"), event.toolId.trimmed());
                if (event.status == QLatin1String("completed"))
                    row.insert(QStringLiteral("success"), event.success);
                const QString summary = summarizeTimelineEntry(event);
                if (!summary.isEmpty())
                    row.insert(QStringLiteral("summary"), summary);
                runtime->childTimeline.append(row);
                if (!summary.isEmpty())
                    runtime->summary = summary.left(240);
            } else {
                ++runtime->childTimelineDropped;
            }
        });

    QObject::connect(
        childAgent,
        &LLMAgent::streamDataReceived,
        childAgent,
        [this, runtime](const QString& chunk) {
            QWriteLocker locker(&m_lock);
            if (runtime->settled)
                return;
            runtime->lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
            runtime->stallNoticeEmitted = false;
            if (runtime->softTimeoutNotified)
                runtime->status = QStringLiteral("running");
            ++runtime->childStreamChunkCount;
            runtime->childStreamChars += chunk.size();
            runtime->summary = QStringLiteral("后台子代理执行中");
        });

    QObject::connect(
        childAgent,
        &LLMAgent::finished,
        childAgent,
        [this, runtime, maxResponseChars, taskForResult](const QString& content) {
            QString normalized = content.trimmed();
            const QString statusHint = extractStatusTag(normalized);
            normalized = ensureStructuredDelegateOutput(taskForResult, normalized, statusHint, nullptr);
            if (normalized.size() > maxResponseChars)
                normalized = normalized.left(maxResponseChars) + QStringLiteral("\n...[delegate response truncated]...");

            const QString childStatus = extractStatusTag(normalized);
            const bool blocked = isBlockedStatus(childStatus);
            settleAsyncJob(
                runtime,
                !blocked,
                normalized,
                blocked ? QStringLiteral("child_blocked") : QString());
        });

    QObject::connect(
        childAgent,
        &LLMAgent::errorOccurred,
        childAgent,
        [this, runtime](const QString& msg) {
            const QString err = msg.trimmed().isEmpty()
                ? QStringLiteral("sub-agent error")
                : msg.trimmed();
            settleAsyncJob(runtime, false, QString(), err);
        });

    QObject::connect(
        watchdog,
        &QTimer::timeout,
        childAgent,
        [this, runtime]() {
            if (!runtime)
                return;

            bool settled = false;
            qint64 startedAtMs = 0;
            qint64 lastProgressAtMs = 0;
            int hardTimeoutMs = 0;
            int expectedTimeoutMs = 0;
            int stallNoProgressMs = 0;
            bool softTimeoutNotified = false;
            bool stallNoticeEmitted = false;
            {
                QReadLocker locker(&m_lock);
                settled = runtime->settled;
                startedAtMs = runtime->startedAtMs;
                lastProgressAtMs = runtime->lastProgressAtMs;
                hardTimeoutMs = runtime->hardTimeoutMs;
                expectedTimeoutMs = runtime->expectedTimeoutMs;
                stallNoProgressMs = runtime->stallNoProgressMs;
                softTimeoutNotified = runtime->softTimeoutNotified;
                stallNoticeEmitted = runtime->stallNoticeEmitted;
            }
            if (settled)
                return;

            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 elapsedMs = nowMs - startedAtMs;
            const qint64 idleMs = nowMs - lastProgressAtMs;
            if (!softTimeoutNotified && elapsedMs >= expectedTimeoutMs) {
                QWriteLocker locker(&m_lock);
                if (!runtime->settled && !runtime->softTimeoutNotified) {
                    runtime->softTimeoutNotified = true;
                    runtime->stallNoticeEmitted = false;
                    runtime->status = QStringLiteral("soft_timeout");
                    runtime->summary = QStringLiteral("子代理超过预计时间，继续等待中");
                }
            }
            if (elapsedMs >= hardTimeoutMs) {
                if (runtime->childAgent)
                    runtime->childAgent->abort();
                settleAsyncJob(runtime, false, QString(), QStringLiteral("sub-agent hard timeout"));
                return;
            }
            if (softTimeoutNotified && idleMs >= stallNoProgressMs && !stallNoticeEmitted) {
                QWriteLocker locker(&m_lock);
                if (!runtime->settled
                    && runtime->softTimeoutNotified
                    && !runtime->stallNoticeEmitted) {
                    runtime->stallNoticeEmitted = true;
                    runtime->status = QStringLiteral("soft_timeout");
                    runtime->summary = QStringLiteral("子代理长时间无新进展，继续等待中（可取消）");
                }
            }
        });

    childAgent->askOnce(executionPrompt);
    watchdog->start();

    data.insert(QStringLiteral("status"), QStringLiteral("accepted"));
    data.insert(QStringLiteral("job_id"), runtime->jobId);
    data.insert(QStringLiteral("owner_agent_id"), runtime->ownerAgentId);
    data.insert(QStringLiteral("expected_timeout_ms"), softTimeoutMs);
    data.insert(QStringLiteral("hard_timeout_ms"), hardTimeoutMs);
    data.insert(QStringLiteral("stall_no_progress_ms"), stallNoProgressMs);
    data.insert(QStringLiteral("child_agent_id"), runtime->childAgentId);
    data.insert(QStringLiteral("child_model"), runtime->childModel);

    result.success = true;
    result.rawResult = QStringLiteral(
                           "后台子代理任务已启动。\n"
                           "job_id: %1\n"
                           "说明: 可使用 delegate_status 查看进度，delegate_cancel 取消任务。")
                           .arg(runtime->jobId);
    result.userSummary = QStringLiteral("后台子代理任务已启动");
    result.data = data;
    return result;
}

bool DelegateTaskScheduler::queryJob(const QString& jobId, const QString& ownerAgentId, JobInfo* info) const
{
    if (info)
        *info = JobInfo();
    const QString id = jobId.trimmed();
    if (id.isEmpty())
        return false;

    QReadLocker locker(&m_lock);
    const QSharedPointer<AsyncJobRuntime> runtime = m_asyncJobs.value(id);
    if (!runtime)
        return false;

    const QString owner = ownerAgentId.trimmed();
    if (!owner.isEmpty() && runtime->ownerAgentId != owner)
        return false;

    if (info)
        *info = toJobInfo(runtime);
    return true;
}

QList<DelegateTaskScheduler::JobInfo> DelegateTaskScheduler::listJobs(const QString& ownerAgentId, bool activeOnly, int limit) const
{
    QList<JobInfo> out;
    const QString owner = ownerAgentId.trimmed();
    const int safeLimit = qBound(1, limit, 200);

    QReadLocker locker(&m_lock);
    for (int i = m_asyncJobOrder.size() - 1; i >= 0; --i) {
        const QString jobId = m_asyncJobOrder.at(i);
        const QSharedPointer<AsyncJobRuntime> runtime = m_asyncJobs.value(jobId);
        if (!runtime)
            continue;
        if (!owner.isEmpty() && runtime->ownerAgentId != owner)
            continue;
        if (activeOnly) {
            const QString status = runtime->status;
            const bool active = status == QLatin1String("running")
                || status == QLatin1String("queued")
                || status == QLatin1String("soft_timeout");
            if (!active)
                continue;
        }
        out.append(toJobInfo(runtime));
        if (out.size() >= safeLimit)
            break;
    }
    return out;
}

bool DelegateTaskScheduler::cancelJob(const QString& jobId, const QString& ownerAgentId, QString* error)
{
    if (error)
        error->clear();
    const QString id = jobId.trimmed();
    if (id.isEmpty()) {
        if (error)
            *error = QStringLiteral("missing job_id");
        return false;
    }

    QSharedPointer<AsyncJobRuntime> runtime;
    {
        QWriteLocker locker(&m_lock);
        runtime = m_asyncJobs.value(id);
        if (!runtime) {
            if (error)
                *error = QStringLiteral("job not found");
            return false;
        }
        const QString owner = ownerAgentId.trimmed();
        if (!owner.isEmpty() && runtime->ownerAgentId != owner) {
            if (error)
                *error = QStringLiteral("permission denied");
            return false;
        }
        if (runtime->settled) {
            if (error)
                *error = QStringLiteral("job already finished");
            return false;
        }
        runtime->cancelRequested = true;
    }

    if (runtime->childAgent)
        runtime->childAgent->abort();
    settleAsyncJob(runtime, false, QString(), QStringLiteral("cancelled by user"));
    return true;
}

QList<DelegateTaskScheduler::Snapshot> DelegateTaskScheduler::activeTasks() const
{
    QList<Snapshot> out;
    QReadLocker locker(&m_lock);
    for (const QString& taskId : m_snapshotOrder) {
        const Snapshot s = m_snapshots.value(taskId);
        if (s.status == QLatin1String("running")
            || s.status == QLatin1String("soft_timeout")) {
            out.append(s);
        }
    }
    return out;
}

void DelegateTaskScheduler::upsertSnapshot(const Snapshot& snapshot)
{
    if (snapshot.taskId.trimmed().isEmpty())
        return;

    QWriteLocker locker(&m_lock);
    const QString taskId = snapshot.taskId.trimmed();
    if (!m_snapshots.contains(taskId))
        m_snapshotOrder.append(taskId);
    m_snapshots.insert(taskId, snapshot);
    pruneSnapshotsLocked();
}

void DelegateTaskScheduler::pruneSnapshotsLocked()
{
    if (m_snapshotOrder.size() <= kMaxSnapshots)
        return;

    auto isFinished = [](const Snapshot& s) {
        return s.status == QLatin1String("completed")
            || s.status == QLatin1String("failed")
            || s.status == QLatin1String("cancelled");
    };

    while (m_snapshotOrder.size() > kMaxSnapshots) {
        int removeIndex = -1;
        for (int i = 0; i < m_snapshotOrder.size(); ++i) {
            const Snapshot s = m_snapshots.value(m_snapshotOrder.at(i));
            if (isFinished(s)) {
                removeIndex = i;
                break;
            }
        }
        if (removeIndex < 0)
            removeIndex = 0;
        const QString key = m_snapshotOrder.takeAt(removeIndex);
        m_snapshots.remove(key);
    }
}

QJsonObject DelegateTaskScheduler::collectChildRunData(const QJsonArray& ioHistory)
{
    QJsonObject out;
    if (ioHistory.isEmpty())
        return out;

    QString latestRequestId;
    QString finishReason;
    QString errorMessage;
    int responseToolCallBatches = 0;
    int responseToolCallTotal = 0;
    int lastRequestMessagesCount = 0;

    for (const QJsonValue& value : ioHistory) {
        const QJsonObject entry = value.toObject();
        const QString requestId = entry.value(QStringLiteral("request_id")).toString().trimmed();
        if (!requestId.isEmpty())
            latestRequestId = requestId;

        const QJsonObject requestObj = entry.value(QStringLiteral("request")).toObject();
        const QJsonArray requestMessages = requestObj.value(QStringLiteral("messages")).toArray();
        if (!requestMessages.isEmpty())
            lastRequestMessagesCount = requestMessages.size();

        const QJsonObject response = entry.value(QStringLiteral("response")).toObject();
        const QJsonArray choices = response.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QJsonObject firstChoice = choices.at(0).toObject();
            const QJsonObject message = firstChoice.value(QStringLiteral("message")).toObject();
            const QJsonArray toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
            if (!toolCalls.isEmpty()) {
                ++responseToolCallBatches;
                responseToolCallTotal += toolCalls.size();
            }
            const QString reason = firstChoice.value(QStringLiteral("finish_reason"))
                                       .toString()
                                       .trimmed();
            if (!reason.isEmpty())
                finishReason = reason;
        }

        const QString err = entry.value(QStringLiteral("error"))
                                .toObject()
                                .value(QStringLiteral("message"))
                                .toString()
                                .trimmed();
        if (!err.isEmpty())
            errorMessage = err;
    }

    if (!latestRequestId.isEmpty()) {
        out.insert(QStringLiteral("child_request_id"), latestRequestId);
        out.insert(QStringLiteral("child_trace_id"), latestRequestId);
    }
    if (!finishReason.isEmpty())
        out.insert(QStringLiteral("child_finish_reason"), finishReason);
    if (!errorMessage.isEmpty())
        out.insert(QStringLiteral("child_error"), errorMessage);
    out.insert(QStringLiteral("child_io_entries"), ioHistory.size());
    out.insert(QStringLiteral("child_response_tool_call_batches"), responseToolCallBatches);
    out.insert(QStringLiteral("child_response_tool_call_total"), responseToolCallTotal);
    if (lastRequestMessagesCount > 0)
        out.insert(QStringLiteral("child_last_request_messages_count"), lastRequestMessagesCount);
    return out;
}

QString DelegateTaskScheduler::summarizeTimelineEntry(const ToolExecutionEvent& event)
{
    QString summary = event.formattedResult.trimmed();
    if (summary.isEmpty())
        summary = event.rawResult.trimmed();
    if (summary.size() > 200)
        summary = summary.left(200) + QStringLiteral("...");
    return summary;
}

int DelegateTaskScheduler::calcHardTimeoutMs(int softTimeoutMs)
{
    const int base = std::max(softTimeoutMs + 15000, std::max(softTimeoutMs * 2, softTimeoutMs + 60000));
    return qBound(softTimeoutMs + 5000, base, kMaxHardTimeoutMs);
}

int DelegateTaskScheduler::calcStallNoProgressMs(int softTimeoutMs)
{
    return qBound(10000, softTimeoutMs / 2, 120000);
}

bool DelegateTaskScheduler::isChildGuarded(const QString& childFinishReason, const QString& finalResult)
{
    return (childFinishReason == QLatin1String("tool_loop_guard"))
        || finalResult.contains(QStringLiteral("[熔断]"))
        || finalResult.contains(QStringLiteral("本轮已触发保护性停止"));
}
