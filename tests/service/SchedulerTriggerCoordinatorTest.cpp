#include <QCoreApplication>
#include <QDebug>

#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "SchedulerTriggerCoordinator.h"

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

struct EmittedEvent {
    QString sessionId;
    QString type;
    QString error;
    QJsonObject extra;
};

ScheduledJob makeJob()
{
    ScheduledJob job;
    job.jobId = QStringLiteral("job-1");
    job.name = QStringLiteral("Daily Brief");
    job.agentId = QStringLiteral("agent-1");
    job.prompt = QStringLiteral("summarize the day");
    job.cronExpr = QStringLiteral("0 9 * * *");
    job.sessionTarget = QStringLiteral("main");
    job.enabled = true;
    return job;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "   SchedulerTriggerCoordinator 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("job 缺失 - 发 scheduler.failed(job_not_found)") {
        QList<EmittedEvent> events;
        SchedulerTriggerCoordinator::Dependencies deps;
        deps.jobById = [](const QString&, ScheduledJob*) { return false; };
        deps.findIdentity = [](const QString&) -> Identity* { return nullptr; };
        deps.resolvePrimarySessionForAgent = [](const QString&, bool, bool, const QString&) { return QString(); };
        deps.userIdentityId = []() { return QStringLiteral("user-1"); };
        deps.buildClientMessageId = [](const QString&, const QString&, const QString&, const QString&) { return QString(); };
        deps.buildPrompt = [](const QString&, const QString&, const QString&, const QString&) { return QString(); };
        deps.enqueueUserMessageAs = [](const QString&, const QString&, const QString&, const QString&) { return QString(); };
        deps.emitPipelineEventSimple = [&](const QString& sessionId,
                                           const QString& type,
                                           const QString& error,
                                           const QString&,
                                           const QJsonObject& extra,
                                           bool) {
            events.append({ sessionId, type, error, extra });
        };

        SchedulerTriggerCoordinator coordinator(deps);
        coordinator.onScheduledJobTriggered(QStringLiteral("missing"), QStringLiteral("name"));

        if (events.size() != 1)
            return fail(QStringLiteral("1 个事件"), QString::number(events.size()));
        if (events.first().type != QStringLiteral("scheduler.failed"))
            return fail(QStringLiteral("scheduler.failed"), events.first().type);
        if (events.first().error != QStringLiteral("job_not_found"))
            return fail(QStringLiteral("job_not_found"), events.first().error);
        return 0;
    } END_TEST

    TEST("agent 缺失 - 发 scheduler.failed(agent_not_found)") {
        QList<EmittedEvent> events;
        ScheduledJob job = makeJob();

        SchedulerTriggerCoordinator::Dependencies deps;
        deps.jobById = [&](const QString&, ScheduledJob* outJob) {
            if (outJob)
                *outJob = job;
            return true;
        };
        deps.findIdentity = [](const QString&) -> Identity* { return nullptr; };
        deps.resolvePrimarySessionForAgent = [](const QString&, bool, bool, const QString&) { return QString(); };
        deps.userIdentityId = []() { return QStringLiteral("user-1"); };
        deps.buildClientMessageId = [](const QString&, const QString&, const QString&, const QString&) { return QString(); };
        deps.buildPrompt = [](const QString&, const QString&, const QString&, const QString&) { return QString(); };
        deps.enqueueUserMessageAs = [](const QString&, const QString&, const QString&, const QString&) { return QString(); };
        deps.emitPipelineEventSimple = [&](const QString& sessionId,
                                           const QString& type,
                                           const QString& error,
                                           const QString&,
                                           const QJsonObject& extra,
                                           bool) {
            events.append({ sessionId, type, error, extra });
        };

        SchedulerTriggerCoordinator coordinator(deps);
        coordinator.onScheduledJobTriggered(job.jobId, job.name);

        if (events.size() != 1)
            return fail(QStringLiteral("1 个事件"), QString::number(events.size()));
        if (events.first().error != QStringLiteral("agent_not_found"))
            return fail(QStringLiteral("agent_not_found"), events.first().error);
        return 0;
    } END_TEST

    TEST("正常触发 - fired + completed，且 enqueue 收到用户和 session") {
        QList<EmittedEvent> events;
        ScheduledJob job = makeJob();
        auto* agent = Identity::createAgent(QStringLiteral("Agent"), new IdentityProfile(), nullptr, job.agentId);
        QString enqueueActor;
        QString enqueueSession;
        QString enqueuePrompt;
        QString enqueueClientMessageId;

        SchedulerTriggerCoordinator::Dependencies deps;
        deps.jobById = [&](const QString&, ScheduledJob* outJob) {
            if (outJob)
                *outJob = job;
            return true;
        };
        deps.findIdentity = [&](const QString& identityId) -> Identity* {
            return identityId == job.agentId ? agent : nullptr;
        };
        deps.resolvePrimarySessionForAgent = [](const QString&, bool, bool, const QString&) {
            return QStringLiteral("session-1");
        };
        deps.userIdentityId = []() { return QStringLiteral("user-1"); };
        deps.buildClientMessageId = [](const QString& jobId, const QString& uuid, const QString&, const QString&) {
            return QStringLiteral("scheduler-%1-%2").arg(jobId, uuid);
        };
        deps.buildPrompt = [](const QString& jobNameArg, const QString&, const QString& prompt, const QString&) {
            return QStringLiteral("【定时任务:%1】\n%2").arg(jobNameArg, prompt);
        };
        deps.enqueueUserMessageAs = [&](const QString& actorId,
                                        const QString& sessionId,
                                        const QString& prompt,
                                        const QString& clientMessageId) {
            enqueueActor = actorId;
            enqueueSession = sessionId;
            enqueuePrompt = prompt;
            enqueueClientMessageId = clientMessageId;
            return QStringLiteral("turn-1");
        };
        deps.emitPipelineEventSimple = [&](const QString& sessionId,
                                           const QString& type,
                                           const QString& error,
                                           const QString&,
                                           const QJsonObject& extra,
                                           bool) {
            events.append({ sessionId, type, error, extra });
        };

        SchedulerTriggerCoordinator coordinator(deps);
        coordinator.onScheduledJobTriggered(job.jobId, job.name);

        if (enqueueActor != QStringLiteral("user-1"))
            return fail(QStringLiteral("user-1"), enqueueActor);
        if (enqueueSession != QStringLiteral("session-1"))
            return fail(QStringLiteral("session-1"), enqueueSession);
        if (!enqueuePrompt.contains(QStringLiteral("【定时任务:Daily Brief】")))
            return fail(QStringLiteral("prompt 包含任务标题"), enqueuePrompt);
        if (!enqueueClientMessageId.startsWith(QStringLiteral("scheduler-job-1-")))
            return fail(QStringLiteral("scheduler-job-1-*"), enqueueClientMessageId);
        if (events.size() != 2)
            return fail(QStringLiteral("2 个事件"), QString::number(events.size()));
        if (events.at(0).type != QStringLiteral("scheduler.fired"))
            return fail(QStringLiteral("scheduler.fired"), events.at(0).type);
        if (events.at(1).type != QStringLiteral("scheduler.completed"))
            return fail(QStringLiteral("scheduler.completed"), events.at(1).type);
        delete agent;
        return 0;
    } END_TEST

    TEST("enqueue 失败 - fired 后发 scheduler.failed(enqueue_failed)") {
        QList<EmittedEvent> events;
        ScheduledJob job = makeJob();
        auto* agent = Identity::createAgent(QStringLiteral("Agent"), new IdentityProfile(), nullptr, job.agentId);

        SchedulerTriggerCoordinator::Dependencies deps;
        deps.jobById = [&](const QString&, ScheduledJob* outJob) {
            if (outJob)
                *outJob = job;
            return true;
        };
        deps.findIdentity = [&](const QString& identityId) -> Identity* {
            return identityId == job.agentId ? agent : nullptr;
        };
        deps.resolvePrimarySessionForAgent = [](const QString&, bool, bool, const QString&) {
            return QStringLiteral("session-1");
        };
        deps.userIdentityId = []() { return QStringLiteral("user-1"); };
        deps.buildClientMessageId = [](const QString& jobId, const QString& uuid, const QString&, const QString&) {
            return QStringLiteral("scheduler-%1-%2").arg(jobId, uuid);
        };
        deps.buildPrompt = [](const QString& jobNameArg, const QString&, const QString& prompt, const QString&) {
            return QStringLiteral("【定时任务:%1】\n%2").arg(jobNameArg, prompt);
        };
        deps.enqueueUserMessageAs = [](const QString&, const QString&, const QString&, const QString&) {
            return QString();
        };
        deps.emitPipelineEventSimple = [&](const QString& sessionId,
                                           const QString& type,
                                           const QString& error,
                                           const QString&,
                                           const QJsonObject& extra,
                                           bool) {
            events.append({ sessionId, type, error, extra });
        };

        SchedulerTriggerCoordinator coordinator(deps);
        coordinator.onScheduledJobTriggered(job.jobId, job.name);

        if (events.size() != 2)
            return fail(QStringLiteral("2 个事件"), QString::number(events.size()));
        if (events.at(1).type != QStringLiteral("scheduler.failed"))
            return fail(QStringLiteral("scheduler.failed"), events.at(1).type);
        if (events.at(1).error != QStringLiteral("enqueue_failed"))
            return fail(QStringLiteral("enqueue_failed"), events.at(1).error);
        delete agent;
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

