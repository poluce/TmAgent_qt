#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>

#include "AgentPulseRegistry.h"
#include "ChatCoordinatorSupport.h"

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

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "   AgentPulseRegistry 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("ensure 同一 agent 只创建一个 pulse") {
        QHash<QString, AgentPulse*> pulses;
        AgentPulseRegistry registry(
            AgentPulseRegistry::Dependencies {
                &app,
                &pulses,
                [](AgentPulse::State state) { return ChatCoordinatorSupport::pulseStateToString(state); },
                {},
                {}
            });

        AgentPulse* first = registry.ensure(QStringLiteral("agent-1"));
        AgentPulse* second = registry.ensure(QStringLiteral("agent-1"));
        if (!first || !second)
            return fail(QStringLiteral("非空 pulse"), QStringLiteral("<null>"));
        if (first != second)
            return fail(QStringLiteral("同一个实例"), QStringLiteral("different"));
        if (pulses.size() != 1)
            return fail(QStringLiteral("1 个 pulse"), QString::number(pulses.size()));
        return 0;
    } END_TEST

    TEST("hard timeout 与恢复进度都能触发回调") {
        QHash<QString, AgentPulse*> pulses;
        QStringList stateChanges;
        QStringList hardTimeouts;
        AgentPulseRegistry registry(
            AgentPulseRegistry::Dependencies {
                &app,
                &pulses,
                [](AgentPulse::State state) { return ChatCoordinatorSupport::pulseStateToString(state); },
                [&](const QString& agentId, const QString& stateText) {
                    stateChanges.append(agentId + QStringLiteral(":") + stateText);
                },
                [&](const QString& agentId) { hardTimeouts.append(agentId); }
            });

        AgentPulse* pulse = registry.ensure(QStringLiteral("agent-2"));
        if (!pulse)
            return fail(QStringLiteral("非空 pulse"), QStringLiteral("<null>"));
        pulse->setThresholds(1000, 3000, 2000);

        QElapsedTimer timer;
        timer.start();
        while (hardTimeouts.isEmpty() && timer.elapsed() < 4200) {
            QThread::msleep(50);
            QCoreApplication::processEvents();
        }
        if (hardTimeouts != QStringList { QStringLiteral("agent-2") })
            return fail(QStringLiteral("agent-2 hard timeout"), hardTimeouts.join(QStringLiteral(",")));

        registry.reportProgress(QStringLiteral("agent-2"), QStringLiteral("resume"));
        QCoreApplication::processEvents();
        if (!stateChanges.contains(QStringLiteral("agent-2:hard_timeout")))
            return fail(QStringLiteral("包含 hard_timeout"), stateChanges.join(QStringLiteral(",")));
        if (!stateChanges.contains(QStringLiteral("agent-2:healthy")))
            return fail(QStringLiteral("包含 healthy"), stateChanges.join(QStringLiteral(",")));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果：%1/%2 通过").arg(g_passCount).arg(g_testCount);
    PRINT_DIVIDER();
    return g_passCount == g_testCount ? 0 : 1;
}

