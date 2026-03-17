#include <QCoreApplication>
#include <QDebug>

#include "DelegateSettlementCoordinator.h"

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
    QJsonObject extra;
};

struct EventRecord {
    QString sessionId;
    QString type;
    QJsonObject extra;
};

struct Fixture {
    QString resolvedSessionId = QStringLiteral("session-1");
    QList<Message> postedMessages;
    QList<UpdateRecord> updates;
    QList<QPair<QString, QString>> heartbeatTriggers;
    QList<EventRecord> events;

    DelegateSettlementCoordinator::Dependencies makeDeps()
    {
        DelegateSettlementCoordinator::Dependencies deps;
        deps.resolvePrimarySessionForAgent = [this](const QString&, bool, bool, const QString&) {
            return resolvedSessionId;
        };
        deps.postMessage = [this](const QString&, const Message& message) {
            postedMessages.append(message);
        };
        deps.updateTaskStateForSession = [this](const QString& sid, const QString& state, const void*, const QJsonObject& extra) {
            updates.append({ sid, state, extra });
        };
        deps.taskStateTextPreview = [](const QString& text, int maxChars) {
            return text.left(maxChars);
        };
        deps.triggerHeartbeat = [this](const QString& agentId, const QString& reason) {
            heartbeatTriggers.append({ agentId, reason });
        };
        deps.emitPipelineEventSimple = [this](const QString& sid,
                                              const QString& type,
                                              const QString&,
                                              const QString&,
                                              const QJsonObject& extra,
                                              bool) {
            events.append({ sid, type, extra });
        };
        return deps;
    }
};

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "  DelegateSettlementCoordinator 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("owner 为空 - 不做任何动作") {
        Fixture f;
        DelegateSettlementCoordinator coordinator(f.makeDeps());
        coordinator.onDelegateJobSettled(QStringLiteral("job-1"), QString(), true, QStringLiteral("done"));
        if (!f.postedMessages.isEmpty())
            return fail(QStringLiteral("0 条消息"), QString::number(f.postedMessages.size()));
        if (!f.updates.isEmpty())
            return fail(QStringLiteral("0 次状态更新"), QString::number(f.updates.size()));
        if (!f.heartbeatTriggers.isEmpty())
            return fail(QStringLiteral("0 次 heartbeat"), QString::number(f.heartbeatTriggers.size()));
        if (!f.events.isEmpty())
            return fail(QStringLiteral("0 个事件"), QString::number(f.events.size()));
        return 0;
    } END_TEST

    TEST("成功完成 - 注入通知、done 状态、heartbeat 与事件") {
        Fixture f;
        DelegateSettlementCoordinator coordinator(f.makeDeps());
        coordinator.onDelegateJobSettled(
            QStringLiteral("job-42"),
            QStringLiteral("agent-1"),
            true,
            QStringLiteral("child finished successfully"));

        if (f.postedMessages.size() != 1)
            return fail(QStringLiteral("1 条系统消息"), QString::number(f.postedMessages.size()));
        if (!f.postedMessages.first().content.text.contains(QStringLiteral("job_id: job-42")))
            return fail(QStringLiteral("通知包含 job_id"), f.postedMessages.first().content.text);
        if (f.updates.size() != 1)
            return fail(QStringLiteral("1 次状态更新"), QString::number(f.updates.size()));
        if (f.updates.first().state != QStringLiteral("done"))
            return fail(QStringLiteral("done"), f.updates.first().state);
        if (f.heartbeatTriggers != QList<QPair<QString, QString>> { { QStringLiteral("agent-1"), QStringLiteral("delegate_job_settled") } })
            return fail(QStringLiteral("触发 agent-1 heartbeat"), QStringLiteral("数量=%1").arg(f.heartbeatTriggers.size()));
        if (f.events.size() != 1 || f.events.first().type != QStringLiteral("delegate.job_settled"))
            return fail(QStringLiteral("delegate.job_settled"), f.events.isEmpty() ? QStringLiteral("<none>") : f.events.first().type);
        return 0;
    } END_TEST

    TEST("失败完成 - failed 状态写入 last_error") {
        Fixture f;
        DelegateSettlementCoordinator coordinator(f.makeDeps());
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
            return fail(QStringLiteral("事件 success=false"), f.events.isEmpty() ? QStringLiteral("<none>") : QString::number(f.events.first().extra.value(QStringLiteral("success")).toBool()));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果：%1/%2 通过").arg(g_passCount).arg(g_testCount);
    PRINT_DIVIDER();
    return g_passCount == g_testCount ? 0 : 1;
}

