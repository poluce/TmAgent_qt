#include <QCoreApplication>
#include <QDebug>

#include "TurnCompletionCoordinator.h"

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

struct UpdateRecord {
    QString sessionId;
    QString state;
    QString turnId;
    QJsonObject extra;
};

struct EventRecord {
    QString sessionId;
    QString type;
    QString error;
    QJsonObject extra;
};

struct MessageRecord {
    QString sessionId;
    Message message;
};

struct Fixture {
    TurnManager turnManager;
    QList<UpdateRecord> updates;
    QList<EventRecord> events;
    QList<MessageRecord> messages;
    QStringList finishedSignals;
    QStringList pulseReports;
    QString suppressedDigest;
    QString deliveredDigest;
    QString manualSuppressedDigest;
    QList<QJsonObject> persistedContexts;
    QHash<QString, QString> activeSessionByAgent;
};

TurnTask makeActiveTurn(Fixture& f,
                        const QString& sessionId,
                        const QString& clientMessageId = QString(),
                        const QString& userContent = QStringLiteral("hello"))
{
    TurnTask turn;
    turn.turnId = QStringLiteral("turn-1");
    turn.requestTraceId = QStringLiteral("trace-1");
    turn.runId = QStringLiteral("run-1");
    turn.actorIdentityId = QStringLiteral("user-1");
    turn.clientMessageId = clientMessageId;
    turn.userContent = userContent;
    turn.enqueuedAtMs = 1;

    f.turnManager.ensurePipeline(sessionId);
    f.turnManager.enqueueTurn(sessionId, turn);
    f.turnManager.startNextTurn(sessionId);
    return turn;
}

TurnCompletionCoordinator::Dependencies makeDeps(Fixture& f,
                                                 const QJsonObject& taskState = QJsonObject(),
                                                 const QString& agentId = QStringLiteral("agent-1"))
{
    TurnCompletionCoordinator::Dependencies deps;
    deps.findPipeline = [&](const QString& sessionId) { return f.turnManager.findPipeline(sessionId); };
    deps.activeTurn = [&](const QString& sessionId) { return f.turnManager.activeTurn(sessionId); };
    deps.flushPendingDeltaLog = [](const QString&, SessionPipeline*, const TurnTask*, bool) {};
    deps.turnManager = &f.turnManager;
    deps.clearDelegateStartsForSession = [](const QString&) {};
    deps.clearToolProgressCacheForSession = [](const QString&) {};
    deps.ctx.agentIdentityIdForSession = [agentId](const QString&) { return agentId; };
    deps.activeSessionForAgent = [&](const QString& id) { return f.activeSessionByAgent.value(id); };
    deps.clearActiveSessionForAgent = [&](const QString& id) { f.activeSessionByAgent.remove(id); };
    deps.resetSessionStreamState = [](const QString&) {};
    deps.tryStartNextTurn = [](const QString&) {};
    deps.tryStartNextTurnForAgent = [](const QString&) {};
    deps.ctx.isBackgroundClientMessage = [](const QString& clientMessageId) {
        return clientMessageId.startsWith(QStringLiteral("heartbeat-bg-"));
    };
    deps.ctx.isHeartbeatClientMessage = [](const QString& clientMessageId) {
        return clientMessageId.startsWith(QStringLiteral("heartbeat-bg-"))
            || clientMessageId.startsWith(QStringLiteral("heartbeat-manual-"));
    };
    deps.isManualHeartbeatClientMessage = [](const QString& clientMessageId) {
        return clientMessageId.startsWith(QStringLiteral("heartbeat-manual-"));
    };
    deps.ctx.reportPulseProgress = [&](const QString& aid, const QString& summary) {
        f.pulseReports.append(aid + QStringLiteral(":") + summary);
    };
    deps.taskStateForSession = [taskState](const QString&) { return taskState; };
    deps.ctx.updateTaskState = [&](const QString& sessionId,
                                   const QString& state,
                                   const TurnTask* turn,
                                   const QJsonObject& extra) {
        f.updates.append({ sessionId, state, turn ? turn->turnId : QString(), extra });
    };
    deps.ctx.taskStateTextPreview = [](const QString& text, int maxChars) { return text.left(maxChars); };
    deps.heartbeatDuplicateWindowMs = [](const QString&) { return 24 * 60 * 60 * 1000; };
    deps.heartbeatLastDeliveredAt = [](const QString&) { return QDateTime(); };
    deps.heartbeatLastDeliveredDigest = [](const QString&) { return QString(); };
    deps.recordHeartbeatSuppressed = [&](const QString&, const QString& digest, const QString&, const QDateTime&) {
        f.suppressedDigest = digest;
    };
    deps.recordHeartbeatDelivered = [&](const QString&, const QString& digest, const QString&, const QDateTime&) {
        f.deliveredDigest = digest;
    };
    deps.recordHeartbeatManualSuppress = [&](const QString&, const QString& digest, const QString&, const QDateTime&) {
        f.manualSuppressedDigest = digest;
    };
    deps.ctx.postMessage = [&](const QString& sessionId, const Message& message) {
        f.messages.append({ sessionId, message });
    };
    deps.persistCompletionContext = [&](const QString&,
                                        const TurnTask&,
                                        const QJsonObject& contextState,
                                        const QDateTime&) {
        f.persistedContexts.append(contextState);
    };
    deps.emitFinished = [&](const QString& sessionId, const QString& content) {
        f.finishedSignals.append(sessionId + QStringLiteral(":") + content);
    };
    deps.ctx.emitPipelineEvent = [&](const QString& sessionId,
                                     const QString& type,
                                     const TurnTask*,
                                     const QString&,
                                     const QString& error,
                                     const QJsonObject& extra,
                                     bool) {
        f.events.append({ sessionId, type, error, extra });
    };
    deps.reflectionEnabled = []() { return false; };
    deps.refreshMemoryIndexAndEmit =
        [](const QString&, const QString&, const TurnTask*, const QString&, const QString&, const QJsonObject&) {};
    deps.maybeReflectMemoryAndEmit =
        [](const QString&, const QString&, const TurnTask&, bool, const QString&) {};
    return deps;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << " TurnCompletionCoordinator 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("普通完成 - 写 done、落 assistant 消息、发 finished + turn_completed") {
        Fixture f;
        makeActiveTurn(f, QStringLiteral("session-1"));
        f.activeSessionByAgent.insert(QStringLiteral("agent-1"), QStringLiteral("session-1"));
        auto deps = makeDeps(f);
        TurnCompletionCoordinator coordinator(deps);
        const auto result = coordinator.onRuntimeFinished(QStringLiteral("session-1"), QStringLiteral("answer"));

        if (!result.valid)
            return fail(QStringLiteral("valid=true"), QStringLiteral("false"));
        if (f.updates.size() != 1 || f.updates.first().state != QStringLiteral("done"))
            return fail(QStringLiteral("done"), f.updates.isEmpty() ? QStringLiteral("<none>") : f.updates.first().state);
        if (f.messages.size() != 1)
            return fail(QStringLiteral("1 条 assistant 消息"), QString::number(f.messages.size()));
        if (f.finishedSignals != QStringList { QStringLiteral("session-1:answer") })
            return fail(QStringLiteral("finished(answer)"), f.finishedSignals.join(QStringLiteral(",")));
        if (f.events.size() != 1 || f.events.first().type != QStringLiteral("turn_completed"))
            return fail(QStringLiteral("turn_completed"), f.events.isEmpty() ? QStringLiteral("<none>") : f.events.first().type);
        if (f.persistedContexts.size() != 1)
            return fail(QStringLiteral("1 次上下文持久化"), QString::number(f.persistedContexts.size()));
        return 0;
    } END_TEST

    TEST("blocked 同一 turn - 不再写 done") {
        Fixture f;
        makeActiveTurn(f, QStringLiteral("session-1"));
        QJsonObject taskState;
        taskState.insert(QStringLiteral("state"), QStringLiteral("blocked"));
        taskState.insert(QStringLiteral("turn_id"), QStringLiteral("turn-1"));
        auto deps = makeDeps(f, taskState);
        TurnCompletionCoordinator coordinator(deps);
        const auto result = coordinator.onRuntimeFinished(QStringLiteral("session-1"), QStringLiteral("answer"));

        if (!result.valid)
            return fail(QStringLiteral("valid=true"), QStringLiteral("false"));
        if (!f.updates.isEmpty())
            return fail(QStringLiteral("0 次状态更新"), QString::number(f.updates.size()));
        return 0;
    } END_TEST

    TEST("后台 heartbeat 无关键更新 - 静默并发 heartbeat.skipped") {
        Fixture f;
        makeActiveTurn(f, QStringLiteral("session-1"), QStringLiteral("heartbeat-bg-1"));
        auto deps = makeDeps(f);
        TurnCompletionCoordinator coordinator(deps);
        const auto result = coordinator.onRuntimeFinished(QStringLiteral("session-1"), QStringLiteral("当前无关键更新。"));

        if (!result.valid)
            return fail(QStringLiteral("valid=true"), QStringLiteral("false"));
        if (!result.skipMemoryForHeartbeat)
            return fail(QStringLiteral("skipMemoryForHeartbeat=true"), QStringLiteral("false"));
        if (!f.finishedSignals.isEmpty())
            return fail(QStringLiteral("finished 不触发"), f.finishedSignals.join(QStringLiteral(",")));
        QStringList actualEvents;
        for (const EventRecord& event : f.events)
            actualEvents.append(event.type);
        if (!actualEvents.contains(QStringLiteral("turn_completed")))
            return fail(QStringLiteral("包含 turn_completed"), actualEvents.join(QStringLiteral(",")));
        if (!actualEvents.contains(QStringLiteral("heartbeat.skipped")))
            return fail(QStringLiteral("包含 heartbeat.skipped"), actualEvents.join(QStringLiteral(",")));
        if (!actualEvents.contains(QStringLiteral("memory.skipped")))
            return fail(QStringLiteral("包含 memory.skipped"), actualEvents.join(QStringLiteral(",")));
        return 0;
    } END_TEST

    TEST("手动 heartbeat - 即使是 heartbeat 也应 finished，并记录 delivered") {
        Fixture f;
        makeActiveTurn(f, QStringLiteral("session-1"), QStringLiteral("heartbeat-manual-1"));
        auto deps = makeDeps(f);
        TurnCompletionCoordinator coordinator(deps);
        const auto result = coordinator.onRuntimeFinished(QStringLiteral("session-1"), QStringLiteral("manual answer"));

        if (!result.valid)
            return fail(QStringLiteral("valid=true"), QStringLiteral("false"));
        if (f.finishedSignals != QStringList { QStringLiteral("session-1:manual answer") })
            return fail(QStringLiteral("finished(manual answer)"), f.finishedSignals.join(QStringLiteral(",")));
        if (f.deliveredDigest.trimmed().isEmpty())
            return fail(QStringLiteral("recordHeartbeatDelivered 被调用"), QStringLiteral("<empty>"));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

