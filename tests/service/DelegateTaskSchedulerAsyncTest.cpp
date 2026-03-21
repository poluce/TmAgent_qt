#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QUuid>

#include "core/agent/DelegateTaskScheduler.h"

static int g_testCount = 0;
static int g_passCount = 0;

#define PRINT_DIVIDER() qDebug().noquote() << "────────────────────────────────────────"
#define PRINT_RESULT(pass) qDebug().noquote() << (pass ? "  ✅ 通过" : "  ❌ 失败")

#define TEST(name) \
    ++g_testCount; \
    PRINT_DIVIDER(); \
    qDebug().noquote() << QString("[测试 %1] %2").arg(g_testCount).arg(name); \
    if (auto result = [&]() -> int

#define END_TEST \
    (); result != 0) { \
        PRINT_RESULT(false); \
    } else { \
        ++g_passCount; \
        PRINT_RESULT(true); \
    }

namespace {

int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
    return 1;
}

bool containsJobId(const QList<DelegateTaskScheduler::JobInfo>& jobs, const QString& jobId)
{
    for (const DelegateTaskScheduler::JobInfo& job : jobs) {
        if (job.jobId == jobId)
            return true;
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "   DelegateTaskScheduler Async 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("submitAsync - 受理后可查询，并最终发出 settled 事件") {
        auto* scheduler = DelegateTaskScheduler::instance();
        const QString ownerAgentId =
            QStringLiteral("delegate-owner-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

        QStringList settledJobIds;
        QString settledOwnerId;
        bool settledSuccess = false;
        QString settledResult;

        const QMetaObject::Connection conn = QObject::connect(
            scheduler,
            &DelegateTaskScheduler::jobSettled,
            &app,
            [&](const QString& jobId, const QString& ownerAgent, bool success, const QString& result) {
                if (ownerAgent != ownerAgentId)
                    return;
                settledJobIds.append(jobId);
                settledOwnerId = ownerAgent;
                settledSuccess = success;
                settledResult = result;
            });

        DelegateTaskScheduler::Request request;
        request.delegateToolName = QStringLiteral("delegate_task");
        request.task = QStringLiteral("验证后台委派任务提交后是否可查询并结算");
        request.backend = QStringLiteral("tmagent");
        request.expectedTimeoutMs = 2000;
        request.maxResponseChars = 1200;
        request.parentConfig.uuid = ownerAgentId;
        request.parentConfig.configId = QStringLiteral("missing-test-config");
        request.parentConfig.recursionDepth = 1;

        const DelegateTaskScheduler::Result submitResult =
            scheduler->submitAsync(request, ownerAgentId);

        if (!submitResult.success)
            return fail(QStringLiteral("submitAsync 成功受理"), submitResult.rawResult);
        if (submitResult.data.value(QStringLiteral("status")).toString() != QStringLiteral("accepted")) {
            return fail(QStringLiteral("accepted"),
                        submitResult.data.value(QStringLiteral("status")).toString());
        }

        const QString jobId = submitResult.data.value(QStringLiteral("job_id")).toString().trimmed();
        if (jobId.isEmpty())
            return fail(QStringLiteral("非空 job_id"), QStringLiteral("<empty>"));

        DelegateTaskScheduler::JobInfo info;
        if (!scheduler->queryJob(jobId, ownerAgentId, &info))
            return fail(QStringLiteral("queryJob 命中已受理任务"), QStringLiteral("false"));
        if (info.jobId != jobId)
            return fail(jobId, info.jobId);
        if (info.ownerAgentId != ownerAgentId)
            return fail(ownerAgentId, info.ownerAgentId);
        if (info.task.trimmed().isEmpty())
            return fail(QStringLiteral("非空 task"), QStringLiteral("<empty>"));
        if (info.status.trimmed().isEmpty())
            return fail(QStringLiteral("非空 status"), QStringLiteral("<empty>"));

        const QList<DelegateTaskScheduler::JobInfo> jobs = scheduler->listJobs(ownerAgentId, false, 10);
        if (!containsJobId(jobs, jobId))
            return fail(QStringLiteral("listJobs 可见该 job"), QStringLiteral("missing"));

        QElapsedTimer timer;
        timer.start();
        while (!settledJobIds.contains(jobId) && timer.elapsed() < 1000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        QObject::disconnect(conn);

        if (!settledJobIds.contains(jobId))
            return fail(QStringLiteral("jobSettled 信号已触发"), QStringLiteral("timeout"));
        if (settledOwnerId != ownerAgentId)
            return fail(ownerAgentId, settledOwnerId);

        DelegateTaskScheduler::JobInfo settledInfo;
        if (!scheduler->queryJob(jobId, ownerAgentId, &settledInfo))
            return fail(QStringLiteral("queryJob 在结算后仍可读取"), QStringLiteral("false"));
        if (settledInfo.finishedAtMs <= 0)
            return fail(QStringLiteral("finished_at_ms > 0"), QString::number(settledInfo.finishedAtMs));
        if (settledInfo.status != QStringLiteral("failed")) {
            return fail(QStringLiteral("failed"),
                        settledInfo.status);
        }
        if (settledSuccess)
            return fail(QStringLiteral("false"), QStringLiteral("true"));
        Q_UNUSED(settledResult);

        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}
