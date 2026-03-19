#include <QCoreApplication>
#include <QDebug>

#include "BackgroundTaskCoordinator.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"

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

struct EventRecord {
    QString sessionId;
    QString type;
    QString error;
    QJsonObject extra;
};

struct UpdateRecord {
    QString sessionId;
    QString state;
    QJsonObject extra;
};

struct Fixture {
    QString resolvedSessionId = QStringLiteral("session-1");
    QList<Message> postedMessages;
    QList<UpdateRecord> updates;
    QList<QPair<QString, QString>> heartbeatTriggers;
    QList<EventRecord> events;
    QString enqueueActor;
    QString enqueueSession;
    QString enqueuePrompt;
    QString enqueueClientMessageId;

    BackgroundTaskCoordinator::Dependencies makeDeps()
    {
        BackgroundTaskCoordinator::Dependencies deps;
        deps.ctx.postMessage = [this](const QString&, const Message& message) {
            postedMessages.append(message);
        };
        deps.ctx.taskStateTextPreview = [](const QString& text, int maxChars) {
            return text.left(maxChars);
        };
        deps.jobById = [](const QString&, ScheduledJob*) { return false; };
        deps.findIdentity = [](const QString&) -> Identity* { return nullptr; };
        deps.userIdentityId = []() { return QStringLiteral("user-1"); };
        deps.buildClientMessageId = [](const QString&, const QString&, const QString&, const QString&) {
            return QString();
        };
        deps.buildPrompt = [](const QString&, const QString&, const QString&, const QString&) {
            return QString();
        };
        deps.enqueueUserMessageAs = [this](const QString& actorId,
                                           const QString& sessionId,
                                           const QString& prompt,
                                           const QString& clientMessageId) {
            enqueueActor = actorId;
            enqueueSession = sessionId;
            enqueuePrompt = prompt;
            enqueueClientMessageId = clientMessageId;
            return QStringLiteral("turn-1");
        };
        deps.triggerHeartbeat = [this](const QString& agentId, const QString& reason) {
            heartbeatTriggers.append({ agentId, reason });
        };
        deps.updateTaskStateForSession = [this](const QString& sid,
                                                const QString& state,
                                                const void*,
                                                const QJsonObject& extra) {
            updates.append({ sid, state, extra });
        };
        deps.resolvePrimarySessionForAgent = [this](const QString&, bool, bool, const QString&) {
            return resolvedSessionId;
        };
        deps.emitPipelineEventSimple = [this](const QString& sid,
                                              const QString& type,
                                              const QString& error,
                                              const QString&,
                                              const QJsonObject& extra,
                                              bool) {
            events.append({ sid, type, error, extra });
        };
        return deps;
    }
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
    qDebug().noquote() << " BackgroundTaskCoordinator 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("job 缺失 - 发 scheduler.failed(job_not_found)") {
        Fixture f;
        auto deps = f.makeDeps();
        BackgroundTaskCoordinator coordinator(deps);
        coordinator.onScheduledJobTriggered(QStringLiteral("missing"), QStringLiteral("name"));

        if (f.events.size() != 1)
            return fail(QStringLiteral("1 个事件"), QString::number(f.events.size()));
        if (f.events.first().type != QStringLiteral("scheduler.failed"))
            return fail(QStringLiteral("scheduler.failed"), f.events.first().type);
        if (f.events.first().error != QStringLiteral("job_not_found"))
            return fail(QStringLiteral("job_not_found"), f.events.first().error);
        return 0;
    } END_TEST

    TEST("正常触发 scheduler - fired + completed") {
        Fixture f;
        ScheduledJob job = makeJob();
        auto* agent =
            Identity::createAgent(QStringLiteral("Agent"), new IdentityProfile(), nullptr, job.agentId);
        auto deps = f.makeDeps();
        deps.jobById = [&](const QString&, ScheduledJob* outJob) {
            if (outJob)
                *outJob = job;
            return true;
        };
        deps.findIdentity = [&](const QString& identityId) -> Identity* {
            return identityId == job.agentId ? agent : nullptr;
        };
        deps.buildClientMessageId = [](const QString& jobId,
                                       const QString& uuid,
                                       const QString&,
                                       const QString&) {
            return QStringLiteral("scheduler-%1-%2").arg(jobId, uuid);
        };
        deps.buildPrompt = [](const QString& jobNameArg,
                              const QString&,
                              const QString& prompt,
                              const QString&) {
            return QStringLiteral("【定时任务:%1】\n%2").arg(jobNameArg, prompt);
        };

        BackgroundTaskCoordinator coordinator(deps);
        coordinator.onScheduledJobTriggered(job.jobId, job.name);

        if (f.enqueueActor != QStringLiteral("user-1"))
            return fail(QStringLiteral("user-1"), f.enqueueActor);
        if (f.enqueueSession != QStringLiteral("session-1"))
            return fail(QStringLiteral("session-1"), f.enqueueSession);
        if (!f.enqueuePrompt.contains(QStringLiteral("【定时任务:Daily Brief】")))
            return fail(QStringLiteral("prompt 包含任务标题"), f.enqueuePrompt);
        if (!f.enqueueClientMessageId.startsWith(QStringLiteral("scheduler-job-1-")))
            return fail(QStringLiteral("scheduler-job-1-*"), f.enqueueClientMessageId);
        if (f.events.size() != 2)
            return fail(QStringLiteral("2 个事件"), QString::number(f.events.size()));
        if (f.events.at(0).type != QStringLiteral("scheduler.fired"))
            return fail(QStringLiteral("scheduler.fired"), f.events.at(0).type);
        if (f.events.at(1).type != QStringLiteral("scheduler.completed"))
            return fail(QStringLiteral("scheduler.completed"), f.events.at(1).type);
        delete agent;
        return 0;
    } END_TEST

    TEST("delegate 完成成功 - 注入通知、done 状态、heartbeat 与事件") {
        Fixture f;
        auto deps = f.makeDeps();
        BackgroundTaskCoordinator coordinator(deps);
        coordinator.onDelegateJobSettled(QStringLiteral("job-42"),
                                         QStringLiteral("agent-1"),
                                         true,
                                         QStringLiteral("child finished successfully"));

        if (f.postedMessages.size() != 1)
            return fail(QStringLiteral("1 条系统消息"), QString::number(f.postedMessages.size()));
        if (!f.postedMessages.first().content.text.contains(QStringLiteral("job_id: job-42")))
            return fail(QStringLiteral("通知包含 job_id"), f.postedMessages.first().content.text);
        if (f.updates.size() != 1 || f.updates.first().state != QStringLiteral("done"))
            return fail(QStringLiteral("done"),
                        f.updates.isEmpty() ? QStringLiteral("<none>") : f.updates.first().state);
        if (f.heartbeatTriggers != QList<QPair<QString, QString>> {
                { QStringLiteral("agent-1"), QStringLiteral("delegate_job_settled") } })
            return fail(QStringLiteral("触发 agent-1 heartbeat"),
                        QStringLiteral("数量=%1").arg(f.heartbeatTriggers.size()));
        if (f.events.size() != 1 || f.events.first().type != QStringLiteral("delegate.job_settled"))
            return fail(QStringLiteral("delegate.job_settled"),
                        f.events.isEmpty() ? QStringLiteral("<none>") : f.events.first().type);
        return 0;
    } END_TEST

    TEST("delegate 完成失败 - failed 状态写入 last_error") {
        Fixture f;
        auto deps = f.makeDeps();
        BackgroundTaskCoordinator coordinator(deps);
        coordinator.onDelegateJobSettled(
            QStringLiteral("job-99"),
            QStringLiteral("agent-2"),
            false,
            QStringLiteral("delegate child failed because upstream timed out"));

        if (f.updates.size() != 1)
            return fail(QStringLiteral("1 次状态更新"), QString::number(f.updates.size()));
        if (f.updates.first().state != QStringLiteral("failed"))
            return fail(QStringLiteral("failed"), f.updates.first().state);
        const QString lastError = f.updates.first().extra.value(QStringLiteral("last_error")).toString();
        if (lastError.trimmed().isEmpty())
            return fail(QStringLiteral("非空 last_error"), QStringLiteral("<empty>"));
        if (f.events.size() != 1 || f.events.first().extra.value(QStringLiteral("success")).toBool())
            return fail(QStringLiteral("事件 success=false"),
                        f.events.isEmpty()
                            ? QStringLiteral("<none>")
                            : QString::number(f.events.first().extra.value(QStringLiteral("success")).toBool()));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}
