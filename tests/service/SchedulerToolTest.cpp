#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QHash>

#include "core/tools/SchedulerTool.h"
#include "core/utils/DefaultPrompts.h"

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

struct SchedulerFixture {
    QHash<QString, ScheduledJob> jobs;
    int nextId = 1;

    SchedulerFixture()
    {
        SchedulerTool::setDependencies(
            SchedulerTool::Dependencies {
                [this]() { return jobs.values(); },
                [this](const QString& jobId, ScheduledJob* outJob) {
                    if (!outJob || !jobs.contains(jobId))
                        return false;
                    *outJob = jobs.value(jobId);
                    return true;
                },
                [this](const ScheduledJob& job) {
                    ScheduledJob stored = job;
                    stored.jobId = QStringLiteral("job-%1").arg(nextId++);
                    if (stored.scheduleType.trimmed().isEmpty())
                        stored.scheduleType = QStringLiteral("cron");
                    if (stored.enabled) {
                        stored.nextFireAtUtc = stored.scheduleType == QLatin1String("once")
                            ? stored.runAtUtc
                            : QDateTime::currentDateTimeUtc().addSecs(60);
                    }
                    jobs.insert(stored.jobId, stored);
                    return stored.jobId;
                },
                [this](const QString& jobId, const ScheduledJob& job) {
                    if (!jobs.contains(jobId))
                        return false;
                    ScheduledJob stored = job;
                    stored.jobId = jobId;
                    if (stored.scheduleType.trimmed().isEmpty())
                        stored.scheduleType = QStringLiteral("cron");
                    if (stored.enabled) {
                        stored.nextFireAtUtc = stored.scheduleType == QLatin1String("once")
                            ? stored.runAtUtc
                            : QDateTime::currentDateTimeUtc().addSecs(60);
                    } else {
                        stored.nextFireAtUtc = QDateTime();
                    }
                    jobs.insert(jobId, stored);
                    return true;
                },
                [this](const QString& jobId) { return jobs.remove(jobId) > 0; },
                [this](const QString& jobId) {
                    if (jobs.contains(jobId)) {
                        ScheduledJob stored = jobs.value(jobId);
                        stored.lastFireAtUtc = QDateTime::currentDateTimeUtc();
                        jobs.insert(jobId, stored);
                    }
                }
            });
    }
};

QJsonObject agentArgs(const QString& agentId)
{
    QJsonObject args;
    args.insert(QStringLiteral("_agent_id"), agentId);
    return args;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "  SchedulerTool 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("scheduler_create - 成功创建当前助手任务") {
        SchedulerFixture fixture;
        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        args.insert(QStringLiteral("name"), QStringLiteral("每日简报"));
        args.insert(QStringLiteral("prompt"), QStringLiteral("输出每日摘要"));
        args.insert(QStringLiteral("cron_expr"), QStringLiteral("0 9 * * *"));
        args.insert(QStringLiteral("session_target"), QStringLiteral("main"));
        const ToolResult result = SchedulerTool::executeCreate(args);
        if (!result.success)
            return fail(QStringLiteral("success=true"), result.rawContent);
        if (!result.rawContent.contains(QStringLiteral("job_id: job-1")))
            return fail(QStringLiteral("rawContent 包含 job-1"), result.rawContent);
        if (result.userSummary != QStringLiteral("已创建定时任务“每日简报”"))
            return fail(QStringLiteral("已创建定时任务“每日简报”"), result.userSummary);
        if (result.data.value(QStringLiteral("agent_id")).toString() != QStringLiteral("agent-a"))
            return fail(QStringLiteral("agent-a"), result.data.value(QStringLiteral("agent_id")).toString());
        return 0;
    } END_TEST

    TEST("scheduler_create - 非法 cron_expr 失败") {
        SchedulerFixture fixture;
        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        args.insert(QStringLiteral("name"), QStringLiteral("坏任务"));
        args.insert(QStringLiteral("prompt"), QStringLiteral("输出"));
        args.insert(QStringLiteral("cron_expr"), QStringLiteral("0 9 * *"));
        const ToolResult result = SchedulerTool::executeCreate(args);
        if (result.success)
            return fail(QStringLiteral("success=false"), QStringLiteral("true"));
        if (!result.rawContent.contains(QStringLiteral("cron_expr")))
            return fail(QStringLiteral("错误包含 cron_expr"), result.rawContent);
        return 0;
    } END_TEST

    TEST("scheduler_create - 单次任务成功创建并返回 run_at_utc") {
        SchedulerFixture fixture;
        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        const QDateTime runAtUtc = QDateTime::currentDateTimeUtc().addSecs(3600);
        const QString runAtText = runAtUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
        args.insert(QStringLiteral("name"), QStringLiteral("一次性提醒"));
        args.insert(QStringLiteral("prompt"), QStringLiteral("提醒用户：开会"));
        args.insert(QStringLiteral("schedule_type"), QStringLiteral("once"));
        args.insert(QStringLiteral("run_at"), runAtText);
        const ToolResult result = SchedulerTool::executeCreate(args);
        if (!result.success)
            return fail(QStringLiteral("success=true"), result.rawContent);
        if (result.data.value(QStringLiteral("schedule_type")).toString() != QStringLiteral("once"))
            return fail(QStringLiteral("schedule_type=once"),
                        result.data.value(QStringLiteral("schedule_type")).toString());
        if (result.data.value(QStringLiteral("run_at_utc")).toString().trimmed().isEmpty())
            return fail(QStringLiteral("run_at_utc 非空"), QStringLiteral("<empty>"));
        if (!result.rawContent.contains(QStringLiteral("schedule_type: once")))
            return fail(QStringLiteral("rawContent 包含 schedule_type: once"), result.rawContent);
        return 0;
    } END_TEST

    TEST("scheduler_create - 单次任务缺少 run_at 失败") {
        SchedulerFixture fixture;
        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        args.insert(QStringLiteral("name"), QStringLiteral("一次性提醒"));
        args.insert(QStringLiteral("prompt"), QStringLiteral("提醒用户：开会"));
        args.insert(QStringLiteral("schedule_type"), QStringLiteral("once"));
        const ToolResult result = SchedulerTool::executeCreate(args);
        if (result.success)
            return fail(QStringLiteral("success=false"), QStringLiteral("true"));
        if (!result.rawContent.contains(QStringLiteral("schedule_type=once 时必须提供 run_at")))
            return fail(QStringLiteral("提示缺少 run_at"), result.rawContent);
        return 0;
    } END_TEST

    TEST("scheduler_create - 单次任务不允许 cron_expr") {
        SchedulerFixture fixture;
        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        args.insert(QStringLiteral("name"), QStringLiteral("一次性提醒"));
        args.insert(QStringLiteral("prompt"), QStringLiteral("提醒用户：开会"));
        args.insert(QStringLiteral("schedule_type"), QStringLiteral("once"));
        args.insert(QStringLiteral("run_at"),
                    QDateTime::currentDateTime().addSecs(1800).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")));
        args.insert(QStringLiteral("cron_expr"), QStringLiteral("0 9 * * *"));
        const ToolResult result = SchedulerTool::executeCreate(args);
        if (result.success)
            return fail(QStringLiteral("success=false"), QStringLiteral("true"));
        if (!result.rawContent.contains(QStringLiteral("cron_expr 和 run_at 不能同时提供")))
            return fail(QStringLiteral("提示 cron_expr/run_at 互斥"), result.rawContent);
        return 0;
    } END_TEST

    TEST("scheduler_create - cron 任务不允许 run_at") {
        SchedulerFixture fixture;
        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        args.insert(QStringLiteral("name"), QStringLiteral("每日提醒"));
        args.insert(QStringLiteral("prompt"), QStringLiteral("提醒用户：喝水"));
        args.insert(QStringLiteral("cron_expr"), QStringLiteral("0 9 * * *"));
        args.insert(QStringLiteral("run_at"),
                    QDateTime::currentDateTime().addSecs(1800).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")));
        const ToolResult result = SchedulerTool::executeCreate(args);
        if (result.success)
            return fail(QStringLiteral("success=false"), QStringLiteral("true"));
        if (!result.rawContent.contains(QStringLiteral("cron_expr 和 run_at 不能同时提供")))
            return fail(QStringLiteral("提示 cron_expr/run_at 互斥"), result.rawContent);
        return 0;
    } END_TEST

    TEST("scheduler_create - 连写分钟小时时给出纠错提示") {
        SchedulerFixture fixture;
        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        args.insert(QStringLiteral("name"), QStringLiteral("每天 14:31"));
        args.insert(QStringLiteral("prompt"), QStringLiteral("提醒用户：喝水"));
        args.insert(QStringLiteral("cron_expr"), QStringLiteral("3114 * * *"));
        const ToolResult result = SchedulerTool::executeCreate(args);
        if (result.success)
            return fail(QStringLiteral("success=false"), QStringLiteral("true"));
        if (!result.rawContent.contains(QStringLiteral("failing_field: cron_expr"))
            || !result.rawContent.contains(QStringLiteral("bad_value: 3114 * * *"))
            || !result.rawContent.contains(QStringLiteral("expected_format: 分钟 小时 日 月 周"))
            || !result.rawContent.contains(QStringLiteral("retry_rule: 若本轮重试，只修正 cron_expr，其余参数保持不变"))
            || !result.rawContent.contains(QStringLiteral("suggested_value: 31 14 * * *"))) {
            return fail(QStringLiteral("rawContent 包含结构化纠错提示"), result.rawContent);
        }
        if (result.data.value(QStringLiteral("failing_field")).toString() != QStringLiteral("cron_expr"))
            return fail(QStringLiteral("data.failing_field=cron_expr"),
                        result.data.value(QStringLiteral("failing_field")).toString());
        if (result.data.value(QStringLiteral("suggested_value")).toString() != QStringLiteral("31 14 * * *"))
            return fail(QStringLiteral("data.suggested_value=31 14 * * *"),
                        result.data.value(QStringLiteral("suggested_value")).toString());
        return 0;
    } END_TEST

    TEST("scheduler_create - 14:35 连写时提示 35 14 * * *") {
        SchedulerFixture fixture;
        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        args.insert(QStringLiteral("name"), QStringLiteral("每天 14:35"));
        args.insert(QStringLiteral("prompt"), QStringLiteral("提醒用户：喝水"));
        args.insert(QStringLiteral("cron_expr"), QStringLiteral("3514 * * *"));
        const ToolResult result = SchedulerTool::executeCreate(args);
        if (result.success)
            return fail(QStringLiteral("success=false"), QStringLiteral("true"));
        if (!result.rawContent.contains(QStringLiteral("suggested_value: 35 14 * * *"))
            || !result.rawContent.contains(QStringLiteral("do_not_repeat: 3514 * * *"))) {
            return fail(QStringLiteral("rawContent 包含 14:35 对应纠错提示"), result.rawContent);
        }
        if (result.data.value(QStringLiteral("suggested_value")).toString() != QStringLiteral("35 14 * * *"))
            return fail(QStringLiteral("data.suggested_value=35 14 * * *"),
                        result.data.value(QStringLiteral("suggested_value")).toString());
        return 0;
    } END_TEST

    TEST("Execution contract - 参数校验失败后先纠正再重试") {
        const QString mainPrompt =
            DefaultPrompts::ensureExecutionDiscipline(QStringLiteral("base prompt"));
        const QString workerPrompt =
            DefaultPrompts::ensureWorkerExecutionDiscipline(QStringLiteral("worker prompt"));

        if (!mainPrompt.contains(QStringLiteral("遇到工具参数校验失败时"))
            || !mainPrompt.contains(QStringLiteral("只允许基于新证据做 1 次修正重试"))
            || !mainPrompt.contains(QStringLiteral("禁止原样重复失败参数"))) {
            return fail(QStringLiteral("主代理执行契约包含自纠重试规则"), mainPrompt);
        }
        if (!workerPrompt.contains(QStringLiteral("遇到工具参数校验失败时"))
            || !workerPrompt.contains(QStringLiteral("只允许基于新证据做 1 次修正重试"))
            || !workerPrompt.contains(QStringLiteral("禁止原样重复失败参数"))) {
            return fail(QStringLiteral("子代理执行契约包含自纠重试规则"), workerPrompt);
        }
        return 0;
    } END_TEST

    TEST("Execution contract - continuous_execute 与 plan_first 契约切换") {
        const QString continuousMain = DefaultPrompts::ensureExecutionDiscipline(
            QStringLiteral("base prompt"),
            DefaultPrompts::executionModeContinuous());
        const QString planMain = DefaultPrompts::ensureExecutionDiscipline(
            QStringLiteral("base prompt"),
            DefaultPrompts::executionModePlanFirst());
        const QString continuousWorker = DefaultPrompts::ensureWorkerExecutionDiscipline(
            QStringLiteral("worker prompt"),
            DefaultPrompts::executionModeContinuous());
        const QString planWorker = DefaultPrompts::ensureWorkerExecutionDiscipline(
            QStringLiteral("worker prompt"),
            DefaultPrompts::executionModePlanFirst());

        if (!continuousMain.contains(QStringLiteral("默认按`实际执行`处理"))
            || !continuousMain.contains(QStringLiteral("在同一回合内持续执行"))) {
            return fail(QStringLiteral("continuous main prompt 包含连续执行规则"), continuousMain);
        }
        if (!planMain.contains(QStringLiteral("默认先给规划说明"))
            || !planMain.contains(QStringLiteral("一旦进入`实际执行`，同一回合内允许连续推进"))) {
            return fail(QStringLiteral("plan main prompt 包含规划优先规则"), planMain);
        }
        if (!continuousWorker.contains(QStringLiteral("默认按`实际执行`处理"))
            || !continuousWorker.contains(QStringLiteral("在同一回合内持续执行"))) {
            return fail(QStringLiteral("continuous worker prompt 包含连续执行规则"), continuousWorker);
        }
        if (!planWorker.contains(QStringLiteral("默认先走`规划说明`"))
            || !planWorker.contains(QStringLiteral("一旦进入`实际执行`，同一回合内允许连续推进"))) {
            return fail(QStringLiteral("plan worker prompt 包含规划优先规则"), planWorker);
        }
        return 0;
    } END_TEST

    TEST("scheduler_list - 只列当前助手自己的任务") {
        SchedulerFixture fixture;
        ScheduledJob a;
        a.jobId = QStringLiteral("job-a");
        a.name = QStringLiteral("A");
        a.agentId = QStringLiteral("agent-a");
        a.prompt = QStringLiteral("do A");
        a.cronExpr = QStringLiteral("0 9 * * *");
        a.enabled = true;
        a.nextFireAtUtc = QDateTime::currentDateTimeUtc().addSecs(60);
        fixture.jobs.insert(a.jobId, a);

        ScheduledJob b = a;
        b.jobId = QStringLiteral("job-b");
        b.name = QStringLiteral("B");
        b.agentId = QStringLiteral("agent-b");
        fixture.jobs.insert(b.jobId, b);

        const ToolResult result = SchedulerTool::executeList(agentArgs(QStringLiteral("agent-a")));
        if (!result.success)
            return fail(QStringLiteral("success=true"), result.rawContent);
        if (!result.rawContent.contains(QStringLiteral("job-a")) || result.rawContent.contains(QStringLiteral("job-b")))
            return fail(QStringLiteral("仅包含 job-a"), result.rawContent);
        return 0;
    } END_TEST

    TEST("scheduler_update - 只能更新当前助手自己的任务") {
        SchedulerFixture fixture;
        ScheduledJob job;
        job.jobId = QStringLiteral("job-a");
        job.name = QStringLiteral("A");
        job.agentId = QStringLiteral("agent-b");
        job.prompt = QStringLiteral("do");
        job.cronExpr = QStringLiteral("0 9 * * *");
        job.enabled = true;
        job.nextFireAtUtc = QDateTime::currentDateTimeUtc().addSecs(60);
        fixture.jobs.insert(job.jobId, job);

        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        args.insert(QStringLiteral("job_id"), QStringLiteral("job-a"));
        args.insert(QStringLiteral("name"), QStringLiteral("A2"));
        const ToolResult result = SchedulerTool::executeUpdate(args);
        if (result.success)
            return fail(QStringLiteral("success=false"), QStringLiteral("true"));
        if (!result.rawContent.contains(QStringLiteral("无权操作任务")))
            return fail(QStringLiteral("权限失败"), result.rawContent);
        return 0;
    } END_TEST

    TEST("scheduler_run - 禁用任务失败") {
        SchedulerFixture fixture;
        ScheduledJob job;
        job.jobId = QStringLiteral("job-a");
        job.name = QStringLiteral("A");
        job.agentId = QStringLiteral("agent-a");
        job.prompt = QStringLiteral("do");
        job.cronExpr = QStringLiteral("0 9 * * *");
        job.enabled = false;
        fixture.jobs.insert(job.jobId, job);

        QJsonObject args = agentArgs(QStringLiteral("agent-a"));
        args.insert(QStringLiteral("job_id"), QStringLiteral("job-a"));
        const ToolResult result = SchedulerTool::executeRun(args);
        if (result.success)
            return fail(QStringLiteral("success=false"), QStringLiteral("true"));
        if (!result.rawContent.contains(QStringLiteral("任务已禁用")))
            return fail(QStringLiteral("任务已禁用"), result.rawContent);
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果：%1/%2 通过").arg(g_passCount).arg(g_testCount);
    PRINT_DIVIDER();
    return g_passCount == g_testCount ? 0 : 1;
}
