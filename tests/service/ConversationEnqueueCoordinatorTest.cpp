#include <QCoreApplication>
#include <QDebug>

#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "ConversationEnqueueCoordinator.h"

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
    QString turnId;
    QJsonObject extra;
};

struct Fixture {
    Identity* user = nullptr;
    Identity* agent = nullptr;
    Session* session = nullptr;
    TurnManager turnManager;
    QList<EventRecord> events;
    QList<UpdateRecord> updates;
    QStringList tryStartCalls;

    Fixture()
    {
        user = IdentityManager::instance()->userIdentity();
        agent = IdentityManager::instance()->createAgent(QStringLiteral("Agent"), new IdentityProfile(), QStringLiteral("agent-1"));
        session = SessionManager::instance()->createPrivateSession(user->id(), agent->id());
        session->setTitle(QStringLiteral("S"));
    }
};

ConversationEnqueueCoordinator::Dependencies makeDeps(Fixture& f, bool allowSend = true)
{
    ConversationEnqueueCoordinator::Dependencies deps;
    deps.identityManager = nullptr;
    deps.sessionManager = SessionManager::instance();
    deps.turnManager = &f.turnManager;
    deps.canIdentitySendMessage = [allowSend](const QString&, const QString&) { return allowSend; };
    deps.emitPipelineEvent = [&](const QString& sessionId,
                                 const QString& type,
                                 const TurnTask*,
                                 const QString&,
                                 const QString& error,
                                 const QJsonObject& extra,
                                 bool) {
        f.events.append({ sessionId, type, error, extra });
    };
    deps.updateTaskStateForSession = [&](const QString& sessionId,
                                         const QString& state,
                                         const TurnTask* turn,
                                         const QJsonObject& extra) {
        f.updates.append({ sessionId, state, turn ? turn->turnId : QString(), extra });
    };
    deps.tryStartNextTurn = [&](const QString& sessionId) { f.tryStartCalls.append(sessionId); };
    deps.isBackgroundClientMessage = [](const QString&) { return false; };
    deps.taskStateTextPreview = [](const QString& text, int maxChars) {
        return text.left(maxChars);
    };
    return deps;
}

ConversationEnqueueCoordinator::Limits defaultLimits()
{
    ConversationEnqueueCoordinator::Limits limits;
    limits.softQueueDepth = 2;
    limits.hardQueueDepth = 3;
    limits.queueMergeWindowMs = 100000;
    limits.queueMergeMaxMergedMessages = 4;
    limits.queueMergeMaxChars = 12000;
    return limits;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "  ConversationEnqueueCoordinator 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("无权限 - 返回空且不入队") {
        Fixture f;
        auto deps = makeDeps(f, false);
        deps.identityManager = IdentityManager::instance();
        ConversationEnqueueCoordinator coordinator(deps, defaultLimits());
        const QString turnId = coordinator.enqueueUserMessageAs(f.user->id(), f.session->id(), QStringLiteral("hello"));
        if (!turnId.isEmpty())
            return fail(QStringLiteral("<empty>"), turnId);
        if (!f.events.isEmpty())
            return fail(QStringLiteral("0 个事件"), QString::number(f.events.size()));
        return 0;
    } END_TEST

    TEST("空消息 - 返回空") {
        Fixture f;
        auto deps = makeDeps(f, true);
        deps.identityManager = IdentityManager::instance();
        ConversationEnqueueCoordinator coordinator(deps, defaultLimits());
        const QString turnId = coordinator.enqueueUserMessageAs(f.user->id(), f.session->id(), QStringLiteral("   "));
        if (!turnId.isEmpty())
            return fail(QStringLiteral("<empty>"), turnId);
        return 0;
    } END_TEST

    TEST("正常入队 - 产生 routed/queued 事件并触发 tryStart") {
        Fixture f;
        auto deps = makeDeps(f, true);
        deps.identityManager = IdentityManager::instance();
        ConversationEnqueueCoordinator coordinator(deps, defaultLimits());
        const QString turnId = coordinator.enqueueUserMessageAs(f.user->id(), f.session->id(), QStringLiteral("hello"));
        if (turnId.trimmed().isEmpty())
            return fail(QStringLiteral("非空 turnId"), QStringLiteral("<empty>"));
        if (f.events.size() != 2)
            return fail(QStringLiteral("2 个事件"), QString::number(f.events.size()));
        if (f.events.at(0).type != QStringLiteral("message_routed"))
            return fail(QStringLiteral("message_routed"), f.events.at(0).type);
        if (f.events.at(1).type != QStringLiteral("turn_queued"))
            return fail(QStringLiteral("turn_queued"), f.events.at(1).type);
        if (f.tryStartCalls != QStringList { f.session->id() })
            return fail(QStringLiteral("tryStartNextTurn 被调用"), f.tryStartCalls.join(QStringLiteral(",")));
        return 0;
    } END_TEST

    TEST("合并消息 - 命中 merge 路径") {
        Fixture f;
        TurnTask queued;
        queued.turnId = QStringLiteral("turn-1");
        queued.requestTraceId = QStringLiteral("trace-1");
        queued.actorIdentityId = f.user->id();
        queued.enqueuedAtMs = QDateTime::currentMSecsSinceEpoch();
        queued.userContent = QStringLiteral("first");
        f.turnManager.enqueueTurn(f.session->id(), queued);

        auto deps = makeDeps(f, true);
        deps.identityManager = IdentityManager::instance();
        ConversationEnqueueCoordinator coordinator(deps, defaultLimits());
        const QString turnId = coordinator.enqueueUserMessageAs(f.user->id(), f.session->id(), QStringLiteral("second"));
        if (turnId != QStringLiteral("turn-1"))
            return fail(QStringLiteral("turn-1"), turnId);
        if (f.events.size() != 2 || f.events.at(1).type != QStringLiteral("turn_merged"))
            return fail(QStringLiteral("turn_merged"), f.events.isEmpty() ? QStringLiteral("<none>") : f.events.last().type);
        return 0;
    } END_TEST

    TEST("soft backpressure - 先发 queue_backpressure 再 routed") {
        Fixture f;
        for (int i = 0; i < 2; ++i) {
            TurnTask queued;
            queued.turnId = QStringLiteral("turn-%1").arg(i + 1);
            queued.requestTraceId = QStringLiteral("trace-%1").arg(i + 1);
            queued.actorIdentityId = QStringLiteral("other-actor-%1").arg(i + 1);
            queued.enqueuedAtMs = 0;
            queued.userContent = QStringLiteral("queued-%1").arg(i + 1);
            f.turnManager.enqueueTurn(f.session->id(), queued);
        }

        auto deps = makeDeps(f, true);
        deps.identityManager = IdentityManager::instance();
        ConversationEnqueueCoordinator coordinator(deps, defaultLimits());
        coordinator.enqueueUserMessageAs(f.user->id(), f.session->id(), QStringLiteral("third"));
        if (f.events.size() < 2)
            return fail(QStringLiteral("至少 2 个事件"), QString::number(f.events.size()));
        if (f.events.at(0).type != QStringLiteral("queue_backpressure"))
            return fail(QStringLiteral("queue_backpressure"), f.events.at(0).type);
        if (f.events.at(1).type != QStringLiteral("message_routed"))
            return fail(QStringLiteral("message_routed"), f.events.at(1).type);
        return 0;
    } END_TEST

    TEST("hard overflow - 非 merge 场景下直接 rejected") {
        Fixture f;
        for (int i = 0; i < 3; ++i) {
            TurnTask queued;
            queued.turnId = QStringLiteral("turn-%1").arg(i + 1);
            queued.requestTraceId = QStringLiteral("trace-%1").arg(i + 1);
            queued.actorIdentityId = QStringLiteral("other-actor-%1").arg(i + 1);
            queued.enqueuedAtMs = 0;
            queued.userContent = QStringLiteral("queued-%1").arg(i + 1);
            f.turnManager.enqueueTurn(f.session->id(), queued);
        }

        auto deps = makeDeps(f, true);
        deps.identityManager = IdentityManager::instance();
        ConversationEnqueueCoordinator coordinator(deps, defaultLimits());
        const QString turnId = coordinator.enqueueUserMessageAs(f.user->id(), f.session->id(), QStringLiteral("overflow"));
        if (!turnId.isEmpty())
            return fail(QStringLiteral("<empty>"), turnId);
        if (f.events.size() != 1 || f.events.first().type != QStringLiteral("turn_rejected"))
            return fail(QStringLiteral("turn_rejected"), f.events.isEmpty() ? QStringLiteral("<none>") : f.events.first().type);
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

