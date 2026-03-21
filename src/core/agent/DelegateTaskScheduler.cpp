#include "DelegateTaskScheduler.h"

#include "delegate/CodexDelegateBackend.h"
#include "delegate/DelegateBackendSupport.h"
#include "delegate/IDelegateBackend.h"
#include "delegate/TmagentDelegateBackend.h"
#include "core/utils/DefaultPrompts.h"
#include "llm/ModelFactory.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QReadLocker>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <algorithm>

using DelegateBackendInternal::DelegateBackendCallbacks;
using DelegateBackendInternal::DelegateBackendStartRequest;
using DelegateBackendInternal::IDelegateBackend;
using DelegateBackendInternal::IDelegateBackendSession;

namespace {

static constexpr int kMinExpectedTimeoutMs = 2000;
static constexpr int kMaxExpectedTimeoutMs = 300000;
static constexpr int kMaxHardTimeoutMs = 900000;
static constexpr int kChildTimelineLimit = 32;
static constexpr int kMaxAsyncJobs = 512;

int calcHardTimeoutValue(int softTimeoutMs)
{
    const int base = std::max(softTimeoutMs + 15000, std::max(softTimeoutMs * 2, softTimeoutMs + 60000));
    return qBound(softTimeoutMs + 5000, base, kMaxHardTimeoutMs);
}

int calcStallNoProgressValue(int softTimeoutMs)
{
    return qBound(10000, softTimeoutMs / 2, 120000);
}

bool isCodexBackend(const QString& backend)
{
    return backend == QLatin1String("codex");
}

QString truncateSummary(const QString& text, int maxChars = 240)
{
    const QString trimmed = text.trimmed();
    if (trimmed.size() <= maxChars)
        return trimmed;
    return trimmed.left(maxChars);
}

QString successSummaryForBackend(const QString& backend, bool partial)
{
    if (isCodexBackend(backend)) {
        return partial
            ? QStringLiteral("Codex 子代理任务部分完成")
            : QStringLiteral("Codex 子代理任务完成");
    }
    return partial
        ? QStringLiteral("后台子代理任务部分完成")
        : QStringLiteral("后台子代理任务完成");
}

QString failureSummaryForBackend(const QString& backend, bool cancelled)
{
    if (cancelled) {
        return isCodexBackend(backend)
            ? QStringLiteral("Codex 子代理任务已取消")
            : QStringLiteral("后台子代理任务已取消");
    }
    return isCodexBackend(backend)
        ? QStringLiteral("Codex 子代理任务失败")
        : QStringLiteral("后台子代理任务失败");
}

QString softTimeoutSummaryForBackend(const QString& backend, bool stalled)
{
    if (isCodexBackend(backend)) {
        return stalled
            ? QStringLiteral("Codex 子代理长时间无新进展，继续等待中（可取消）")
            : QStringLiteral("Codex 子代理超过预计时间，继续等待中");
    }
    return stalled
        ? QStringLiteral("子代理长时间无新进展，继续等待中（可取消）")
        : QStringLiteral("子代理超过预计时间，继续等待中");
}

QString hardTimeoutReasonForBackend(const QString& backend)
{
    return isCodexBackend(backend)
        ? QStringLiteral("codex hard timeout")
        : QStringLiteral("sub-agent hard timeout");
}

DelegateTaskScheduler::Result makeRejectedResult(
    QJsonObject data,
    const QString& failureReason,
    const QString& rawResult,
    const QString& userSummary)
{
    DelegateTaskScheduler::Result result;
    data.insert(QStringLiteral("status"), QStringLiteral("failed"));
    data.insert(QStringLiteral("failure_reason"), failureReason);
    result.success = false;
    result.rawResult = rawResult;
    result.userSummary = userSummary;
    result.data = data;
    return result;
}

bool validateSubmitRequest(
    DelegateTaskScheduler* scheduler,
    const DelegateTaskScheduler::Request& request,
    const QString& task,
    const QJsonObject& data,
    DelegateTaskScheduler::Result* rejected)
{
    if (task.isEmpty()) {
        *rejected = makeRejectedResult(
            data,
            QStringLiteral("missing_task"),
            QStringLiteral("错误: delegate_task 必须显式提供非空 task"),
            QStringLiteral("后台子代理任务提交失败：委派参数缺失"));
        return false;
    }

    if (request.parentConfig.recursionDepth <= 0) {
        *rejected = makeRejectedResult(
            data,
            QStringLiteral("recursion_depth_exhausted"),
            QStringLiteral("错误: 当前递归深度已耗尽，不能继续委派"),
            QStringLiteral("后台子代理任务提交失败：递归深度不足"));
        return false;
    }

    if (scheduler->activeJobCount() >= DelegateTaskScheduler::kMaxConcurrentAsyncJobs) {
        *rejected = makeRejectedResult(
            data,
            QStringLiteral("concurrent_limit_reached"),
            QStringLiteral("错误: 后台子代理并发数已达上限(%1)，请等待现有任务完成")
                .arg(DelegateTaskScheduler::kMaxConcurrentAsyncJobs),
            QStringLiteral("后台子代理任务提交失败：并发上限"));
        return false;
    }

    return true;
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

struct PreparedSubmit {
    QString task;
    QString normalizedOwnerId;
    QString backend;
    int softTimeoutMs = 0;
    int hardTimeoutMs = 0;
    int stallNoProgressMs = 0;
    int maxResponseChars = 0;
    LLMConfig childConfig;
    QString executionPrompt;
    QJsonObject data;
};

PreparedSubmit prepareSubmit(
    const DelegateTaskScheduler::Request& request,
    const QString& ownerAgentId)
{
    PreparedSubmit prepared;
    prepared.task = request.task.trimmed();
    prepared.normalizedOwnerId = ownerAgentId.trimmed();
    prepared.softTimeoutMs = qBound(kMinExpectedTimeoutMs, request.expectedTimeoutMs, kMaxExpectedTimeoutMs);
    prepared.hardTimeoutMs = calcHardTimeoutValue(prepared.softTimeoutMs);
    prepared.stallNoProgressMs = calcStallNoProgressValue(prepared.softTimeoutMs);
    prepared.maxResponseChars = qBound(500, request.maxResponseChars, 20000);
    prepared.backend = DelegateBackendInternal::normalizeDelegateBackend(request.backend);
    prepared.data.insert(
        QStringLiteral("delegate_tool"),
        request.delegateToolName.trimmed().isEmpty()
            ? QStringLiteral("delegate_task")
            : request.delegateToolName.trimmed());
    prepared.data.insert(
        QStringLiteral("requested_at"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    prepared.data.insert(QStringLiteral("backend"), prepared.backend);
    prepared.childConfig = buildChildConfig(request, &prepared.data);
    prepared.executionPrompt = DelegateBackendInternal::buildExecutionPrompt(prepared.task);
    return prepared;
}

bool isFinishedStatus(const QString& status)
{
    return status == QLatin1String("completed")
        || status == QLatin1String("failed")
        || status == QLatin1String("cancelled");
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

    bool settled = false;
    bool cancelRequested = false;
    bool softTimeoutNotified = false;
    bool stallNoticeEmitted = false;

    std::unique_ptr<IDelegateBackendSession> session;
    QPointer<QTimer> watchdog;
};

void DelegateTaskScheduler::markRuntimeActivityLocked(AsyncJobRuntime& runtime)
{
    runtime.lastProgressAtMs = QDateTime::currentMSecsSinceEpoch();
    runtime.stallNoticeEmitted = false;
    if (runtime.softTimeoutNotified)
        runtime.status = QStringLiteral("running");
}

void DelegateTaskScheduler::appendTimelineRowLocked(
    AsyncJobRuntime& runtime,
    const QString& status,
    const QString& summary,
    const QJsonObject& extra)
{
    if (runtime.childTimeline.size() >= kChildTimelineLimit) {
        ++runtime.childTimelineDropped;
        return;
    }

    QJsonObject row;
    row.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    row.insert(QStringLiteral("status"), status);
    if (!summary.trimmed().isEmpty())
        row.insert(QStringLiteral("summary"), summary);
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        row.insert(it.key(), it.value());
    runtime.childTimeline.append(row);
}

DelegateTaskScheduler::Result DelegateTaskScheduler::buildAcceptedResult(
    const QSharedPointer<AsyncJobRuntime>& runtime,
    const QJsonObject& inputData) const
{
    QJsonObject data = inputData;
    DelegateTaskScheduler::Result result;
    data.insert(QStringLiteral("status"), QStringLiteral("accepted"));
    data.insert(QStringLiteral("job_id"), runtime->jobId);
    data.insert(QStringLiteral("owner_agent_id"), runtime->ownerAgentId);
    data.insert(QStringLiteral("expected_timeout_ms"), runtime->expectedTimeoutMs);
    data.insert(QStringLiteral("hard_timeout_ms"), runtime->hardTimeoutMs);
    data.insert(QStringLiteral("stall_no_progress_ms"), runtime->stallNoProgressMs);
    data.insert(QStringLiteral("child_agent_id"), runtime->childAgentId);
    data.insert(QStringLiteral("child_model"), runtime->childModel);
    if (!runtime->backendProgram.trimmed().isEmpty())
        data.insert(QStringLiteral("backend_program"), runtime->backendProgram.trimmed());

    result.success = true;
    if (isCodexBackend(runtime->backend)) {
        result.rawResult = QStringLiteral(
                               "Codex 子代理任务已启动。\n"
                               "job_id: %1\n"
                               "backend: codex\n"
                               "说明: 可使用 delegate_status 查看进度，delegate_cancel 取消任务。")
                               .arg(runtime->jobId);
        result.userSummary = QStringLiteral("Codex 子代理任务已启动");
    } else {
        result.rawResult = QStringLiteral(
                               "后台子代理任务已启动。\n"
                               "job_id: %1\n"
                               "说明: 可使用 delegate_status 查看进度，delegate_cancel 取消任务。")
                               .arg(runtime->jobId);
        result.userSummary = QStringLiteral("后台子代理任务已启动");
    }
    result.data = data;
    return result;
}

DelegateTaskScheduler::DelegateTaskScheduler(QObject* parent)
    : QObject(parent)
    , m_tmagentBackend(std::make_unique<DelegateBackendInternal::TmagentDelegateBackend>())
    , m_codexBackend(std::make_unique<DelegateBackendInternal::CodexDelegateBackend>())
{
}

DelegateTaskScheduler::~DelegateTaskScheduler() = default;

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

DelegateBackendCallbacks DelegateTaskScheduler::makeBackendCallbacks(const QSharedPointer<AsyncJobRuntime>& runtime)
{
    DelegateBackendCallbacks callbacks;

    callbacks.onActivity = [this, runtime]() {
        QWriteLocker locker(&m_lock);
        if (!runtime || runtime->settled)
            return;
        markRuntimeActivityLocked(*runtime);
    };

    callbacks.onSummary = [this, runtime](const QString& summary) {
        QWriteLocker locker(&m_lock);
        if (!runtime || runtime->settled || summary.trimmed().isEmpty())
            return;
        runtime->summary = truncateSummary(summary);
    };

    callbacks.onToolEvent = [this, runtime](const ToolExecutionEvent& event) {
        QWriteLocker locker(&m_lock);
        if (!runtime || runtime->settled)
            return;

        markRuntimeActivityLocked(*runtime);
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

        QJsonObject extra;
        if (!toolName.isEmpty())
            extra.insert(QStringLiteral("tool_name"), toolName);
        if (!event.toolId.trimmed().isEmpty())
            extra.insert(QStringLiteral("tool_id"), event.toolId.trimmed());
        if (event.status == QLatin1String("completed"))
            extra.insert(QStringLiteral("success"), event.success);
        const QString summary = summarizeTimelineEntry(event);
        appendTimelineRowLocked(*runtime, event.status.trimmed(), summary, extra);
        if (!summary.isEmpty())
            runtime->summary = truncateSummary(summary);
    };

    callbacks.onStreamDelta = [this, runtime](const QString& delta) {
        QWriteLocker locker(&m_lock);
        if (!runtime || runtime->settled)
            return;
        markRuntimeActivityLocked(*runtime);
        ++runtime->childStreamChunkCount;
        runtime->childStreamChars += delta.size();
    };

    callbacks.onBackendIdentity = [this, runtime](const QString& threadId, const QString& turnId, const QString& program) {
        QWriteLocker locker(&m_lock);
        if (!runtime || runtime->settled)
            return;
        if (!threadId.trimmed().isEmpty())
            runtime->backendThreadId = threadId.trimmed();
        if (!turnId.trimmed().isEmpty())
            runtime->backendTurnId = turnId.trimmed();
        if (!program.trimmed().isEmpty())
            runtime->backendProgram = program.trimmed();
    };

    callbacks.onTimelineEvent = [this, runtime](const QString& status, const QString& summary, const QJsonObject& extra) {
        QWriteLocker locker(&m_lock);
        if (!runtime || runtime->settled)
            return;
        markRuntimeActivityLocked(*runtime);
        appendTimelineRowLocked(*runtime, status, summary, extra);
        if (!summary.isEmpty())
            runtime->summary = truncateSummary(summary);
    };

    callbacks.onSuccess = [this, runtime](const QString& normalizedResult) {
        const QString childStatus = DelegateBackendInternal::extractStatusTag(normalizedResult);
        const bool blocked = DelegateBackendInternal::isBlockedStatus(childStatus);
        settleAsyncJob(
            runtime,
            !blocked,
            normalizedResult,
            blocked ? QStringLiteral("child_blocked") : QString());
    };

    callbacks.onFailure = [this, runtime](const QString& reason) {
        settleAsyncJob(runtime, false, QString(), reason.trimmed());
    };

    return callbacks;
}

IDelegateBackend* DelegateTaskScheduler::resolveBackend(const QString& backendId) const
{
    if (backendId == QLatin1String("codex"))
        return m_codexBackend.get();
    return m_tmagentBackend.get();
}

void DelegateTaskScheduler::registerRuntime(const QSharedPointer<AsyncJobRuntime>& runtime)
{
    QWriteLocker locker(&m_lock);
    m_asyncJobs.insert(runtime->jobId, runtime);
    m_asyncJobOrder.append(runtime->jobId);
    pruneJobsLocked();
}

void DelegateTaskScheduler::pruneJobsLocked()
{
    if (m_asyncJobOrder.size() <= kMaxAsyncJobs)
        return;

    while (m_asyncJobOrder.size() > kMaxAsyncJobs) {
        int removeIndex = -1;
        for (int i = 0; i < m_asyncJobOrder.size(); ++i) {
            const QSharedPointer<AsyncJobRuntime> runtime = m_asyncJobs.value(m_asyncJobOrder.at(i));
            if (!runtime || isFinishedStatus(runtime->status)) {
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

void DelegateTaskScheduler::handleWatchdogTimeout(const QSharedPointer<AsyncJobRuntime>& runtime)
{
    if (!runtime)
        return;

    bool emitStatusChanged = false;
    QString newStatus;
    QString newSummary;
    QString hardTimeoutReason;
    std::unique_ptr<IDelegateBackendSession> timedOutSession;

    {
        QWriteLocker locker(&m_lock);
        if (runtime->settled)
            return;

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 elapsedMs = nowMs - runtime->startedAtMs;
        const qint64 idleMs = nowMs - runtime->lastProgressAtMs;

        if (!runtime->softTimeoutNotified && elapsedMs >= runtime->expectedTimeoutMs) {
            runtime->softTimeoutNotified = true;
            runtime->stallNoticeEmitted = false;
            runtime->status = QStringLiteral("soft_timeout");
            runtime->summary = softTimeoutSummaryForBackend(runtime->backend, false);
            emitStatusChanged = true;
            newStatus = runtime->status;
            newSummary = runtime->summary;
        }

        if (elapsedMs >= runtime->hardTimeoutMs) {
            hardTimeoutReason = hardTimeoutReasonForBackend(runtime->backend);
            timedOutSession = std::move(runtime->session);
        } else if (runtime->softTimeoutNotified
                   && idleMs >= runtime->stallNoProgressMs
                   && !runtime->stallNoticeEmitted) {
            runtime->stallNoticeEmitted = true;
            runtime->status = QStringLiteral("soft_timeout");
            runtime->summary = softTimeoutSummaryForBackend(runtime->backend, true);
            emitStatusChanged = true;
            newStatus = runtime->status;
            newSummary = runtime->summary;
        }
    }

    if (timedOutSession) {
        timedOutSession->cancel();
        settleAsyncJob(runtime, false, QString(), hardTimeoutReason);
        return;
    }

    if (emitStatusChanged)
        emit jobStatusChanged(runtime->jobId, runtime->ownerAgentId, newStatus, newSummary);
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
    std::unique_ptr<IDelegateBackendSession> session;
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
            const QString childStatus = DelegateBackendInternal::extractStatusTag(result);
            runtime->status = QStringLiteral("completed");
            runtime->summary = successSummaryForBackend(
                runtime->backend,
                childStatus == QLatin1String("PARTIAL"));
            runtime->failureReason.clear();
        } else {
            runtime->status = runtime->cancelRequested
                ? QStringLiteral("cancelled")
                : QStringLiteral("failed");
            runtime->summary = failureSummaryForBackend(runtime->backend, runtime->cancelRequested);
            runtime->failureReason = failureReason.trimmed();
        }
        watchdog = runtime->watchdog;
        runtime->watchdog = nullptr;
        session = std::move(runtime->session);
    }

    if (watchdog) {
        watchdog->stop();
        watchdog->deleteLater();
    }
    if (session)
        session->cancel();

    emit jobSettled(runtime->jobId, runtime->ownerAgentId, success, result);
}

DelegateTaskScheduler::Result DelegateTaskScheduler::submitAsync(const Request& request, const QString& ownerAgentId)
{
    const PreparedSubmit prepared = prepareSubmit(request, ownerAgentId);

    Result rejected;
    if (!validateSubmitRequest(this, request, prepared.task, prepared.data, &rejected))
        return rejected;

    const QSharedPointer<AsyncJobRuntime> runtime(new AsyncJobRuntime());
    runtime->jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    runtime->ownerAgentId = prepared.normalizedOwnerId;
    runtime->status = QStringLiteral("running");
    runtime->backend = prepared.backend;
    runtime->summary = isCodexBackend(prepared.backend)
        ? QStringLiteral("Codex 子代理任务已启动")
        : QStringLiteral("后台子代理任务已启动");
    runtime->task = prepared.task.left(4000);
    runtime->createdAtMs = QDateTime::currentMSecsSinceEpoch();
    runtime->startedAtMs = runtime->createdAtMs;
    runtime->lastProgressAtMs = runtime->createdAtMs;
    runtime->expectedTimeoutMs = prepared.softTimeoutMs;
    runtime->hardTimeoutMs = prepared.hardTimeoutMs;
    runtime->stallNoProgressMs = prepared.stallNoProgressMs;
    runtime->maxResponseChars = prepared.maxResponseChars;
    runtime->childAgentId = prepared.childConfig.uuid;
    runtime->childModel = isCodexBackend(prepared.backend)
        ? QStringLiteral("codex-app-server")
        : ModelFactory::instance()->resolveModelId(prepared.childConfig);

    DelegateBackendStartRequest startRequest;
    startRequest.task = prepared.task;
    startRequest.executionPrompt = prepared.executionPrompt;
    startRequest.childConfig = prepared.childConfig;
    startRequest.expectedTimeoutMs = prepared.softTimeoutMs;
    startRequest.maxResponseChars = prepared.maxResponseChars;
    startRequest.restrictDelegation = request.restrictDelegation;
    startRequest.inheritedAllowedTools = request.inheritedAllowedTools;
    startRequest.toolDispatcher = request.toolDispatcher;

    IDelegateBackend* backend = resolveBackend(prepared.backend);
    QString sessionError;
    std::unique_ptr<IDelegateBackendSession> session =
        backend ? backend->createSession(startRequest, makeBackendCallbacks(runtime), &sessionError) : nullptr;
    if (!session) {
        return makeRejectedResult(
            prepared.data,
            QStringLiteral("backend_unavailable"),
            sessionError.trimmed().isEmpty()
                ? QStringLiteral("错误: 后台子代理后端不可用")
                : QStringLiteral("错误: %1").arg(sessionError.trimmed()),
            QStringLiteral("后台子代理任务提交失败：后端不可用"));
    }

    runtime->backendProgram = session->backendProgram().trimmed();
    runtime->session = std::move(session);

    QTimer* watchdog = new QTimer(QCoreApplication::instance());
    watchdog->setInterval(1000);
    watchdog->setSingleShot(false);
    runtime->watchdog = watchdog;
    QObject::connect(
        watchdog,
        &QTimer::timeout,
        watchdog,
        [this, runtime]() { handleWatchdogTimeout(runtime); });

    registerRuntime(runtime);
    const Result accepted = buildAcceptedResult(runtime, prepared.data);
    watchdog->start();
    if (runtime->session)
        runtime->session->start();
    return accepted;
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
    std::unique_ptr<IDelegateBackendSession> session;
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
        session = std::move(runtime->session);
    }

    if (session)
        session->cancel();
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
