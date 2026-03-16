#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QUuid>

#include <atomic>
#include <thread>
#include <vector>

#include "core/model/Message.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"

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

struct MessagePersistenceFixture {
    QString homeRoot;
    QString sessionId;

    MessagePersistenceFixture()
    {
        homeRoot = QDir::temp().filePath(
            QStringLiteral("tmagent-message-persist-test-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        sessionId = QStringLiteral("message-session-%1")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        qputenv("HOME", QDir::toNativeSeparators(homeRoot).toUtf8());
        qputenv("USERPROFILE", QDir::toNativeSeparators(homeRoot).toUtf8());
        QDir().mkpath(homeRoot);
    }

    ~MessagePersistenceFixture()
    {
        QDir(homeRoot).removeRecursively();
        qunsetenv("HOME");
        qunsetenv("USERPROFILE");
    }
};

} // namespace

int main(int argc, char* argv[])
{
    MessagePersistenceFixture fixture;
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "  Message Persistence Concurrency Test";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("insert/load - 多线程写入同一 session 不丢消息且按 seq 恢复逻辑顺序") {
        if (!DatabaseManager::instance()->initialize())
            return fail(QStringLiteral("数据库初始化成功"), QStringLiteral("initialize 返回 false"));

        ChatPersistenceService persistence;
        const int threadCount = 4;
        const int messagesPerThread = 25;
        const int totalMessages = threadCount * messagesPerThread;
        std::atomic<int> globalSeq { 0 };
        std::atomic<int> failures { 0 };

        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
            workers.emplace_back([&, threadIndex]() {
                for (int i = 0; i < messagesPerThread; ++i) {
                    const int seq = ++globalSeq;
                    Message msg = Message::createText(
                        fixture.sessionId,
                        QStringLiteral("agent-%1").arg(threadIndex + 1),
                        QStringLiteral("message-%1-%2").arg(threadIndex + 1).arg(i + 1));
                    msg.traceId = QStringLiteral("trace-%1").arg(threadIndex + 1);
                    msg.turnId = QStringLiteral("turn-%1").arg(seq);
                    msg.seq = seq;
                    msg.content.payload.insert(QStringLiteral("thread_index"), threadIndex + 1);
                    msg.content.payload.insert(QStringLiteral("message_index"), i + 1);
                    msg.content.payload.insert(QStringLiteral("run_id"), QStringLiteral("run-%1").arg(seq));
                    if (!persistence.insertMessageToDb(msg, QStringLiteral("concurrency_test")))
                        ++failures;
                }
            });
        }

        for (std::thread& worker : workers)
            worker.join();

        if (failures.load() != 0)
            return fail(QStringLiteral("0 次写入失败"), QString::number(failures.load()));

        const QList<Message> loaded = persistence.loadMessagesFromDb(fixture.sessionId);
        if (loaded.size() != totalMessages)
            return fail(QStringLiteral("总消息数 %1").arg(totalMessages), QString::number(loaded.size()));

        qint64 previousSeq = 0;
        QSet<QString> ids;
        for (int i = 0; i < loaded.size(); ++i) {
            const Message& msg = loaded.at(i);
            if (!msg.isValid())
                return fail(QStringLiteral("所有消息都有效"), QStringLiteral("存在无效消息"));
            if (msg.seq <= previousSeq)
                return fail(QStringLiteral("seq 严格递增"), QStringLiteral("%1 <= %2").arg(msg.seq).arg(previousSeq));
            previousSeq = msg.seq;
            if (ids.contains(msg.id))
                return fail(QStringLiteral("消息 ID 唯一"), msg.id);
            ids.insert(msg.id);
            if (msg.content.payload.value(QStringLiteral("run_id")).toString().trimmed().isEmpty())
                return fail(QStringLiteral("payload.run_id 非空"), QStringLiteral("<empty>"));
        }

        const qint64 maxRowId = persistence.maxMessageRowId(fixture.sessionId);
        if (maxRowId < totalMessages)
            return fail(QStringLiteral("maxRowId >= %1").arg(totalMessages), QString::number(maxRowId));

        const QList<Message> incremental = persistence.loadNewMessagesFromDb(fixture.sessionId, 0);
        if (incremental.size() != totalMessages)
            return fail(QStringLiteral("增量查询返回 %1 条").arg(totalMessages), QString::number(incremental.size()));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}
