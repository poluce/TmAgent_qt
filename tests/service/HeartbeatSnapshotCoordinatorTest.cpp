#include <QCoreApplication>
#include <QDebug>

#include "HeartbeatSnapshotCoordinator.h"

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

HeartbeatConfig baseConfig()
{
    HeartbeatConfig cfg;
    cfg.silentWhenNoChange = true;
    cfg.notifyOnChangeOnly = true;
    cfg.notifyMinIntervalMs = 30 * 60 * 1000;
    cfg.persistStateOnNoChange = false;
    cfg.statePersistIntervalMs = 60 * 1000;
    cfg.snapshotSignals = QStringList()
        << QStringLiteral("provider_status")
        << QStringLiteral("delegate_jobs")
        << QStringLiteral("pulse_state");
    return cfg;
}

HeartbeatSnapshotCoordinator::Inputs baseInputs()
{
    HeartbeatSnapshotCoordinator::Inputs inputs;
    inputs.agentId = QStringLiteral("agent-1");
    inputs.reason = QStringLiteral("interval");
    inputs.config = baseConfig();
    inputs.nowUtc = QDateTime::currentDateTimeUtc();
    return inputs;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "   HeartbeatSnapshotCoordinator 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("首帧快照 - 默认静默但需要持久化") {
        HeartbeatSnapshotCoordinator::Inputs inputs = baseInputs();
        const auto result = HeartbeatSnapshotCoordinator::evaluate(inputs);
        if (!result.valid)
            return fail(QStringLiteral("valid=true"), QStringLiteral("false"));
        if (result.forceInteractive)
            return fail(QStringLiteral("forceInteractive=false"), QStringLiteral("true"));
        if (result.shouldNotify)
            return fail(QStringLiteral("shouldNotify=false"), QStringLiteral("true"));
        if (result.skipReason != QStringLiteral("silent_no_change"))
            return fail(QStringLiteral("silent_no_change"), result.skipReason);
        if (!result.shouldPersistState)
            return fail(QStringLiteral("shouldPersistState=true"), QStringLiteral("false"));
        return 0;
    } END_TEST

    TEST("手动触发 - 无变化也允许通知") {
        HeartbeatSnapshotCoordinator::Inputs inputs = baseInputs();
        inputs.reason = QStringLiteral("requested");
        const auto result = HeartbeatSnapshotCoordinator::evaluate(inputs);
        if (!result.valid)
            return fail(QStringLiteral("valid=true"), QStringLiteral("false"));
        if (!result.forceInteractive)
            return fail(QStringLiteral("forceInteractive=true"), QStringLiteral("false"));
        if (!result.shouldNotify)
            return fail(QStringLiteral("shouldNotify=true"), QStringLiteral("false"));
        return 0;
    } END_TEST

    TEST("委派任务变化 - 视为可行动变化并允许通知") {
        HeartbeatSnapshotCoordinator::Inputs inputs = baseInputs();
        inputs.runtimeState.hasSnapshot = true;
        inputs.runtimeState.lastSnapshotObj.insert(QStringLiteral("watch_signals"),
                                                   QJsonArray { QStringLiteral("delegate_jobs") });
        inputs.runtimeState.lastSnapshotObj.insert(QStringLiteral("active_jobs_count"), 0);
        inputs.config.snapshotSignals = QStringList() << QStringLiteral("delegate_jobs");
        DelegateTaskScheduler::JobInfo job;
        job.jobId = QStringLiteral("job-1");
        job.status = QStringLiteral("running");
        job.summary = QStringLiteral("processing");
        inputs.activeJobs.append(job);

        const auto result = HeartbeatSnapshotCoordinator::evaluate(inputs);
        if (!result.hasChange)
            return fail(QStringLiteral("hasChange=true"), QStringLiteral("false"));
        if (!result.hasActionableChange)
            return fail(QStringLiteral("hasActionableChange=true"), QStringLiteral("false"));
        if (!result.shouldNotify)
            return fail(QStringLiteral("shouldNotify=true"), QStringLiteral("false"));
        return 0;
    } END_TEST

    TEST("仅 pulse 变化 - 视为非行动变化并静默") {
        HeartbeatSnapshotCoordinator::Inputs inputs = baseInputs();
        inputs.config.snapshotSignals = QStringList() << QStringLiteral("pulse_state");
        inputs.runtimeState.hasSnapshot = true;
        inputs.runtimeState.lastSnapshotObj.insert(QStringLiteral("watch_signals"),
                                                   QJsonArray { QStringLiteral("pulse_state") });
        inputs.runtimeState.lastSnapshotObj.insert(QStringLiteral("pulse_state"),
                                                   QStringLiteral("healthy"));
        inputs.pulseState = QStringLiteral("stalled");

        const auto result = HeartbeatSnapshotCoordinator::evaluate(inputs);
        if (!result.hasChange)
            return fail(QStringLiteral("hasChange=true"), QStringLiteral("false"));
        if (result.hasActionableChange)
            return fail(QStringLiteral("hasActionableChange=false"), QStringLiteral("true"));
        if (result.shouldNotify)
            return fail(QStringLiteral("shouldNotify=false"), QStringLiteral("true"));
        if (result.skipReason != QStringLiteral("non_actionable_change"))
            return fail(QStringLiteral("non_actionable_change"), result.skipReason);
        return 0;
    } END_TEST

    TEST("provider down - 直接跳过通知") {
        HeartbeatSnapshotCoordinator::Inputs inputs = baseInputs();
        inputs.providerDown = true;
        inputs.providerId = QStringLiteral("provider-1");

        const auto result = HeartbeatSnapshotCoordinator::evaluate(inputs);
        if (result.shouldNotify)
            return fail(QStringLiteral("shouldNotify=false"), QStringLiteral("true"));
        if (result.skipReason != QStringLiteral("provider_down"))
            return fail(QStringLiteral("provider_down"), result.skipReason);
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

