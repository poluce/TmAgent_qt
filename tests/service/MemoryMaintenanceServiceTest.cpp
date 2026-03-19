#include <QCoreApplication>
#include <QDebug>

#include "MemoryMaintenanceService.h"

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
    int retainedTurns = 0;
    bool reflectionEnabled = true;
    int reflectionIntervalTurns = 2;
    bool rebuildOk = true;
    bool reflectOk = true;
    QJsonObject rebuildMetadata;
    QJsonObject reflectMetadata;
    QString reflectWrittenPath = QStringLiteral("memory.md");
    QString reflectSummary = QStringLiteral("summary");
};

MemoryMaintenanceService makeService(Fixture& fixture)
{
    MemoryMaintenanceService::Dependencies deps;
    deps.rebuildSearchIndex = [&](const QString&, QJsonObject* metadata, QString* error) {
        if (metadata)
            *metadata = fixture.rebuildMetadata;
        if (error)
            *error = fixture.rebuildOk ? QString() : QStringLiteral("rebuild failed");
        return fixture.rebuildOk;
    };
    deps.reflectionEnabled = [&]() { return fixture.reflectionEnabled; };
    deps.reflectionIntervalTurns = [&]() { return fixture.reflectionIntervalTurns; };
    deps.reflectAndScore = [&](const QString&,
                               const QString&,
                               const QString&,
                               const QString&,
                               QString* summary,
                               QString* writtenPath,
                               QJsonObject* metadata,
                               QString* error) {
        if (summary)
            *summary = fixture.reflectSummary;
        if (writtenPath)
            *writtenPath = fixture.reflectWrittenPath;
        if (metadata)
            *metadata = fixture.reflectMetadata;
        if (error)
            *error = fixture.reflectOk ? QString() : QStringLiteral("reflect failed");
        return fixture.reflectOk;
    };
    deps.retainedTurnsForAgent = [&](const QString&) { return fixture.retainedTurns; };
    deps.setRetainedTurnsForAgent = [&](const QString&, int retainedTurns) {
        fixture.retainedTurns = retainedTurns;
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
    return MemoryMaintenanceService(deps);
}

TurnTask makeTurn()
{
    TurnTask turn;
    turn.turnId = QStringLiteral("turn-1");
    turn.requestTraceId = QStringLiteral("trace-1");
    turn.userContent = QStringLiteral("user");
    turn.assistantContent = QStringLiteral("assistant");
    return turn;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << " MemoryMaintenanceService 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("refreshIndexAndEmit - 成功发 memory.index.updated") {
        Fixture fixture;
        fixture.rebuildMetadata.insert(QStringLiteral("rows_indexed"), 12);
        MemoryMaintenanceService service = makeService(fixture);
        service.refreshIndexAndEmit(QStringLiteral("session-1"),
                                    QStringLiteral("agent-1"),
                                    nullptr,
                                    QStringLiteral("manual_remember"),
                                    QStringLiteral("memory.md"),
                                    QJsonObject());

        if (fixture.events.size() != 1)
            return fail(QStringLiteral("1 个事件"), QString::number(fixture.events.size()));
        if (fixture.events.first().type != QStringLiteral("memory.index.updated"))
            return fail(QStringLiteral("memory.index.updated"), fixture.events.first().type);
        if (fixture.events.first().extra.value(QStringLiteral("rows_indexed")).toInt() != 12)
            return fail(QStringLiteral("12"),
                        QString::number(fixture.events.first().extra.value(QStringLiteral("rows_indexed")).toInt()));
        return 0;
    } END_TEST

    TEST("maybeReflectAndEmit - 到达间隔后发 reflected/quality/index.updated") {
        Fixture fixture;
        fixture.retainedTurns = 1;
        fixture.reflectMetadata.insert(QStringLiteral("quality_score"), 88);
        fixture.reflectMetadata.insert(QStringLiteral("quality_level"), QStringLiteral("good"));
        fixture.reflectMetadata.insert(QStringLiteral("longMemoryAdded"), 1);
        fixture.reflectMetadata.insert(QStringLiteral("longMemoryPath"), QStringLiteral("memory.md"));
        MemoryMaintenanceService service = makeService(fixture);
        service.maybeReflectAndEmit(QStringLiteral("session-1"),
                                    QStringLiteral("agent-1"),
                                    makeTurn(),
                                    false,
                                    QString());

        const QStringList expectedEvents {
            QStringLiteral("memory.reflected"),
            QStringLiteral("memory.quality"),
            QStringLiteral("memory.index.updated")
        };
        QStringList actualEvents;
        for (const EventRecord& event : fixture.events)
            actualEvents.append(event.type);
        if (actualEvents != expectedEvents)
            return fail(expectedEvents.join(QStringLiteral(",")),
                        actualEvents.join(QStringLiteral(",")));
        if (fixture.retainedTurns != 2)
            return fail(QStringLiteral("2"), QString::number(fixture.retainedTurns));
        return 0;
    } END_TEST

    TEST("maybeReflectAndEmit - 未到间隔时不发事件") {
        Fixture fixture;
        fixture.retainedTurns = 0;
        MemoryMaintenanceService service = makeService(fixture);
        service.maybeReflectAndEmit(QStringLiteral("session-1"),
                                    QStringLiteral("agent-1"),
                                    makeTurn(),
                                    false,
                                    QString());

        if (!fixture.events.isEmpty())
            return fail(QStringLiteral("0 个事件"), QString::number(fixture.events.size()));
        if (fixture.retainedTurns != 1)
            return fail(QStringLiteral("1"), QString::number(fixture.retainedTurns));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}
