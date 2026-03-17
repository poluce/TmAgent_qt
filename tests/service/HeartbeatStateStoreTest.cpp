#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>

#include "HeartbeatStateStore.h"

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
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "  HeartbeatStateStore 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("优先从 app state 加载 heartbeat 运行时状态") {
        const QJsonObject stored {
            { QStringLiteral("last_snapshot_digest"), QStringLiteral("digest-1") },
            { QStringLiteral("last_snapshot"), QJsonObject { { QStringLiteral("k"), 1 } } },
            { QStringLiteral("last_notify_at_utc"), QStringLiteral("2026-03-16T01:02:03.000Z") },
            { QStringLiteral("last_delivered_at_utc"), QStringLiteral("2026-03-16T02:03:04.000Z") },
            { QStringLiteral("last_snapshot_at_utc"), QStringLiteral("2026-03-16T03:04:05.000Z") },
            { QStringLiteral("last_delivered_digest"), QStringLiteral("delivered-1") }
        };

        HeartbeatStateStore store(
            HeartbeatStateStore::Dependencies {
                [stored](const QString&) {
                    return QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact));
                },
                {},
                [](const QString&) { return QJsonObject(); },
                []() { return QStringLiteral("F:/agents"); },
                []() { return true; }
            });

        HeartbeatRuntimeState runtimeState;
        store.load(QStringLiteral("agent-1"), &runtimeState);
        if (!runtimeState.loaded || !runtimeState.hasSnapshot)
            return fail(QStringLiteral("loaded=true, hasSnapshot=true"), QStringLiteral("false"));
        if (runtimeState.lastSnapshotDigest != QStringLiteral("digest-1"))
            return fail(QStringLiteral("digest-1"), runtimeState.lastSnapshotDigest);
        if (runtimeState.lastDeliveredDigest != QStringLiteral("delivered-1"))
            return fail(QStringLiteral("delivered-1"), runtimeState.lastDeliveredDigest);
        if (!runtimeState.statePath.endsWith(QStringLiteral("agent-1/heartbeat_state.json")))
            return fail(QStringLiteral("statePath 以 heartbeat_state.json 结尾"), runtimeState.statePath);
        return 0;
    } END_TEST

    TEST("无 app state 时回退到 json 文件对象") {
        const QJsonObject fallback {
            { QStringLiteral("last_snapshot_digest"), QStringLiteral("digest-file") }
        };
        HeartbeatStateStore store(
            HeartbeatStateStore::Dependencies {
                [](const QString&) { return QString(); },
                {},
                [fallback](const QString&) { return fallback; },
                []() { return QStringLiteral("F:/agents"); },
                []() { return false; }
            });

        HeartbeatRuntimeState runtimeState;
        store.load(QStringLiteral("agent-2"), &runtimeState);
        if (runtimeState.lastSnapshotDigest != QStringLiteral("digest-file"))
            return fail(QStringLiteral("digest-file"), runtimeState.lastSnapshotDigest);
        return 0;
    } END_TEST

    TEST("persist 在 force=false 或 DB 不可用时直接返回 false") {
        HeartbeatStateStore store(
            HeartbeatStateStore::Dependencies {
                {},
                [](const QString&, const QString&) { return true; },
                {},
                []() { return QString(); },
                []() { return false; }
            });

        HeartbeatRuntimeState runtimeState;
        runtimeState.stateStorageKey = QStringLiteral("heartbeat_state:agent-1");
        if (store.persist(QStringLiteral("agent-1"), &runtimeState, QDateTime::currentDateTimeUtc(), true))
            return fail(QStringLiteral("false"), QStringLiteral("true"));
        return 0;
    } END_TEST

    TEST("persist 在 force=true 且 DB 可用时会写入并更新时间") {
        QString savedKey;
        QString savedValue;
        HeartbeatStateStore store(
            HeartbeatStateStore::Dependencies {
                {},
                [&](const QString& key, const QString& value) {
                    savedKey = key;
                    savedValue = value;
                    return true;
                },
                {},
                []() { return QString(); },
                []() { return true; }
            });

        HeartbeatRuntimeState runtimeState;
        runtimeState.stateStorageKey = QStringLiteral("heartbeat_state:agent-3");
        runtimeState.stateObj.insert(QStringLiteral("foo"), QStringLiteral("bar"));
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        if (!store.persist(QStringLiteral("agent-3"), &runtimeState, nowUtc, true))
            return fail(QStringLiteral("true"), QStringLiteral("false"));
        if (savedKey != QStringLiteral("heartbeat_state:agent-3"))
            return fail(QStringLiteral("heartbeat_state:agent-3"), savedKey);
        if (!savedValue.contains(QStringLiteral("\"foo\":\"bar\"")))
            return fail(QStringLiteral("保存 foo=bar"), savedValue);
        if (runtimeState.lastPersistAtUtc != nowUtc)
            return fail(QStringLiteral("lastPersistAtUtc 更新"), runtimeState.lastPersistAtUtc.toString(Qt::ISODateWithMs));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果：%1/%2 通过").arg(g_passCount).arg(g_testCount);
    PRINT_DIVIDER();
    return g_passCount == g_testCount ? 0 : 1;
}

