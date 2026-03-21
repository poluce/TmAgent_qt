#include "DelegateTaskScheduler.h"
#include "LLMAgent.h"
#include "ToolDispatcher.h"
#include "CodexAppServerClient.h"
#include "core/utils/DefaultPrompts.h"
#include "llm/ModelFactory.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
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
static constexpr int kMaxAsyncJobs = 512;
static constexpr int kStructuredRawOutputMaxChars = 1200;
static constexpr int kProgressForwardMinIntervalMs = 1500;
static constexpr int kMaxSameToolSummaryRepeats = 4;
static constexpr int kMaxConsecutiveToolFailures = 5;

QString truncateForData(const QString& text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars) + QStringLiteral("\n...[truncated]...");
}

QString buildExecutionPrompt(const QString& task)
{
    QString prompt = QStringLiteral("请执行以下任务，并在必要时使用工具。\n任务：\n%1\n").arg(task);
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

QString normalizeDelegateBackend(const QString& rawBackend)
{
    const QString lowered = rawBackend.trimmed().toLower();
    if (lowered == QLatin1String("codex"))
        return QStringLiteral("codex");
    return QStringLiteral("tmagent");
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
    child.systemPrompt = DefaultPrompts::ensureWorkerExecutionDiscipline(rolePrompt);

    child.userName = request.delegateToolName.trimmed().isEmpty()
        ? QStringLiteral("delegate_task")
        : request.delegateToolName.trimmed();
    child.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);

    if (data) {
        data->insert(QStringLiteral("child_agent_id"), child.uuid);
        const QString childModelId = ModelFactory::instance()->resolveModelId(child);
        if (!childModelId.isEmpty())
            data->insert(QStringLiteral("child_model"), childModelId);
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
    QString backend;
    QString summary;
    QString failureReason;
    QString task;
    QString result;
    QString backendThreadId;
    QString backendTurnId;
    QString backendProgram;
    QString accumulatedText;

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
    QPointer<CodexAppServerClient> codexClient;
    QPointer<QTimer> watchdog;
    QString initializeRequestId;
    QString threadStartRequestId;
    QString turnStartRequestId;
};

DelegateTaskScheduler::DelegateTaskScheduler(QObject* parent)
    : QObject(parent)
{
}

DelegateTaskScheduler* DelegateTaskScheduler::instance()
{
    static DelegateTaskScheduler scheduler(nullptr);
    return &scheduler;
}

DelegateTaskScheduler::JobInfo DelegateTaskScheduler::toJobInfo(const QSharedPointer<AsyncJobRuntime>& runtime)
{
    JobInfo info;
    if (!runtime)
        return info;

    info.jobId = runtime->jobId;
    info.ownerAgentId = runtime->ownerAgentId;
    info.status = runtime->status;
    info.backend = runtime->backend;
    info.summary = runtime->summary;
    info.failureReason = runtime->failureReason;
    info.task = runtime->task;
    info.result = runtime->result;
    info.backendThreadId = runtime->backendThreadId;
    info.backendTurnId = runtime->backendTurnId;
    info.backendProgram = runtime->backendProgram;
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
    QPointer<CodexAppServerClient> codexClient;
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
            const bool codexBackend = (runtime->backend == QLatin1String("codex"));
            if (childStatus == QLatin1String("PARTIAL")) {
                runtime->summary = codexBackend
                    ? QStringLiteral("Codex 子代理任务部分完成")
                    : QStringLiteral("后台子代理任务部分完成");
            } else {
                runtime->summary = codexBackend
                    ? QStringLiteral("Codex 子代理任务完成")
                    : QStringLiteral("后台子代理任务完成");
            }
            runtime->failureReason.clear();
        } else {
            runtime->status = runtime->cancelRequested
                ? QStringLiteral("cancelled")
                : QStringLiteral("failed");
            const bool codexBackend = (runtime->backend == QLatin1String("codex"));
            runtime->summary = runtime->cancelRequested
                ? (codexBackend ? QStringLiteral("Codex 子代理任务已取消")
                                : QStringLiteral("后台子代理任务已取消"))
                : (codexBackend ? QStringLiteral("Codex 子代理任务失败")
                                : QStringLiteral("后台子代理任务失败"));
            runtime->failureReason = failureReason.trimmed();
        }
        watchdog = runtime->watchdog;
        childAgent = runtime->childAgent;
        codexClient = runtime->codexClient;
        runtime->watchdog = nullptr;
        runtime->childAgent = nullptr;
        runtime->codexClient = nullptr;
    }

    if (watchdog) {
        watchdog->stop();
        watchdog->deleteLater();
    }
    if (childAgent) {
        childAgent->deleteLater();
    }
    if (codexClient) {
        codexClient->shutdown();
        codexClient->deleteLater();
    }

    // P0: 自动通知父 Agent
    emit jobSettled(runtime->jobId, runtime->ownerAgentId, success, result);
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

    // P0: 并发 Guards —— 检查全局活跃 Job 数
    if (activeJobCount() >= kMaxConcurrentAsyncJobs) {
        data.insert(QStringLiteral("status"), QStringLiteral("failed"));
        data.insert(QStringLiteral("failure_reason"), QStringLiteral("concurrent_limit_reached"));
        result.success = false;
        result.rawResult = QStringLiteral("错误: 后台子代理并发数已达上限(%1)，请等待现有任务完成")
                               .arg(kMaxConcurrentAsyncJobs);
        result.userSummary = QStringLiteral("后台子代理任务提交失败：并发上限");
        result.data = data;
        return result;
    }

    const int softTimeoutMs = qBound(kMinExpectedTimeoutMs, request.expectedTimeoutMs, kMaxExpectedTimeoutMs);
    const int hardTimeoutMs = calcHardTimeoutMs(softTimeoutMs);
    const int stallNoProgressMs = calcStallNoProgressMs(softTimeoutMs);
    const int maxResponseChars = qBound(500, request.maxResponseChars, 20000);
    const QString normalizedOwnerId = ownerAgentId.trimmed();
    const QString backend = normalizeDelegateBackend(request.backend);
    LLMConfig childConfig = buildChildConfig(request, &data);
    const QString executionPrompt = buildExecutionPrompt(task);

    const QSharedPointer<AsyncJobRuntime> runtime(new AsyncJobRuntime());
    runtime->jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    runtime->ownerAgentId = normalizedOwnerId;
    runtime->status = QStringLiteral("running");
    runtime->backend = backend;
    runtime->summary = backend == QLatin1String("codex")
        ? QStringLiteral("Codex 子代理任务已启动")
        : QStringLiteral("后台子代理任务已启动");
    runtime->task = task.left(4000);
    runtime->createdAtMs = QDateTime::currentMSecsSinceEpoch();
    runtime->startedAtMs = runtime->createdAtMs;
    runtime->lastProgressAtMs = runtime->createdAtMs;
    runtime->expectedTimeoutMs = softTimeoutMs;
    runtime->hardTimeoutMs = hardTimeoutMs;
    runtime->stallNoProgressMs = stallNoProgressMs;
    runtime->maxResponseChars = maxResponseChars;
    runtime->childAgentId = childConfig.uuid;
    runtime->childModel = backend == QLatin1String("codex")
        ? QStringLiteral("codex-app-server")
        : ModelFactory::instance()->resolveModelId(childConfig);
    const QString taskForResult = runtime->task;
    data.insert(QStringLiteral("backend"), backend);

    if (backend == QLatin1String("codex")) {
        CodexAppServerClient* codexClient = new CodexAppServerClient(QCoreApplication::instance());
        CodexAppServerClient::LaunchOptions launch = CodexAppServerClient::defaultLaunchOptions();
        launch.clientName = QStringLiteral("tmagent-delegate");
        launch.clientTitle = QStringLiteral("TmAgent Delegate");
        launch.workingDirectory = childConfig.workspaceDir.trimmed().isEmpty()
            ? QDir::currentPath()
            : childConfig.workspaceDir.trimmed();
        launch.optOutNotificationMethods.clear();
        codexClient->setLaunchOptions(launch);

        runtime->codexClient = codexClient;
        runtime->backendProgram = codexClient->programDisplayName();

        QTimer* codexWatchdog = new QTimer(QCoreApplication::instance());
        codexWatchdog->setInterval(1000);
        codexWatchdog->setSingleShot(false);
        runtime->watchdog = codexWatchdog;

        {
            QWriteLocker locker(&m_lock);
            m_asyncJobs.insert(runtime->jobId, runtime);
            m_asyncJobOrder.append(runtime->jobId);
            pruneJobsLocked();
        }

        QObject::connect(
            codexClient,
            &CodexAppServerClient::started,
            codexClient,
            [runtime, codexClient]() {
                runtime->initializeRequestId = codexClient->requestInitialize();
            });

        QObject::connect(
            codexClient,
            &CodexAppServerClient::responseReceived,
            codexClient,
            [this, runtime, codexClient, executionPrompt, childConfig](const QString& requestId, const QJsonValue& resultValue) {
                QWriteLocker locker(&m_lock);
                if (runtime->settled)
                    return;

                runtime->lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
                runtime->stallNoticeEmitted = false;
                if (runtime->softTimeoutNotified)
                    runtime->status = QStringLiteral("running");

                if (requestId == runtime->initializeRequestId) {
                    codexClient->completeInitializeHandshake();

                    QJsonObject threadOverrides;
                    threadOverrides.insert(QStringLiteral("approvalPolicy"), QStringLiteral("never"));
                    threadOverrides.insert(QStringLiteral("sandbox"), QStringLiteral("danger-full-access"));
                    threadOverrides.insert(QStringLiteral("developerInstructions"), childConfig.systemPrompt);
                    threadOverrides.insert(QStringLiteral("serviceName"), QStringLiteral("TmAgent Codex Delegate"));
                    runtime->threadStartRequestId = codexClient->requestThreadStart(threadOverrides);
                    runtime->summary = QStringLiteral("正在建立 Codex 子代理线程");
                    return;
                }

                if (requestId == runtime->threadStartRequestId) {
                    const QJsonObject thread = resultValue.toObject().value(QStringLiteral("thread")).toObject();
                    runtime->backendThreadId = thread.value(QStringLiteral("id")).toString().trimmed();
                    if (runtime->backendThreadId.isEmpty()) {
                        locker.unlock();
                        settleAsyncJob(runtime, false, QString(), QStringLiteral("codex thread/start missing thread id"));
                        return;
                    }
                    runtime->summary = runtime->backendThreadId.isEmpty()
                        ? QStringLiteral("Codex 子代理线程已建立")
                        : QStringLiteral("Codex 子代理线程已建立(%1)").arg(runtime->backendThreadId.left(8));
                    runtime->turnStartRequestId = codexClient->requestTurnStartText(runtime->backendThreadId, executionPrompt);
                    return;
                }

                if (requestId == runtime->turnStartRequestId) {
                    const QJsonObject turn = resultValue.toObject().value(QStringLiteral("turn")).toObject();
                    runtime->backendTurnId = turn.value(QStringLiteral("id")).toString().trimmed();
                    runtime->summary = QStringLiteral("Codex 子代理执行中");
                }
            });

        QObject::connect(
            codexClient,
            &CodexAppServerClient::responseErrorReceived,
            codexClient,
            [this, runtime](const QString& requestId, int code, const QString& message, const QJsonObject&) {
                if (!runtime || runtime->settled)
                    return;
                if (requestId == runtime->initializeRequestId
                    || requestId == runtime->threadStartRequestId
                    || requestId == runtime->turnStartRequestId) {
                    settleAsyncJob(
                        runtime,
                        false,
                        QString(),
                        QStringLiteral("codex rpc error [%1] %2").arg(code).arg(message));
                }
            });

        QObject::connect(
            codexClient,
            &CodexAppServerClient::assistantMessageDelta,
            codexClient,
            [this, runtime](const QString&, const QString&, const QString&, const QString& delta) {
                QWriteLocker locker(&m_lock);
                if (runtime->settled)
                    return;
                runtime->lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
                runtime->stallNoticeEmitted = false;
                runtime->accumulatedText += delta;
                ++runtime->childStreamChunkCount;
                runtime->childStreamChars += delta.size();
                runtime->summary = QStringLiteral("Codex 子代理正在回复");
            });

        QObject::connect(
            codexClient,
            &CodexAppServerClient::assistantMessageCompleted,
            codexClient,
            [this, runtime](const QString&, const QString&, const QString&, const QString& text) {
                QWriteLocker locker(&m_lock);
                if (runtime->settled)
                    return;
                runtime->lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
                runtime->stallNoticeEmitted = false;
                if (runtime->accumulatedText.trimmed().isEmpty()) {
                    runtime->accumulatedText = text;
                } else if (text.size() > runtime->accumulatedText.size()
                           && text.contains(runtime->accumulatedText)) {
                    runtime->accumulatedText = text;
                }
                runtime->summary = QStringLiteral("Codex 子代理已生成回复");
            });

        QObject::connect(
            codexClient,
            &CodexAppServerClient::commandExecutionApprovalRequested,
            codexClient,
            [this, runtime, codexClient](const QString& requestId,
                                         const QString&,
                                         const QString&,
                                         const QString&,
                                         const QString& command,
                                         const QString& cwd,
                                         const QString& reason,
                                         const QStringList&) {
                QWriteLocker locker(&m_lock);
                if (runtime->settled)
                    return;
                runtime->lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
                runtime->summary = QStringLiteral("Codex 请求命令执行权限，已自动放行");
                if (runtime->childTimeline.size() < kChildTimelineLimit) {
                    QJsonObject row;
                    row.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                    row.insert(QStringLiteral("status"), QStringLiteral("approval_auto_accepted"));
                    row.insert(QStringLiteral("summary"), !reason.trimmed().isEmpty() ? reason : command.left(160));
                    if (!command.trimmed().isEmpty())
                        row.insert(QStringLiteral("command"), command.left(200));
                    if (!cwd.trimmed().isEmpty())
                        row.insert(QStringLiteral("cwd"), cwd.left(160));
                    runtime->childTimeline.append(row);
                }
                QJsonObject response;
                response.insert(QStringLiteral("decision"), QStringLiteral("acceptForSession"));
                codexClient->sendServerRequestResult(requestId, response);
            });

        QObject::connect(
            codexClient,
            &CodexAppServerClient::fileChangeApprovalRequested,
            codexClient,
            [this, runtime, codexClient](const QString& requestId,
                                         const QString&,
                                         const QString&,
                                         const QString&,
                                         const QString& reason,
                                         const QString& grantRoot) {
                QWriteLocker locker(&m_lock);
                if (runtime->settled)
                    return;
                runtime->lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
                runtime->summary = QStringLiteral("Codex 请求文件改动权限，已自动放行");
                if (runtime->childTimeline.size() < kChildTimelineLimit) {
                    QJsonObject row;
                    row.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                    row.insert(QStringLiteral("status"), QStringLiteral("file_change_auto_accepted"));
                    row.insert(QStringLiteral("summary"), !reason.trimmed().isEmpty() ? reason : grantRoot.left(160));
                    runtime->childTimeline.append(row);
                }
                QJsonObject response;
                response.insert(QStringLiteral("decision"), QStringLiteral("acceptForSession"));
                codexClient->sendServerRequestResult(requestId, response);
            });

        QObject::connect(
            codexClient,
            &CodexAppServerClient::serverRequestReceived,
            codexClient,
            [this, runtime, codexClient](const QString& requestId, const QString& method, const QJsonValue&) {
                if (!runtime || runtime->settled)
                    return;
                if (method == QLatin1String("item/commandExecution/requestApproval")
                    || method == QLatin1String("item/fileChange/requestApproval")) {
                    return;
                }
                codexClient->sendServerRequestError(
                    requestId,
                    -32601,
                    QStringLiteral("TmAgent delegate backend 暂不支持该 Codex server request"));
                settleAsyncJob(runtime, false, QString(), QStringLiteral("unsupported codex server request: %1").arg(method));
            });

        QObject::connect(
            codexClient,
            &CodexAppServerClient::turnCompleted,
            codexClient,
            [this, runtime, taskForResult, maxResponseChars](const QString&, const QString& turnId, const QString& status, const QJsonObject& error) {
                QString normalized;
                {
                    QWriteLocker locker(&m_lock);
                    if (runtime->settled)
                        return;
                    runtime->backendTurnId = turnId.trimmed();
                    runtime->lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
                    normalized = runtime->accumulatedText.trimmed();
                }

                if (status == QLatin1String("failed")) {
                    const QString message = error.value(QStringLiteral("message")).toString().trimmed();
                    settleAsyncJob(runtime, false, QString(), message.isEmpty() ? QStringLiteral("codex turn failed") : message);
                    return;
                }

                if (status == QLatin1String("interrupted")) {
                    settleAsyncJob(runtime, false, QString(), QStringLiteral("codex turn interrupted"));
                    return;
                }

                normalized = ensureStructuredDelegateOutput(taskForResult, normalized, QStringLiteral("COMPLETED"), nullptr);
                normalized.prepend(QStringLiteral("[Codex Delegate Report]\n"));
                if (normalized.size() > maxResponseChars)
                    normalized = normalized.left(maxResponseChars) + QStringLiteral("\n...[delegate response truncated]...");
                settleAsyncJob(runtime, true, normalized, QString());
            });

        QObject::connect(
            codexClient,
            &CodexAppServerClient::transportError,
            codexClient,
            [this, runtime](const QString& message) {
                if (!runtime || runtime->settled)
                    return;
                settleAsyncJob(runtime, false, QString(), message.trimmed().isEmpty() ? QStringLiteral("codex transport error") : message.trimmed());
            });

        QObject::connect(
            codexWatchdog,
            &QTimer::timeout,
            codexClient,
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
                        runtime->summary = QStringLiteral("Codex 子代理超过预计时间，继续等待中");
                    }
                    emit jobStatusChanged(runtime->jobId, runtime->ownerAgentId, runtime->status, runtime->summary);
                }
                if (elapsedMs >= hardTimeoutMs) {
                    if (runtime->codexClient)
                        runtime->codexClient->shutdown();
                    settleAsyncJob(runtime, false, QString(), QStringLiteral("codex hard timeout"));
                    return;
                }
                if (softTimeoutNotified && idleMs >= stallNoProgressMs && !stallNoticeEmitted) {
                    QWriteLocker locker(&m_lock);
                    if (!runtime->settled
                        && runtime->softTimeoutNotified
                        && !runtime->stallNoticeEmitted) {
                        runtime->stallNoticeEmitted = true;
                        runtime->status = QStringLiteral("soft_timeout");
                        runtime->summary = QStringLiteral("Codex 子代理长时间无新进展，继续等待中（可取消）");
                    }
                    emit jobStatusChanged(runtime->jobId, runtime->ownerAgentId, runtime->status, runtime->summary);
                }
            });

        codexClient->start();
        codexWatchdog->start();

        data.insert(QStringLiteral("status"), QStringLiteral("accepted"));
        data.insert(QStringLiteral("job_id"), runtime->jobId);
        data.insert(QStringLiteral("owner_agent_id"), runtime->ownerAgentId);
        data.insert(QStringLiteral("expected_timeout_ms"), softTimeoutMs);
        data.insert(QStringLiteral("hard_timeout_ms"), hardTimeoutMs);
        data.insert(QStringLiteral("stall_no_progress_ms"), stallNoProgressMs);
        data.insert(QStringLiteral("child_agent_id"), runtime->childAgentId);
        data.insert(QStringLiteral("child_model"), runtime->childModel);
        if (!runtime->backendProgram.trimmed().isEmpty())
            data.insert(QStringLiteral("backend_program"), runtime->backendProgram.trimmed());

        result.success = true;
        result.rawResult = QStringLiteral(
                               "Codex 子代理任务已启动。\n"
                               "job_id: %1\n"
                               "backend: codex\n"
                               "说明: 可使用 delegate_status 查看进度，delegate_cancel 取消任务。")
                               .arg(runtime->jobId);
        result.userSummary = QStringLiteral("Codex 子代理任务已启动");
        result.data = data;
        return result;
    }

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
                // P2: 状态变化通知
                emit jobStatusChanged(runtime->jobId, runtime->ownerAgentId, runtime->status, runtime->summary);
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
                // P2: 状态变化通知
                emit jobStatusChanged(runtime->jobId, runtime->ownerAgentId, runtime->status, runtime->summary);
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
    if (runtime->codexClient)
        runtime->codexClient->shutdown();
    settleAsyncJob(runtime, false, QString(), QStringLiteral("cancelled by user"));
    return true;
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

// P1: 格式化活跃 Job 列表供注入到 system prompt
QString DelegateTaskScheduler::formatActiveJobsContext(const QString& ownerAgentId) const
{
    const QList<JobInfo> jobs = listJobs(ownerAgentId, true, 10);
    if (jobs.isEmpty())
        return QString();

    QString ctx = QStringLiteral("## Active Sub-Agent Jobs\n");
    for (const JobInfo& job : jobs) {
        QString line = QStringLiteral("- [%1] %2").arg(job.jobId.left(8), job.status);
        if (!job.backend.trimmed().isEmpty())
            line += QStringLiteral(" | backend=%1").arg(job.backend.trimmed());
        if (!job.backendThreadId.trimmed().isEmpty())
            line += QStringLiteral(" | thread=%1").arg(job.backendThreadId.left(12));
        if (!job.summary.trimmed().isEmpty())
            line += QStringLiteral(" | %1").arg(job.summary.left(80));
        ctx += line + QStringLiteral("\n");
    }
    return ctx;
}

// P0: 统计全局活跃 Job 数（用于并发 Guards）
int DelegateTaskScheduler::activeJobCount() const
{
    QReadLocker locker(&m_lock);
    int count = 0;
    for (auto it = m_asyncJobs.constBegin(); it != m_asyncJobs.constEnd(); ++it) {
        const QSharedPointer<AsyncJobRuntime>& runtime = it.value();
        if (!runtime || runtime->settled)
            continue;
        ++count;
    }
    return count;
}
