#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimeZone>
#include <QUuid>

#include "core/persistence/ChatPersistenceService.h"
#include "core/service/HeartbeatService.h"

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

static int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
    return 1;
}

static bool waitForCondition(int timeoutMs, const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

struct HeartbeatTestFixture {
    QString rootPath;
    QString agentId;

    HeartbeatTestFixture()
    {
        rootPath = QDir::temp().filePath(
            QStringLiteral("tmagent-heartbeat-test-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        agentId = QStringLiteral("heartbeat-agent-%1")
                      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        qputenv("TMAGENT_TEST_HEARTBEAT_ROOT", QDir::toNativeSeparators(rootPath).toUtf8());
        QDir().mkpath(rootPath);
    }

    ~HeartbeatTestFixture()
    {
        QDir(rootPath).removeRecursively();
        qunsetenv("TMAGENT_TEST_HEARTBEAT_ROOT");
    }
};

static QString joinStrings(const QStringList& values)
{
    return QStringLiteral("[") + values.join(QStringLiteral(", ")) + QStringLiteral("]");
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "      HeartbeatService 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    HeartbeatTestFixture fixture;
    ChatPersistenceService persistence;

    TEST("updateConfig/configForAgent - 配置可持久化并归一化快照信号") {
        HeartbeatService service;
        service.setPersistence(&persistence);

        HeartbeatConfig cfg;
        cfg.enabled = true;
        cfg.intervalMs = 5000;
        cfg.coalesceMs = 25;
        cfg.snapshotSignals = QStringList()
            << QStringLiteral("provider")
            << QStringLiteral("delegate_jobs")
            << QStringLiteral("provider_status");
        cfg.activeHours.start = QTime(8, 0);
        cfg.activeHours.end = QTime(23, 0);
        cfg.activeHours.timezone = QStringLiteral("Asia/Shanghai");

        service.updateConfig(fixture.agentId, cfg);

        HeartbeatService reloaded;
        reloaded.setPersistence(&persistence);
        const HeartbeatConfig loaded = reloaded.configForAgent(fixture.agentId);
        const QStringList expectedSignals {
            QStringLiteral("provider_status"),
            QStringLiteral("delegate_jobs")
        };
        if (loaded.intervalMs != 5000)
            return fail(QStringLiteral("intervalMs=5000"), QString::number(loaded.intervalMs));
        if (loaded.coalesceMs != 25)
            return fail(QStringLiteral("coalesceMs=25"), QString::number(loaded.coalesceMs));
        if (loaded.snapshotSignals != expectedSignals)
            return fail(joinStrings(expectedSignals), joinStrings(loaded.snapshotSignals));
        if (loaded.activeHours.timezone != QStringLiteral("Asia/Shanghai"))
            return fail(QStringLiteral("Asia/Shanghai"), loaded.activeHours.timezone);
        return 0;
    } END_TEST

    TEST("triggerHeartbeat - 禁用状态直接 skipped") {
        HeartbeatService service;
        service.setPersistence(&persistence);

        HeartbeatConfig cfg;
        cfg.enabled = false;
        service.updateConfig(fixture.agentId, cfg);

        QStringList skippedReasons;
        QObject::connect(&service, &HeartbeatService::heartbeatSkipped, &app, [&](const QString& agentId, const QString& reason) {
            if (agentId == fixture.agentId)
                skippedReasons.append(reason);
        });

        service.triggerHeartbeat(fixture.agentId, QStringLiteral("manual_test"));
        if (!waitForCondition(200, [&]() { return !skippedReasons.isEmpty(); }))
            return fail(QStringLiteral("收到 skipped=disabled"), QStringLiteral("未收到 skipped"));
        if (skippedReasons.first() != QStringLiteral("disabled"))
            return fail(QStringLiteral("disabled"), skippedReasons.first());
        return 0;
    } END_TEST

    TEST("triggerHeartbeat - 活跃时间窗口外不触发") {
        HeartbeatService service;
        service.setPersistence(&persistence);

        const QTime nowUtc = QDateTime::currentDateTimeUtc().time();
        HeartbeatConfig cfg;
        cfg.enabled = true;
        cfg.activeHours.start = nowUtc.addSecs(120);
        cfg.activeHours.end = nowUtc.addSecs(180);
        cfg.activeHours.timezone = QStringLiteral("UTC");
        service.updateConfig(fixture.agentId, cfg);

        QStringList skippedReasons;
        QObject::connect(&service, &HeartbeatService::heartbeatSkipped, &app, [&](const QString& agentId, const QString& reason) {
            if (agentId == fixture.agentId)
                skippedReasons.append(reason);
        });

        service.triggerHeartbeat(fixture.agentId, QStringLiteral("manual_window_check"));
        if (!waitForCondition(200, [&]() { return !skippedReasons.isEmpty(); }))
            return fail(QStringLiteral("收到 skipped=outside_active_hours"), QStringLiteral("未收到 skipped"));
        if (skippedReasons.first() != QStringLiteral("outside_active_hours"))
            return fail(QStringLiteral("outside_active_hours"), skippedReasons.first());
        return 0;
    } END_TEST

    TEST("suppress/unsuppress - 恢复后会补触发一次") {
        HeartbeatService service;
        service.setPersistence(&persistence);

        HeartbeatConfig cfg;
        cfg.enabled = true;
        cfg.intervalMs = 1000;
        cfg.coalesceMs = 10;
        cfg.activeHours.start = QTime(0, 0);
        cfg.activeHours.end = QTime(23, 59);
        cfg.activeHours.timezone = QStringLiteral("UTC");
        service.updateConfig(fixture.agentId, cfg);
        service.startHeartbeat(fixture.agentId);
        service.suppressHeartbeat(fixture.agentId, QStringLiteral("delegate_running"));

        QStringList skippedReasons;
        QStringList triggeredReasons;
        QObject::connect(&service, &HeartbeatService::heartbeatSkipped, &app, [&](const QString& agentId, const QString& reason) {
            if (agentId == fixture.agentId)
                skippedReasons.append(reason);
        });
        QObject::connect(&service, &HeartbeatService::heartbeatTriggered, &app, [&](const QString& agentId, const QString& reason) {
            if (agentId == fixture.agentId)
                triggeredReasons.append(reason);
        });

        service.triggerHeartbeat(fixture.agentId, QStringLiteral("while_suppressed"));
        if (!waitForCondition(200, [&]() { return !skippedReasons.isEmpty(); }))
            return fail(QStringLiteral("收到 skipped=suppressed"), QStringLiteral("未收到 skipped"));
        if (skippedReasons.first() != QStringLiteral("suppressed"))
            return fail(QStringLiteral("suppressed"), skippedReasons.first());

        QThread::msleep(1100);
        service.unsuppressHeartbeat(fixture.agentId);
        if (!waitForCondition(400, [&]() { return !triggeredReasons.isEmpty(); }))
            return fail(QStringLiteral("恢复后触发 heartbeat"), QStringLiteral("未触发"));
        if (triggeredReasons.first() != QStringLiteral("resume_after_suppress"))
            return fail(QStringLiteral("resume_after_suppress"), triggeredReasons.first());
        return 0;
    } END_TEST

    TEST("startHeartbeat - 到达间隔后自动触发") {
        HeartbeatService service;
        service.setPersistence(&persistence);

        HeartbeatConfig cfg;
        cfg.enabled = true;
        cfg.intervalMs = 1000;
        cfg.coalesceMs = 10;
        cfg.activeHours.start = QTime(0, 0);
        cfg.activeHours.end = QTime(23, 59);
        cfg.activeHours.timezone = QStringLiteral("UTC");
        service.updateConfig(fixture.agentId, cfg);

        int triggerCount = 0;
        QObject::connect(&service, &HeartbeatService::heartbeatTriggered, &app, [&](const QString& agentId, const QString&) {
            if (agentId == fixture.agentId)
                ++triggerCount;
        });

        service.startHeartbeat(fixture.agentId);
        if (!waitForCondition(2500, [&]() { return triggerCount >= 1; }))
            return fail(QStringLiteral("1 次自动触发"), QStringLiteral("0 次"));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}
