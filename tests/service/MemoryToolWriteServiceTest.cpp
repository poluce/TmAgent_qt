#include <QCoreApplication>
#include <QDebug>

#include "MemoryToolWriteService.h"

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
    QString refreshedAgentId;
    QString refreshedReason;
    bool rememberOk = true;
};

MemoryToolWriteService makeService(Fixture& fixture)
{
    MemoryToolWriteService::Dependencies deps;
    deps.activeSessionForAgent = [](const QString&) { return QStringLiteral("session-1"); };
    deps.resolveSessionForAgent = [](const QString&) { return QStringLiteral("fallback-session"); };
    deps.activeTurnForSession = [](const QString&) -> TurnTask* { return nullptr; };
    deps.rememberToolRequested = [&](const QString&,
                                     const QString&,
                                     const QString&,
                                     const QString&,
                                     const QString&,
                                     const QString&,
                                     QString* summary,
                                     QString* writtenPath,
                                     QJsonObject* metadata,
                                     QString* error) {
        if (summary)
            *summary = QStringLiteral("记忆摘要");
        if (writtenPath)
            *writtenPath = QStringLiteral("memory.md");
        if (metadata) {
            metadata->insert(QStringLiteral("longMemoryAdded"), fixture.rememberOk ? 1 : 0);
            metadata->insert(QStringLiteral("longMemoryDuplicate"), 0);
        }
        if (error)
            *error = fixture.rememberOk ? QString() : QStringLiteral("remember failed");
        return fixture.rememberOk;
    };
    deps.emitPipelineEvent = [&](const QString& sessionId,
                                 const QString& type,
                                 const TurnTask*,
                                 const QString&,
                                 const QString& error,
                                 const QJsonObject& extra,
                                 bool) {
        fixture.events.append({ sessionId, type, error, extra });
    };
    deps.refreshMemoryIndexAndEmit = [&](const QString&,
                                         const QString& agentId,
                                         const TurnTask*,
                                         const QString& reason,
                                         const QString&,
                                         const QJsonObject&) {
        fixture.refreshedAgentId = agentId;
        fixture.refreshedReason = reason;
    };
    return MemoryToolWriteService(deps);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << " MemoryToolWriteService 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("缺少 _agent_id - 返回失败") {
        Fixture fixture;
        MemoryToolWriteService service = makeService(fixture);
        const ToolResult result = service.execute(QJsonObject());
        if (result.success)
            return fail(QStringLiteral("success=false"), QStringLiteral("true"));
        return 0;
    } END_TEST

    TEST("成功写入 - 发 memory.updated 并触发索引刷新") {
        Fixture fixture;
        MemoryToolWriteService service = makeService(fixture);
        QJsonObject args;
        args.insert(QStringLiteral("_agent_id"), QStringLiteral("agent-1"));
        args.insert(QStringLiteral("memory"), QStringLiteral("remember this"));

        const ToolResult result = service.execute(args);
        if (!result.success)
            return fail(QStringLiteral("success=true"), QStringLiteral("false"));
        if (fixture.events.isEmpty() || fixture.events.first().type != QStringLiteral("memory.updated"))
            return fail(QStringLiteral("memory.updated"),
                        fixture.events.isEmpty() ? QStringLiteral("<none>") : fixture.events.first().type);
        if (fixture.refreshedAgentId != QStringLiteral("agent-1"))
            return fail(QStringLiteral("agent-1"), fixture.refreshedAgentId);
        if (fixture.refreshedReason != QStringLiteral("tool_memory_write"))
            return fail(QStringLiteral("tool_memory_write"), fixture.refreshedReason);
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}
