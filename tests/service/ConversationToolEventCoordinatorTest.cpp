#include <QCoreApplication>
#include <QDebug>

#include "ConversationToolEventCoordinator.h"

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

struct Fixture {
    TurnTask activeTurn;
    QList<UpdateRecord> updates;
    QList<EventRecord> events;
    QStringList suppressed;
    QStringList unsuppressed;
    QStringList pulseReports;
    QHash<QString, qint64> startMsByKey;
    ConversationToolEventCoordinator::DelegateStats stats;

    Fixture()
    {
        activeTurn.turnId = QStringLiteral("turn-1");
        activeTurn.requestTraceId = QStringLiteral("trace-1");
        activeTurn.userContent = QStringLiteral("delegate work");
    }
};

ConversationToolEventCoordinator::Dependencies makeDeps(Fixture& f)
{
    ConversationToolEventCoordinator::Dependencies deps;
    deps.agentIdentityIdForSession = [](const QString&) { return QStringLiteral("agent-1"); };
    deps.reportPulseProgress = [&](const QString& agentId, const QString& summary) {
        f.pulseReports.append(agentId + QStringLiteral(":") + summary);
    };
    deps.updateTaskStateForSession = [&](const QString& sessionId,
                                         const QString& state,
                                         const TurnTask* turn,
                                         const QJsonObject& extra) {
        f.updates.append({ sessionId, state, turn ? turn->turnId : QString(), extra });
    };
    deps.taskStateTextPreview = [](const QString& text, int maxChars) {
        return text.left(maxChars);
    };
    deps.suppressHeartbeat = [&](const QString& agentId, const QString& reason) {
        f.suppressed.append(agentId + QStringLiteral(":") + reason);
    };
    deps.unsuppressHeartbeat = [&](const QString& agentId) {
        f.unsuppressed.append(agentId);
    };
    deps.emitPipelineEvent = [&](const QString& sessionId,
                                 const QString& type,
                                 const TurnTask*,
                                 const QString&,
                                 const QString& error,
                                 const QJsonObject& extra,
                                 bool) {
        f.events.append({ sessionId, type, error, extra });
    };
    deps.takeDelegateStartMs = [&](const QString& sessionId, const QString& toolId) {
        const QString key = sessionId + QStringLiteral("|") + toolId;
        if (!f.startMsByKey.contains(key))
            return qint64(-1);
        const qint64 value = f.startMsByKey.take(key);
        return value;
    };
    deps.putDelegateStartMs = [&](const QString& sessionId, const QString& toolId, qint64 startedAtMs) {
        f.startMsByKey.insert(sessionId + QStringLiteral("|") + toolId, startedAtMs);
    };
    deps.delegateStatsForSession = [&](const QString&) { return f.stats; };
    deps.setDelegateStatsForSession = [&](const QString&, const ConversationToolEventCoordinator::DelegateStats& stats) {
        f.stats = stats;
    };
    return deps;
}

ToolExecutionEvent makeDelegateEvent(const QString& status)
{
    ToolExecutionEvent event;
    event.toolName = QStringLiteral("delegate_task");
    event.toolId = QStringLiteral("tool-1");
    event.status = status;
    return event;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << " ConversationToolEventCoordinator 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("非 delegate 工具 - 仅上报 pulse，不发事件") {
        Fixture f;
        auto deps = makeDeps(f);
        ConversationToolEventCoordinator coordinator(deps);
        ToolExecutionEvent event;
        event.toolName = QStringLiteral("shell");
        event.toolId = QStringLiteral("tool-1");
        event.status = QStringLiteral("started");
        coordinator.handleToolEvent(QStringLiteral("session-1"), &f.activeTurn, event);
        if (f.pulseReports != QStringList { QStringLiteral("agent-1:tool_event") })
            return fail(QStringLiteral("pulse 上报"), f.pulseReports.join(QStringLiteral(",")));
        if (!f.events.isEmpty())
            return fail(QStringLiteral("0 个事件"), QString::number(f.events.size()));
        return 0;
    } END_TEST

    TEST("delegate started - 更新 task state、抑制 heartbeat、发 started 事件") {
        Fixture f;
        auto deps = makeDeps(f);
        ConversationToolEventCoordinator coordinator(deps);
        ToolExecutionEvent event = makeDelegateEvent(QStringLiteral("started"));
        event.data.insert(QStringLiteral("task"), QStringLiteral("do background work"));
        event.data.insert(QStringLiteral("role_prompt"), QStringLiteral("backend"));
        event.data.insert(QStringLiteral("_agent_id"), QStringLiteral("agent-1"));

        coordinator.handleToolEvent(QStringLiteral("session-1"), &f.activeTurn, event);

        if (f.updates.size() != 1)
            return fail(QStringLiteral("1 次状态更新"), QString::number(f.updates.size()));
        if (f.updates.first().state != QStringLiteral("running"))
            return fail(QStringLiteral("running"), f.updates.first().state);
        if (f.suppressed != QStringList { QStringLiteral("agent-1:delegate_running") })
            return fail(QStringLiteral("heartbeat 被抑制"), f.suppressed.join(QStringLiteral(",")));
        if (f.events.size() != 1 || f.events.first().type != QStringLiteral("delegate.tool_started"))
            return fail(QStringLiteral("delegate.tool_started"), f.events.isEmpty() ? QStringLiteral("<none>") : f.events.first().type);
        return 0;
    } END_TEST

    TEST("delegate completed accepted - blocked 状态 + completed 事件 + 统计") {
        Fixture f;
        f.startMsByKey.insert(QStringLiteral("session-1|tool-1"), 500);
        auto deps = makeDeps(f);
        ConversationToolEventCoordinator coordinator(deps);
        ToolExecutionEvent event = makeDelegateEvent(QStringLiteral("completed"));
        event.success = true;
        event.formattedResult = QStringLiteral("accepted");
        event.data.insert(QStringLiteral("status"), QStringLiteral("accepted"));
        event.data.insert(QStringLiteral("job_id"), QStringLiteral("job-1"));
        event.data.insert(QStringLiteral("child_request_id"), QStringLiteral("req-1"));

        coordinator.handleToolEvent(QStringLiteral("session-1"), &f.activeTurn, event);

        if (f.updates.size() != 1 || f.updates.first().state != QStringLiteral("blocked"))
            return fail(QStringLiteral("blocked"), f.updates.isEmpty() ? QStringLiteral("<none>") : f.updates.first().state);
        if (f.unsuppressed != QStringList { QStringLiteral("agent-1") })
            return fail(QStringLiteral("heartbeat 恢复"), f.unsuppressed.join(QStringLiteral(",")));
        if (f.stats.totalCount != 1 || f.stats.successCount != 1 || f.stats.failureCount != 0)
            return fail(QStringLiteral("stats=1/1/0"),
                        QStringLiteral("%1/%2/%3").arg(f.stats.totalCount).arg(f.stats.successCount).arg(f.stats.failureCount));
        if (f.events.size() != 1 || f.events.first().type != QStringLiteral("delegate.tool_completed"))
            return fail(QStringLiteral("delegate.tool_completed"), f.events.isEmpty() ? QStringLiteral("<none>") : f.events.first().type);
        return 0;
    } END_TEST

    TEST("delegate completed failed - failed 事件 + failure 统计") {
        Fixture f;
        auto deps = makeDeps(f);
        ConversationToolEventCoordinator coordinator(deps);
        ToolExecutionEvent event = makeDelegateEvent(QStringLiteral("completed"));
        event.success = false;
        event.rawResult = QStringLiteral("very bad");
        event.data.insert(QStringLiteral("status"), QStringLiteral("failed"));
        event.data.insert(QStringLiteral("failure_reason"), QStringLiteral("timeout"));

        coordinator.handleToolEvent(QStringLiteral("session-1"), &f.activeTurn, event);

        if (f.stats.totalCount != 1 || f.stats.successCount != 0 || f.stats.failureCount != 1)
            return fail(QStringLiteral("stats=1/0/1"),
                        QStringLiteral("%1/%2/%3").arg(f.stats.totalCount).arg(f.stats.successCount).arg(f.stats.failureCount));
        if (f.events.size() != 1 || f.events.first().type != QStringLiteral("delegate.tool_failed"))
            return fail(QStringLiteral("delegate.tool_failed"), f.events.isEmpty() ? QStringLiteral("<none>") : f.events.first().type);
        if (f.events.first().error != QStringLiteral("very bad"))
            return fail(QStringLiteral("very bad"), f.events.first().error);
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

