#include <QCoreApplication>
#include <QDebug>

#include "ConversationContextService.h"

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
    ConversationContext::TaskContextSnapshot loadedSnapshot;
    ConversationContext::TaskContextSnapshot savedSnapshot;
    ConversationContext::ContextCompressionCheckpoint savedCheckpoint;
    ConversationContext::ResumePacket savedResumePacket;
    QList<EventRecord> events;
    bool saveSnapshotResult = true;
    bool saveCheckpointResult = true;
    bool saveResumeResult = true;
};

ConversationContextService::Dependencies makeDeps(Fixture& fixture)
{
    ConversationContextService::Dependencies deps;
    deps.saveTaskContextSnapshot = [&](const QString&, const ConversationContext::TaskContextSnapshot& snapshot) {
        fixture.savedSnapshot = snapshot;
        return fixture.saveSnapshotResult;
    };
    deps.saveContextCompressionCheckpoint = [&](const QString&, const ConversationContext::ContextCompressionCheckpoint& checkpoint) {
        fixture.savedCheckpoint = checkpoint;
        return fixture.saveCheckpointResult;
    };
    deps.saveResumePacket = [&](const QString&, const ConversationContext::ResumePacket& packet) {
        fixture.savedResumePacket = packet;
        return fixture.saveResumeResult;
    };
    deps.loadTaskContextSnapshot = [&](const QString&, bool* ok) {
        if (ok)
            *ok = true;
        return fixture.loadedSnapshot;
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
    return deps;
}

TurnTask makeFinishedTurn(const QString& userContent, const QString& assistantContent)
{
    TurnTask turn;
    turn.turnId = QStringLiteral("turn-1");
    turn.userContent = userContent;
    turn.assistantContent = assistantContent;
    return turn;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << " ConversationContextService 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("完成态持久化 - 生成 snapshot、compact.skipped 和 resume packet") {
        Fixture fixture;
        fixture.loadedSnapshot.payload.insert(QStringLiteral("goal"), QStringLiteral("legacy goal"));
        ConversationContextService service(makeDeps(fixture));

        QJsonObject taskState;
        taskState.insert(QStringLiteral("summary"), QStringLiteral("实现上下文服务抽取"));
        const TurnTask turn = makeFinishedTurn(QStringLiteral("hello"), QStringLiteral("short answer"));
        service.persistCompletionArtifacts(QStringLiteral("session-1"),
                                           turn,
                                           taskState,
                                           QDateTime::fromString(QStringLiteral("2026-03-19T12:00:00.000Z"),
                                                                 Qt::ISODateWithMs));

        if (fixture.savedSnapshot.toJson().value(QStringLiteral("goal")).toString()
            != QStringLiteral("实现上下文服务抽取")) {
            return fail(QStringLiteral("实现上下文服务抽取"),
                        fixture.savedSnapshot.toJson().value(QStringLiteral("goal")).toString());
        }
        if (fixture.savedResumePacket.toJson().value(QStringLiteral("current_phase")).toString()
            != QStringLiteral("completed")) {
            return fail(QStringLiteral("completed"),
                        fixture.savedResumePacket.toJson().value(QStringLiteral("current_phase")).toString());
        }
        const QStringList expectedEvents {
            QStringLiteral("context.snapshot.updated"),
            QStringLiteral("context.compact.skipped"),
            QStringLiteral("context.resume_packet.updated")
        };
        QStringList actualEvents;
        for (const EventRecord& event : fixture.events)
            actualEvents.append(event.type);
        if (actualEvents != expectedEvents)
            return fail(expectedEvents.join(QStringLiteral(",")),
                        actualEvents.join(QStringLiteral(",")));
        return 0;
    } END_TEST

    TEST("长内容 - 生成 context.compacted") {
        Fixture fixture;
        ConversationContextService service(makeDeps(fixture));
        const TurnTask turn =
            makeFinishedTurn(QString(700, QLatin1Char('u')), QString(700, QLatin1Char('a')));
        service.persistCompletionArtifacts(QStringLiteral("session-1"),
                                           turn,
                                           QJsonObject(),
                                           QDateTime::fromString(QStringLiteral("2026-03-19T12:00:00.000Z"),
                                                                 Qt::ISODateWithMs));

        if (fixture.savedCheckpoint.toJson().value(QStringLiteral("reason")).toString()
            != QStringLiteral("content_length_exceeded")) {
            return fail(QStringLiteral("content_length_exceeded"),
                        fixture.savedCheckpoint.toJson().value(QStringLiteral("reason")).toString());
        }
        bool foundCompacted = false;
        for (const EventRecord& event : fixture.events) {
            if (event.type == QStringLiteral("context.compacted"))
                foundCompacted = true;
        }
        if (!foundCompacted)
            return fail(QStringLiteral("包含 context.compacted"), QStringLiteral("missing"));
        return 0;
    } END_TEST

    TEST("snapshot 保存失败 - 发 context.compact.error") {
        Fixture fixture;
        fixture.saveSnapshotResult = false;
        ConversationContextService service(makeDeps(fixture));
        const TurnTask turn = makeFinishedTurn(QStringLiteral("hello"), QStringLiteral("answer"));
        service.persistCompletionArtifacts(QStringLiteral("session-1"),
                                           turn,
                                           QJsonObject(),
                                           QDateTime::fromString(QStringLiteral("2026-03-19T12:00:00.000Z"),
                                                                 Qt::ISODateWithMs));

        if (fixture.events.isEmpty())
            return fail(QStringLiteral("至少一个事件"), QStringLiteral("0"));
        if (fixture.events.first().type != QStringLiteral("context.compact.error"))
            return fail(QStringLiteral("context.compact.error"), fixture.events.first().type);
        if (fixture.events.first().error != QStringLiteral("snapshot_save_failed"))
            return fail(QStringLiteral("snapshot_save_failed"), fixture.events.first().error);
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}
