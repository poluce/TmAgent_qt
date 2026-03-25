#include <QCoreApplication>
#include <QDebug>

#include "HeartbeatDecisionEngine.h"

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

HeartbeatPolicy defaultPolicy()
{
    HeartbeatPolicy policy;
    policy.deliveryPolicy.deliverActionableSummary = true;
    policy.llmEscalation.enabled = true;
    return policy;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "  HeartbeatDecisionEngine 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    HeartbeatDecisionEngine engine;

    TEST("手动心跳总是升级并允许投递摘要") {
        HeartbeatTicket ticket;
        ticket.kind = HeartbeatTicketKind::Manual;
        ticket.reason = QStringLiteral("manual");

        HeartbeatSnapshot snapshot;
        snapshot.providerState = QStringLiteral("healthy");

        const HeartbeatCycleResult result =
            engine.evaluate(defaultPolicy(), ticket, snapshot, HeartbeatRuntimeState {});
        if (result.decision != HeartbeatDecision::Escalate)
            return fail(QStringLiteral("escalate"), heartbeatDecisionToString(result.decision));
        if (!result.deliverToUser)
            return fail(QStringLiteral("deliver=true"), QStringLiteral("false"));
        return 0;
    } END_TEST

    TEST("Provider 状态变化会被判定为关键变化") {
        HeartbeatRuntimeState runtimeState;
        runtimeState.lastSnapshot.capturedAtUtc = QDateTime::currentDateTimeUtc().addSecs(-60);
        runtimeState.lastSnapshot.providerState = QStringLiteral("healthy");
        runtimeState.lastSnapshotDigest = QStringLiteral("prev");

        HeartbeatTicket ticket;
        ticket.kind = HeartbeatTicketKind::Auto;
        ticket.reason = QStringLiteral("cadence");

        HeartbeatSnapshot snapshot;
        snapshot.providerState = QStringLiteral("down");

        const HeartbeatCycleResult result =
            engine.evaluate(defaultPolicy(), ticket, snapshot, runtimeState);
        if (result.decision != HeartbeatDecision::Escalate)
            return fail(QStringLiteral("escalate"), heartbeatDecisionToString(result.decision));
        if (!result.actionableSignals.contains(QStringLiteral("provider_status")))
            return fail(QStringLiteral("包含 provider_status"),
                        result.actionableSignals.join(QStringLiteral(",")));
        return 0;
    } END_TEST

    TEST("无关键变化的自动心跳只做维护，不投递摘要") {
        HeartbeatRuntimeState runtimeState;
        runtimeState.lastSnapshot.capturedAtUtc = QDateTime::currentDateTimeUtc().addSecs(-60);
        runtimeState.lastSnapshot.providerState = QStringLiteral("healthy");
        runtimeState.lastSnapshot.pulseState = QStringLiteral("healthy");
        runtimeState.lastSnapshot.activeDelegateJobCount = 0;
        runtimeState.lastSnapshotDigest = QStringLiteral("prev");

        HeartbeatTicket ticket;
        ticket.kind = HeartbeatTicketKind::Auto;
        ticket.reason = QStringLiteral("cadence");

        HeartbeatSnapshot snapshot = runtimeState.lastSnapshot;
        snapshot.capturedAtUtc = QDateTime::currentDateTimeUtc();

        const HeartbeatCycleResult result =
            engine.evaluate(defaultPolicy(), ticket, snapshot, runtimeState);
        if (result.decision != HeartbeatDecision::MaintainOnly)
            return fail(QStringLiteral("maintain_only"),
                        heartbeatDecisionToString(result.decision));
        if (result.deliverToUser)
            return fail(QStringLiteral("deliver=false"), QStringLiteral("true"));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果：%1/%2 通过").arg(g_passCount).arg(g_testCount);
    PRINT_DIVIDER();
    return g_passCount == g_testCount ? 0 : 1;
}
