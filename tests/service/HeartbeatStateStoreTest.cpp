#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>

#include "HeartbeatStateStore.h"

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

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "  HeartbeatStateStore 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("从 app state 加载新的 heartbeat runtime 状态") {
        HeartbeatRuntimeState expected;
        expected.laneState = HeartbeatLaneState::Deferred;
        expected.lastDecision = HeartbeatDecision::Escalate;
        expected.lastSummaryDigest = QStringLiteral("digest-summary");
        expected.providerState = QStringLiteral("down");
        expected.pulseState = QStringLiteral("hard_timeout");
        expected.hasPendingTicket = true;
        expected.pendingTicket.kind = HeartbeatTicketKind::Recovery;
        expected.pendingTicket.reason = QStringLiteral("restart_missed_window");
        expected.pendingTicket.priority = HeartbeatTicketPriority::High;
        expected.pendingTicket.requestedAtUtc = QDateTime::fromString(
            QStringLiteral("2026-03-25T01:02:03.000Z"), Qt::ISODateWithMs);
        expected.lastCompletedAtUtc = QDateTime::fromString(
            QStringLiteral("2026-03-25T02:03:04.000Z"), Qt::ISODateWithMs);
        expected.nextDueAtUtc = QDateTime::fromString(
            QStringLiteral("2026-03-25T03:03:04.000Z"), Qt::ISODateWithMs);

        const QString payload = QString::fromUtf8(
            QJsonDocument(heartbeatRuntimeStateToJson(expected)).toJson(QJsonDocument::Compact));

        HeartbeatStateStore store(
            HeartbeatStateStore::Dependencies {
                [payload](const QString&) { return payload; },
                {},
                []() { return true; }
            });

        HeartbeatRuntimeState loaded;
        store.load(QStringLiteral("agent-1"), &loaded);
        if (!loaded.loaded)
            return fail(QStringLiteral("loaded=true"), QStringLiteral("false"));
        if (loaded.stateStorageKey != QStringLiteral("heartbeat_runtime:agent-1"))
            return fail(QStringLiteral("heartbeat_runtime:agent-1"), loaded.stateStorageKey);
        if (loaded.stateLocation != QStringLiteral("SQLite app_state :: heartbeat_runtime:agent-1"))
            return fail(QStringLiteral("SQLite app_state :: heartbeat_runtime:agent-1"),
                        loaded.stateLocation);
        if (loaded.laneState != HeartbeatLaneState::Deferred)
            return fail(QStringLiteral("deferred"), heartbeatLaneStateToString(loaded.laneState));
        if (!loaded.hasPendingTicket || loaded.pendingTicket.kind != HeartbeatTicketKind::Recovery)
            return fail(QStringLiteral("pending recovery ticket"), QStringLiteral("missing"));
        if (loaded.lastDecision != HeartbeatDecision::Escalate)
            return fail(QStringLiteral("escalate"),
                        heartbeatDecisionToString(loaded.lastDecision));
        if (loaded.providerState != QStringLiteral("down"))
            return fail(QStringLiteral("down"), loaded.providerState);
        return 0;
    } END_TEST

    TEST("保存新的 heartbeat runtime 状态到 heartbeat_runtime 键") {
        QString savedKey;
        QString savedValue;

        HeartbeatStateStore store(
            HeartbeatStateStore::Dependencies {
                {},
                [&](const QString& key, const QString& value) {
                    savedKey = key;
                    savedValue = value;
                    return true;
                },
                []() { return true; }
            });

        HeartbeatRuntimeState runtimeState;
        runtimeState.loaded = true;
        runtimeState.stateStorageKey = QStringLiteral("heartbeat_runtime:agent-2");
        runtimeState.stateLocation = QStringLiteral("SQLite app_state :: heartbeat_runtime:agent-2");
        runtimeState.laneState = HeartbeatLaneState::Running;
        runtimeState.lastDecision = HeartbeatDecision::MaintainOnly;
        runtimeState.lastSummaryDigest = QStringLiteral("digest-2");
        runtimeState.lastCompletedAtUtc = QDateTime::currentDateTimeUtc();

        if (!store.save(QStringLiteral("agent-2"), &runtimeState))
            return fail(QStringLiteral("true"), QStringLiteral("false"));
        if (savedKey != QStringLiteral("heartbeat_runtime:agent-2"))
            return fail(QStringLiteral("heartbeat_runtime:agent-2"), savedKey);
        if (!savedValue.contains(QStringLiteral("\"lane_state\":\"running\"")))
            return fail(QStringLiteral("lane_state=running"), savedValue);
        if (!savedValue.contains(QStringLiteral("\"last_decision\":\"maintain_only\"")))
            return fail(QStringLiteral("last_decision=maintain_only"), savedValue);
        return 0;
    } END_TEST

    TEST("DB 不可用时不保存状态") {
        HeartbeatStateStore store(
            HeartbeatStateStore::Dependencies {
                {},
                [](const QString&, const QString&) { return true; },
                []() { return false; }
            });

        HeartbeatRuntimeState runtimeState;
        runtimeState.stateStorageKey = QStringLiteral("heartbeat_runtime:agent-3");
        if (store.save(QStringLiteral("agent-3"), &runtimeState))
            return fail(QStringLiteral("false"), QStringLiteral("true"));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果：%1/%2 通过").arg(g_passCount).arg(g_testCount);
    PRINT_DIVIDER();
    return g_passCount == g_testCount ? 0 : 1;
}
