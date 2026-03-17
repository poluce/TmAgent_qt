#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>

#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "ConversationDispatchCoordinator.h"

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
    TurnManager turnManager;
    Session* session = nullptr;
    Identity* agent = nullptr;
    QHash<QString, QString> activeSessionByAgent;
    QList<EventRecord> events;
    QList<UpdateRecord> updates;
    QStringList pulseReports;
    QJsonArray runtimeHistorySet;
    LLMConfig runtimeConfigSet;
    QJsonObject runtimeIoContextSet;
    QString sentSessionId;
    QString sentPrompt;

    Fixture()
    {
        agent = Identity::createAgent(QStringLiteral("Agent"), new IdentityProfile(), nullptr, QStringLiteral("agent-1"));
        session = Session::createPrivate(QStringLiteral("user-1"), agent->id());
        session->setTitle(QStringLiteral("S"));
    }
};

ConversationDispatchCoordinator::Dependencies makeDeps(Fixture& f,
                                                       const QJsonArray& runtimeHistory,
                                                       const QString& memoryContext,
                                                       const QString& delegateContext)
{
    ConversationDispatchCoordinator::Dependencies deps;
    deps.turnManager = &f.turnManager;
    deps.findPipeline = [&](const QString& sid) { return f.turnManager.findPipeline(sid); };
    deps.ensureRuntimeIdentityForSession = [&](const QString&, QString* agentIdOut) -> Identity* {
        if (agentIdOut)
            *agentIdOut = f.agent->id();
        return f.agent;
    };
    deps.findSession = [&](const QString&) { return f.session; };
    deps.activeSessionForAgent = [&](const QString& agentId) { return f.activeSessionByAgent.value(agentId); };
    deps.setActiveSessionForAgent = [&](const QString& agentId, const QString& sid) {
        f.activeSessionByAgent.insert(agentId, sid);
    };
    deps.buildRuntimeHistoryFromMessages = [runtimeHistory](Session*) { return runtimeHistory; };
    deps.estimateHistoryChars = [](const QJsonArray& history) {
        return static_cast<qint64>(QJsonDocument(history).toJson(QJsonDocument::Compact).size());
    };
    deps.setRuntimeHistory = [&](const QString&, const QJsonArray& history) { f.runtimeHistorySet = history; };
    deps.setRuntimeConfig = [&](const QString&, const LLMConfig& config) { f.runtimeConfigSet = config; };
    deps.setRuntimeIoContext = [&](const QString&, const QJsonObject& context) { f.runtimeIoContextSet = context; };
    deps.sendRuntimeMessage = [&](const QString& sid, const QString& prompt) {
        f.sentSessionId = sid;
        f.sentPrompt = prompt;
    };
    deps.emitPipelineEvent = [&](const QString& sid,
                                 const QString& type,
                                 const TurnTask*,
                                 const QString&,
                                 const QString& error,
                                 const QJsonObject& extra,
                                 bool) {
        f.events.append({ sid, type, error, extra });
    };
    deps.updateTaskStateForSession = [&](const QString& sid,
                                         const QString& state,
                                         const TurnTask* turn,
                                         const QJsonObject& extra) {
        f.updates.append({ sid, state, turn ? turn->turnId : QString(), extra });
    };
    deps.taskStateTextPreview = [](const QString& text, int maxChars) { return text.left(maxChars); };
    deps.reportPulseProgress = [&](const QString& agentId, const QString& summary) {
        f.pulseReports.append(agentId + QStringLiteral(":") + summary);
    };
    deps.ensureMemoryInitializedForAgent = [](Identity*) {};
    deps.composeConfigForIdentity = [](Identity*) {
        LLMConfig cfg;
        cfg.selectedModelId = QStringLiteral("model-1");
        cfg.systemPrompt = QStringLiteral("BASE");
        return cfg;
    };
    deps.composeMemoryContext = [memoryContext](const QString&, int) { return memoryContext; };
    deps.delegateContextForAgent = [delegateContext](const QString&) { return delegateContext; };
    return deps;
}

TurnTask makeQueuedTurn(const QString& text)
{
    TurnTask turn;
    turn.turnId = QStringLiteral("turn-1");
    turn.requestTraceId = QStringLiteral("trace-1");
    turn.runId = QStringLiteral("run-1");
    turn.actorIdentityId = QStringLiteral("user-1");
    turn.clientMessageId = QStringLiteral("client-1");
    turn.userContent = text;
    turn.enqueuedAtMs = 1;
    return turn;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << " ConversationDispatchCoordinator 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("无 pipeline - 返回 false") {
        Fixture f;
        auto deps = makeDeps(f, QJsonArray(), QString(), QString());
        ConversationDispatchCoordinator coordinator(deps, ConversationDispatchCoordinator::Limits {});
        if (coordinator.tryStartNextTurn(QStringLiteral("session-1")))
            return fail(QStringLiteral("false"), QStringLiteral("true"));
        return 0;
    } END_TEST

    TEST("agent 已被其他 session 占用 - 不启动") {
        Fixture f;
        f.turnManager.enqueueTurn(QStringLiteral("session-1"), makeQueuedTurn(QStringLiteral("hello")));
        f.activeSessionByAgent.insert(QStringLiteral("agent-1"), QStringLiteral("other-session"));
        auto deps = makeDeps(f, QJsonArray(), QString(), QString());
        ConversationDispatchCoordinator coordinator(deps, ConversationDispatchCoordinator::Limits {});
        if (coordinator.tryStartNextTurn(QStringLiteral("session-1")))
            return fail(QStringLiteral("false"), QStringLiteral("true"));
        if (!f.sentPrompt.isEmpty())
            return fail(QStringLiteral("<empty>"), f.sentPrompt);
        return 0;
    } END_TEST

    TEST("正常 dispatch - 写 history/config/ioContext 并发事件") {
        Fixture f;
        f.turnManager.ensurePipeline(QStringLiteral("session-1"));
        f.turnManager.enqueueTurn(QStringLiteral("session-1"), makeQueuedTurn(QStringLiteral("hello")));

        QJsonArray history;
        history.append(QJsonObject {
            { QStringLiteral("role"), QStringLiteral("system") },
            { QStringLiteral("content"), QStringLiteral("[Context Compact] summary") }
        });

        auto deps = makeDeps(f, history, QStringLiteral("MEMORY"), QStringLiteral("DELEGATE"));
        ConversationDispatchCoordinator::Limits limits;
        limits.memoryContextMaxChars = 4500;
        ConversationDispatchCoordinator coordinator(deps, limits);
        if (!coordinator.tryStartNextTurn(QStringLiteral("session-1")))
            return fail(QStringLiteral("true"), QStringLiteral("false"));

        if (f.activeSessionByAgent.value(QStringLiteral("agent-1")) != QStringLiteral("session-1"))
            return fail(QStringLiteral("session-1"), f.activeSessionByAgent.value(QStringLiteral("agent-1")));
        if (!f.session->streamState().isStreaming)
            return fail(QStringLiteral("streaming=true"), QStringLiteral("false"));
        if (f.runtimeHistorySet.size() != 1)
            return fail(QStringLiteral("history size 1"), QString::number(f.runtimeHistorySet.size()));
        if (!f.runtimeConfigSet.systemPrompt.contains(QStringLiteral("BASE"))
            || !f.runtimeConfigSet.systemPrompt.contains(QStringLiteral("MEMORY"))
            || !f.runtimeConfigSet.systemPrompt.contains(QStringLiteral("DELEGATE"))) {
            return fail(QStringLiteral("systemPrompt 包含 BASE/MEMORY/DELEGATE"), f.runtimeConfigSet.systemPrompt);
        }
        if (f.runtimeIoContextSet.value(QStringLiteral("session_id")).toString() != QStringLiteral("session-1"))
            return fail(QStringLiteral("session-1"), f.runtimeIoContextSet.value(QStringLiteral("session_id")).toString());
        if (f.sentSessionId != QStringLiteral("session-1") || f.sentPrompt != QStringLiteral("hello"))
            return fail(QStringLiteral("sendMessage(session-1, hello)"), f.sentSessionId + QStringLiteral("/") + f.sentPrompt);
        if (f.updates.size() != 1 || f.updates.first().state != QStringLiteral("running"))
            return fail(QStringLiteral("running"), f.updates.isEmpty() ? QStringLiteral("<none>") : f.updates.first().state);
        const QStringList expectedEvents {
            QStringLiteral("context.compacted"),
            QStringLiteral("turn_started"),
            QStringLiteral("memory.recalled"),
            QStringLiteral("turn_dispatch_prepare"),
            QStringLiteral("turn_dispatch_config_applied"),
            QStringLiteral("turn_dispatch_sent")
        };
        QStringList actualEvents;
        for (const EventRecord& event : f.events)
            actualEvents.append(event.type);
        if (actualEvents != expectedEvents)
            return fail(expectedEvents.join(QStringLiteral(",")), actualEvents.join(QStringLiteral(",")));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

