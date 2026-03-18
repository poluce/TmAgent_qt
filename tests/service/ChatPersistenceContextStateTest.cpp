#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>

#include "core/persistence/ChatPersistenceService.h"
#include "core/service/include/ConversationContextTypes.h"

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

struct Fixture {
    QString rootPath;
    QString sessionId;

    Fixture()
    {
        rootPath = QDir::temp().filePath(
            QStringLiteral("tmagent-context-state-test-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        sessionId = QStringLiteral("context-session-%1")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        qputenv("TMAGENT_HOME", QDir::toNativeSeparators(rootPath).toUtf8());
        QDir().mkpath(rootPath);
    }

    ~Fixture()
    {
        QDir(rootPath).removeRecursively();
        qunsetenv("TMAGENT_HOME");
    }
};

QJsonObject makeSnapshotPayload(const QString& sessionId)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), ConversationContext::kSchemaVersion);
    obj.insert(QStringLiteral("session_id"), sessionId);
    obj.insert(QStringLiteral("kind"), QStringLiteral("task_snapshot"));
    obj.insert(QStringLiteral("updated_at_utc"), QStringLiteral("2026-03-18T10:00:00.000Z"));
    obj.insert(QStringLiteral("task_title"), QStringLiteral("实现上下文压缩"));
    obj.insert(QStringLiteral("current_phase"), QStringLiteral("implementation"));
    obj.insert(QStringLiteral("goal"), QStringLiteral("让长任务恢复更稳定"));
    obj.insert(QStringLiteral("done_items"), QJsonArray{ QStringLiteral("已接入 schema") });
    obj.insert(QStringLiteral("pending_items"), QJsonArray{ QStringLiteral("补齐 ChatService") });
    return obj;
}

QJsonObject makeCheckpointPayload(const QString& sessionId)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), ConversationContext::kSchemaVersion);
    obj.insert(QStringLiteral("session_id"), sessionId);
    obj.insert(QStringLiteral("kind"), QStringLiteral("context_checkpoint"));
    obj.insert(QStringLiteral("updated_at_utc"), QStringLiteral("2026-03-18T10:05:00.000Z"));
    obj.insert(QStringLiteral("checkpoint_id"), QStringLiteral("ckpt-1"));
    obj.insert(QStringLiteral("reason"), QStringLiteral("history_budget_exceeded"));
    obj.insert(QStringLiteral("dropped_message_count"), 12);
    return obj;
}

QJsonObject makeResumePacketPayload(const QString& sessionId)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), ConversationContext::kSchemaVersion);
    obj.insert(QStringLiteral("session_id"), sessionId);
    obj.insert(QStringLiteral("kind"), QStringLiteral("resume_packet"));
    obj.insert(QStringLiteral("updated_at_utc"), QStringLiteral("2026-03-18T10:06:00.000Z"));
    obj.insert(QStringLiteral("goal"), QStringLiteral("继续完成 v1 最小落地"));
    obj.insert(QStringLiteral("current_phase"), QStringLiteral("verification"));
    obj.insert(QStringLiteral("pending_items"), QJsonArray{ QStringLiteral("跑测试") });
    return obj;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << " ChatPersistence Context State 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    Fixture fixture;
    ChatPersistenceService persistence;

    TEST("loadTaskContextSnapshot - 缺文件返回空对象且 ok=true") {
        bool ok = false;
        const auto snapshot = persistence.loadTaskContextSnapshot(fixture.sessionId, &ok);
        if (!ok)
            return fail(QStringLiteral("ok=true"), QStringLiteral("false"));
        if (!snapshot.toJson().isEmpty())
            return fail(QStringLiteral("空对象"), QStringLiteral("non-empty"));
        return 0;
    } END_TEST

    TEST("save/loadTaskContextSnapshot - 可 round-trip 关键字段") {
        ConversationContext::TaskContextSnapshot snapshot;
        snapshot.payload = makeSnapshotPayload(fixture.sessionId);
        if (!persistence.saveTaskContextSnapshot(fixture.sessionId, snapshot))
            return fail(QStringLiteral("save 成功"), QStringLiteral("false"));
        bool ok = false;
        const auto loaded = persistence.loadTaskContextSnapshot(fixture.sessionId, &ok);
        if (!ok)
            return fail(QStringLiteral("ok=true"), QStringLiteral("false"));
        if (loaded.toJson().value(QStringLiteral("task_title")).toString() != QStringLiteral("实现上下文压缩"))
            return fail(QStringLiteral("实现上下文压缩"), loaded.toJson().value(QStringLiteral("task_title")).toString());
        if (loaded.toJson().value(QStringLiteral("session_id")).toString() != fixture.sessionId)
            return fail(fixture.sessionId, loaded.toJson().value(QStringLiteral("session_id")).toString());
        return 0;
    } END_TEST

    TEST("save/loadContextCompressionCheckpoint - 可 round-trip 关键字段") {
        ConversationContext::ContextCompressionCheckpoint checkpoint;
        checkpoint.payload = makeCheckpointPayload(fixture.sessionId);
        if (!persistence.saveContextCompressionCheckpoint(fixture.sessionId, checkpoint))
            return fail(QStringLiteral("save 成功"), QStringLiteral("false"));
        bool ok = false;
        const auto loaded = persistence.loadContextCompressionCheckpoint(fixture.sessionId, &ok);
        if (!ok)
            return fail(QStringLiteral("ok=true"), QStringLiteral("false"));
        if (loaded.toJson().value(QStringLiteral("checkpoint_id")).toString() != QStringLiteral("ckpt-1"))
            return fail(QStringLiteral("ckpt-1"), loaded.toJson().value(QStringLiteral("checkpoint_id")).toString());
        if (loaded.toJson().value(QStringLiteral("dropped_message_count")).toInt() != 12)
            return fail(QStringLiteral("12"), QString::number(loaded.toJson().value(QStringLiteral("dropped_message_count")).toInt()));
        return 0;
    } END_TEST

    TEST("save/loadResumePacket - 可 round-trip 关键字段") {
        ConversationContext::ResumePacket packet;
        packet.payload = makeResumePacketPayload(fixture.sessionId);
        if (!persistence.saveResumePacket(fixture.sessionId, packet))
            return fail(QStringLiteral("save 成功"), QStringLiteral("false"));
        bool ok = false;
        const auto loaded = persistence.loadResumePacket(fixture.sessionId, &ok);
        if (!ok)
            return fail(QStringLiteral("ok=true"), QStringLiteral("false"));
        if (loaded.toJson().value(QStringLiteral("current_phase")).toString() != QStringLiteral("verification"))
            return fail(QStringLiteral("verification"), loaded.toJson().value(QStringLiteral("current_phase")).toString());
        return 0;
    } END_TEST

    TEST("loadTaskContextSnapshot - 坏 JSON 返回空对象且 ok=false") {
        const QString path = persistence.contextSnapshotPath(fixture.sessionId);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QFile::WriteOnly | QFile::Text))
            return fail(QStringLiteral("写入坏 JSON 成功"), QStringLiteral("open failed"));
        file.write("{bad json");
        file.close();

        bool ok = true;
        const auto loaded = persistence.loadTaskContextSnapshot(fixture.sessionId, &ok);
        if (ok)
            return fail(QStringLiteral("ok=false"), QStringLiteral("true"));
        if (!loaded.toJson().isEmpty())
            return fail(QStringLiteral("空对象"), QStringLiteral("non-empty"));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}
