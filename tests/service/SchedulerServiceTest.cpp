#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QUuid>
#include <functional>

#define private public
#include "core/service/SchedulerService.h"
#undef private

#include "core/persistence/ChatPersistenceService.h"

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

bool waitForCondition(int timeoutMs, const std::function<bool()>& predicate)
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

struct SchedulerFixture {
    QString rootPath;
    QString agentId;

    SchedulerFixture()
    {
        rootPath = QDir::temp().filePath(
            QStringLiteral("tmagent-scheduler-test-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        agentId = QStringLiteral("scheduler-agent-%1")
                      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        qputenv("TMAGENT_TEST_SCHEDULER_ROOT", QDir::toNativeSeparators(rootPath).toUtf8());
        QDir().mkpath(rootPath);
    }

    ~SchedulerFixture()
    {
        QDir(rootPath).removeRecursively();
        qunsetenv("TMAGENT_TEST_SCHEDULER_ROOT");
    }
};

QJsonArray loadJobsArray(const ChatPersistenceService& persistence, bool* ok = nullptr)
{
    bool rootOk = false;
    const QJsonObject root = persistence.readJsonObject(persistence.scheduledJobsPath(), &rootOk);
    if (ok)
        *ok = rootOk;
    return root.value(QStringLiteral("jobs")).toArray();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "      SchedulerService 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    SchedulerFixture fixture;
    ChatPersistenceService persistence;

    TEST("reload - 缺失文件时自动创建空的 scheduled_jobs.json") {
        SchedulerService service;
        service.setPersistence(&persistence);
        if (!service.reload())
            return fail(QStringLiteral("reload 成功"), QStringLiteral("返回 false"));
        if (!QFile::exists(persistence.scheduledJobsPath()))
            return fail(QStringLiteral("scheduled_jobs.json 已创建"), QStringLiteral("文件不存在"));
        if (!service.allJobs().isEmpty())
            return fail(QStringLiteral("0 个任务"), QString::number(service.allJobs().size()));
        return 0;
    } END_TEST

    TEST("addJob - 自动生成 jobId、归一化 sessionTarget 并持久化") {
        SchedulerService service;
        service.setPersistence(&persistence);
        service.reload();

        ScheduledJob job;
        job.name = QStringLiteral("晨间简报");
        job.agentId = fixture.agentId;
        job.prompt = QStringLiteral("给我今天的简报");
        job.cronExpr = QStringLiteral("*/15 * * * *");
        job.sessionTarget = QStringLiteral("ISOLATED");

        const QString jobId = service.addJob(job);
        if (jobId.trimmed().isEmpty())
            return fail(QStringLiteral("非空 jobId"), QStringLiteral("<empty>"));

        ScheduledJob stored;
        if (!service.jobById(jobId, &stored))
            return fail(QStringLiteral("jobById 可找到任务"), QStringLiteral("返回 false"));
        if (stored.sessionTarget != QStringLiteral("isolated"))
            return fail(QStringLiteral("isolated"), stored.sessionTarget);
        if (stored.timezone.trimmed().isEmpty())
            return fail(QStringLiteral("非空 timezone"), QStringLiteral("<empty>"));
        if (!stored.nextFireAtUtc.isValid())
            return fail(QStringLiteral("nextFireAtUtc 有效"), QStringLiteral("无效时间"));
        if (stored.nextFireAtUtc <= QDateTime::currentDateTimeUtc())
            return fail(QStringLiteral("nextFireAtUtc > now"), stored.nextFireAtUtc.toString(Qt::ISODateWithMs));

        bool ok = false;
        const QJsonArray jobs = loadJobsArray(persistence, &ok);
        if (!ok || jobs.size() != 1)
            return fail(QStringLiteral("持久化 1 个任务"), QString::number(jobs.size()));
        const QJsonObject first = jobs.first().toObject();
        if (first.value(QStringLiteral("sessionTarget")).toString() != QStringLiteral("isolated"))
            return fail(QStringLiteral("isolated"), first.value(QStringLiteral("sessionTarget")).toString());
        return 0;
    } END_TEST

    TEST("updateJob - 可覆盖字段并把非法 sessionTarget 归一化为 main") {
        SchedulerService service;
        service.setPersistence(&persistence);
        service.reload();

        ScheduledJob initial;
        initial.name = QStringLiteral("跟进");
        initial.agentId = fixture.agentId;
        initial.prompt = QStringLiteral("初始 prompt");
        initial.cronExpr = QStringLiteral("0 * * * *");
        const QString jobId = service.addJob(initial);

        ScheduledJob updated;
        updated.name = QStringLiteral("晚间跟进");
        updated.agentId = fixture.agentId;
        updated.prompt = QStringLiteral("新的 prompt");
        updated.cronExpr = QStringLiteral("30 9 * * 1-5");
        updated.sessionTarget = QStringLiteral("unexpected");
        updated.enabled = false;
        if (!service.updateJob(jobId, updated))
            return fail(QStringLiteral("updateJob 成功"), QStringLiteral("返回 false"));

        ScheduledJob stored;
        if (!service.jobById(jobId, &stored))
            return fail(QStringLiteral("jobById 可找到任务"), QStringLiteral("返回 false"));
        if (stored.name != QStringLiteral("晚间跟进"))
            return fail(QStringLiteral("晚间跟进"), stored.name);
        if (stored.prompt != QStringLiteral("新的 prompt"))
            return fail(QStringLiteral("新的 prompt"), stored.prompt);
        if (stored.sessionTarget != QStringLiteral("main"))
            return fail(QStringLiteral("main"), stored.sessionTarget);
        if (stored.enabled)
            return fail(QStringLiteral("enabled=false"), QStringLiteral("true"));
        if (stored.nextFireAtUtc.isValid())
            return fail(QStringLiteral("disabled 时 nextFireAtUtc 无效"), stored.nextFireAtUtc.toString(Qt::ISODateWithMs));
        return 0;
    } END_TEST

    TEST("enableJob - 禁用时清空 nextFireAtUtc，重新启用时重新计算") {
        SchedulerService service;
        service.setPersistence(&persistence);
        service.reload();

        ScheduledJob job;
        job.name = QStringLiteral("间隔任务");
        job.agentId = fixture.agentId;
        job.prompt = QStringLiteral("检查一下");
        job.cronExpr = QStringLiteral("* * * * *");
        const QString jobId = service.addJob(job);

        ScheduledJob stored;
        service.jobById(jobId, &stored);
        if (!stored.nextFireAtUtc.isValid())
            return fail(QStringLiteral("初始 nextFireAtUtc 有效"), QStringLiteral("无效时间"));

        if (!service.enableJob(jobId, false))
            return fail(QStringLiteral("disable 成功"), QStringLiteral("返回 false"));
        service.jobById(jobId, &stored);
        if (stored.enabled)
            return fail(QStringLiteral("enabled=false"), QStringLiteral("true"));
        if (stored.nextFireAtUtc.isValid())
            return fail(QStringLiteral("禁用后 nextFireAtUtc 无效"), stored.nextFireAtUtc.toString(Qt::ISODateWithMs));

        if (!service.enableJob(jobId, true))
            return fail(QStringLiteral("enable 成功"), QStringLiteral("返回 false"));
        service.jobById(jobId, &stored);
        if (!stored.enabled)
            return fail(QStringLiteral("enabled=true"), QStringLiteral("false"));
        if (!stored.nextFireAtUtc.isValid())
            return fail(QStringLiteral("重新启用后 nextFireAtUtc 有效"), QStringLiteral("无效时间"));
        return 0;
    } END_TEST

    TEST("triggerJob - 已启用任务发出 jobFired，禁用和不存在任务发出 jobFailed") {
        SchedulerService service;
        service.setPersistence(&persistence);
        service.reload();

        ScheduledJob job;
        job.name = QStringLiteral("手动触发任务");
        job.agentId = fixture.agentId;
        job.prompt = QStringLiteral("执行一次");
        job.cronExpr = QStringLiteral("* * * * *");
        const QString jobId = service.addJob(job);

        QStringList fired;
        QStringList failed;
        QObject::connect(&service, &SchedulerService::jobFired, &app, [&](const QString& firedId, const QString&) {
            fired.append(firedId);
        });
        QObject::connect(&service, &SchedulerService::jobFailed, &app, [&](const QString& failedId, const QString& reason) {
            failed.append(failedId + QStringLiteral(":") + reason);
        });

        service.triggerJob(jobId);
        if (fired != QStringList { jobId })
            return fail(QStringLiteral("[%1]").arg(jobId), QStringLiteral("[%1]").arg(fired.join(QStringLiteral(", "))));

        service.enableJob(jobId, false);
        service.triggerJob(jobId);
        service.triggerJob(QStringLiteral("missing-job"));
        if (!failed.contains(jobId + QStringLiteral(":job_disabled")))
            return fail(QStringLiteral("包含 job_disabled"), QStringLiteral("[%1]").arg(failed.join(QStringLiteral(", "))));
        if (!failed.contains(QStringLiteral("missing-job:job_not_found")))
            return fail(QStringLiteral("包含 job_not_found"), QStringLiteral("[%1]").arg(failed.join(QStringLiteral(", "))));
        return 0;
    } END_TEST

    TEST("reload - 会忽略缺失关键字段的任务并恢复有效任务") {
        QJsonObject root;
        root.insert(QStringLiteral("schemaVersion"), 1);
        QJsonArray jobs;
        jobs.append(QJsonObject {
            { QStringLiteral("jobId"), QStringLiteral("valid-job") },
            { QStringLiteral("name"), QStringLiteral("有效任务") },
            { QStringLiteral("agentId"), fixture.agentId },
            { QStringLiteral("prompt"), QStringLiteral("执行有效任务") },
            { QStringLiteral("cronExpr"), QStringLiteral("*/10 * * * *") },
            { QStringLiteral("timezone"), QStringLiteral("UTC") },
            { QStringLiteral("enabled"), true }
        });
        jobs.append(QJsonObject {
            { QStringLiteral("jobId"), QStringLiteral("invalid-job") },
            { QStringLiteral("name"), QStringLiteral("无 prompt") },
            { QStringLiteral("agentId"), fixture.agentId },
            { QStringLiteral("cronExpr"), QStringLiteral("*/10 * * * *") }
        });
        root.insert(QStringLiteral("jobs"), jobs);
        if (!persistence.writeJsonObject(persistence.scheduledJobsPath(), root))
            return fail(QStringLiteral("写入测试文件成功"), QStringLiteral("写入失败"));

        SchedulerService service;
        service.setPersistence(&persistence);
        if (!service.reload())
            return fail(QStringLiteral("reload 成功"), QStringLiteral("返回 false"));
        if (service.allJobs().size() != 1)
            return fail(QStringLiteral("仅保留 1 个有效任务"), QString::number(service.allJobs().size()));
        ScheduledJob stored;
        if (!service.jobById(QStringLiteral("valid-job"), &stored))
            return fail(QStringLiteral("valid-job 可恢复"), QStringLiteral("返回 false"));
        if (!stored.nextFireAtUtc.isValid())
            return fail(QStringLiteral("valid-job nextFireAtUtc 有效"), QStringLiteral("无效时间"));
        return 0;
    } END_TEST

    TEST("onTick - 到点任务会触发并刷新 lastFireAtUtc / nextFireAtUtc") {
        SchedulerService service;
        service.setPersistence(&persistence);
        service.reload();

        ScheduledJob job;
        job.name = QStringLiteral("到点任务");
        job.agentId = fixture.agentId;
        job.prompt = QStringLiteral("到点执行");
        job.cronExpr = QStringLiteral("* * * * *");
        const QString jobId = service.addJob(job);

        service.m_jobs[jobId].nextFireAtUtc = QDateTime::currentDateTimeUtc().addSecs(-60);
        int firedCount = 0;
        QObject::connect(&service, &SchedulerService::jobFired, &app, [&](const QString& firedId, const QString&) {
            if (firedId == jobId)
                ++firedCount;
        });

        service.onTick();
        if (!waitForCondition(100, [&]() { return firedCount == 1; }))
            return fail(QStringLiteral("触发 1 次"), QString::number(firedCount));

        ScheduledJob stored;
        if (!service.jobById(jobId, &stored))
            return fail(QStringLiteral("job 仍存在"), QStringLiteral("返回 false"));
        if (!stored.lastFireAtUtc.isValid())
            return fail(QStringLiteral("lastFireAtUtc 有效"), QStringLiteral("无效时间"));
        if (!stored.nextFireAtUtc.isValid())
            return fail(QStringLiteral("nextFireAtUtc 有效"), QStringLiteral("无效时间"));
        if (!(stored.nextFireAtUtc > stored.lastFireAtUtc))
            return fail(QStringLiteral("nextFireAtUtc > lastFireAtUtc"),
                        QStringLiteral("%1 <= %2")
                            .arg(stored.nextFireAtUtc.toString(Qt::ISODateWithMs),
                                 stored.lastFireAtUtc.toString(Qt::ISODateWithMs)));
        return 0;
    } END_TEST

    TEST("removeJob - 可删除任务并同步落盘") {
        SchedulerService service;
        service.setPersistence(&persistence);
        service.reload();

        ScheduledJob job;
        job.name = QStringLiteral("待删除");
        job.agentId = fixture.agentId;
        job.prompt = QStringLiteral("删除我");
        job.cronExpr = QStringLiteral("* * * * *");
        const QString jobId = service.addJob(job);

        if (!service.removeJob(jobId))
            return fail(QStringLiteral("removeJob 成功"), QStringLiteral("返回 false"));
        ScheduledJob stored;
        if (service.jobById(jobId, &stored))
            return fail(QStringLiteral("job 已移除"), QStringLiteral("仍可读取"));

        bool ok = false;
        const QJsonArray jobs = loadJobsArray(persistence, &ok);
        if (!ok)
            return fail(QStringLiteral("可读取 scheduled_jobs.json"), QStringLiteral("读取失败"));
        bool found = false;
        for (const QJsonValue& value : jobs) {
            if (value.toObject().value(QStringLiteral("jobId")).toString().trimmed() == jobId) {
                found = true;
                break;
            }
        }
        if (found)
            return fail(QStringLiteral("已删除 jobId 不再出现在持久化文件中"), jobId);
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}
