#include <QCoreApplication>
#include <QDebug>

#include "HeartbeatDispatchCoordinator.h"

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

struct Fixture {
    QList<EventRecord> events;
    QList<bool> persistCalls;
    QString markedAgentId;
    QString markedTurnId;
    QString enqueuedActor;
    QString enqueuedSession;
    QString enqueuedPrompt;
    QString enqueuedClientMessageId;
    int queueDepth = 0;
};

HeartbeatDispatchCoordinator::Dependencies makeDeps(Fixture& f, const QString& enqueueResult = QStringLiteral("turn-1"))
{
    HeartbeatDispatchCoordinator::Dependencies deps;
    deps.queueDepthForSession = [&](const QString&) { return f.queueDepth; };
    deps.emitPipelineEventSimple = [&](const QString& sessionId,
                                       const QString& type,
                                       const QString& error,
                                       const QString&,
                                       const QJsonObject& extra,
                                       bool) {
        f.events.append({ sessionId, type, error, extra });
    };
    deps.userIdentityId = []() { return QStringLiteral("user-1"); };
    deps.buildHeartbeatPrompt = [](const QString& agentId, const QString& reason) {
        return QStringLiteral("HB %1 %2").arg(agentId, reason);
    };
    deps.pulseForAgent = [](const QString&) -> AgentPulse* { return nullptr; };
    deps.pulseStateText = [](AgentPulse*) { return QString(); };
    deps.enqueueUserMessageAs = [&](const QString& actorId,
                                    const QString& sessionId,
                                    const QString& prompt,
                                    const QString& clientMessageId) {
        f.enqueuedActor = actorId;
        f.enqueuedSession = sessionId;
        f.enqueuedPrompt = prompt;
        f.enqueuedClientMessageId = clientMessageId;
        return enqueueResult;
    };
    deps.buildHeartbeatClientMessageId = [](const QString& tag, const QString& uuid) {
        return QStringLiteral("%1-%2").arg(tag, uuid);
    };
    deps.markHeartbeatNotified = [&](const QString& agentId, const QString& turnId) {
        f.markedAgentId = agentId;
        f.markedTurnId = turnId;
    };
    deps.persistStateIfNeeded = [&](bool forcePersist) { f.persistCalls.append(forcePersist); };
    return deps;
}

QJsonObject triggeredExtra()
{
    QJsonObject extra;
    extra.insert(QStringLiteral("agent_id"), QStringLiteral("agent-1"));
    extra.insert(QStringLiteral("reason"), QStringLiteral("interval"));
    return extra;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "   HeartbeatDispatchCoordinator 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("pipeline busy - 直接 skipped") {
        Fixture f;
        f.queueDepth = 2;
        auto deps = makeDeps(f);
        HeartbeatDispatchCoordinator coordinator(deps);
        coordinator.dispatch(
            QStringLiteral("agent-1"),
            QStringLiteral("session-1"),
            QStringLiteral("interval"),
            false,
            false,
            false,
            false,
            false,
            false,
            QString(),
            QList<DelegateTaskScheduler::JobInfo>(),
            triggeredExtra());

        if (f.events.size() != 1)
            return fail(QStringLiteral("1 个事件"), QString::number(f.events.size()));
        if (f.events.first().type != QStringLiteral("heartbeat.skipped"))
            return fail(QStringLiteral("heartbeat.skipped"), f.events.first().type);
        if (f.events.first().error != QStringLiteral("pipeline_busy"))
            return fail(QStringLiteral("pipeline_busy"), f.events.first().error);
        if (f.persistCalls != QList<bool> { false })
            return fail(QStringLiteral("[false]"), QString::number(f.persistCalls.size()));
        return 0;
    } END_TEST

    TEST("正常 dispatch - enqueue 并发 completed") {
        Fixture f;
        auto deps = makeDeps(f, QStringLiteral("turn-1"));
        HeartbeatDispatchCoordinator coordinator(deps);
        QList<DelegateTaskScheduler::JobInfo> jobs;
        DelegateTaskScheduler::JobInfo job;
        job.jobId = QStringLiteral("job-1");
        job.status = QStringLiteral("running");
        job.summary = QStringLiteral("working");
        jobs.append(job);

        coordinator.dispatch(
            QStringLiteral("agent-1"),
            QStringLiteral("session-1"),
            QStringLiteral("interval"),
            false,
            true,
            true,
            false,
            false,
            false,
            QString(),
            jobs,
            triggeredExtra());

        if (f.enqueuedActor != QStringLiteral("user-1"))
            return fail(QStringLiteral("user-1"), f.enqueuedActor);
        if (f.enqueuedSession != QStringLiteral("session-1"))
            return fail(QStringLiteral("session-1"), f.enqueuedSession);
        if (!f.enqueuedClientMessageId.startsWith(QStringLiteral("heartbeat-bg-")))
            return fail(QStringLiteral("heartbeat-bg-*"), f.enqueuedClientMessageId);
        if (f.markedAgentId != QStringLiteral("agent-1") || f.markedTurnId != QStringLiteral("turn-1"))
            return fail(QStringLiteral("markHeartbeatNotified"), f.markedAgentId + QStringLiteral("/") + f.markedTurnId);
        if (f.events.size() != 1 || f.events.first().type != QStringLiteral("heartbeat.completed"))
            return fail(QStringLiteral("heartbeat.completed"), f.events.isEmpty() ? QStringLiteral("<none>") : f.events.first().type);
        if (f.persistCalls != QList<bool> { true })
            return fail(QStringLiteral("[true]"), QString::number(f.persistCalls.size()));
        return 0;
    } END_TEST

    TEST("enqueue 失败 - 发 heartbeat.failed") {
        Fixture f;
        auto deps = makeDeps(f, QString());
        HeartbeatDispatchCoordinator coordinator(deps);
        coordinator.dispatch(
            QStringLiteral("agent-1"),
            QStringLiteral("session-1"),
            QStringLiteral("requested"),
            true,
            false,
            false,
            false,
            false,
            false,
            QString(),
            QList<DelegateTaskScheduler::JobInfo>(),
            triggeredExtra());

        if (f.events.size() != 1)
            return fail(QStringLiteral("1 个事件"), QString::number(f.events.size()));
        if (f.events.first().type != QStringLiteral("heartbeat.failed"))
            return fail(QStringLiteral("heartbeat.failed"), f.events.first().type);
        if (f.events.first().error != QStringLiteral("enqueue_failed"))
            return fail(QStringLiteral("enqueue_failed"), f.events.first().error);
        if (f.persistCalls != QList<bool> { false })
            return fail(QStringLiteral("[false]"), QString::number(f.persistCalls.size()));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

