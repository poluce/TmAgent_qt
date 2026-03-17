#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QUuid>

#include "core/persistence/ChatPersistenceService.h"
#include "TaskStateService.h"

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

struct TaskStateFixture {
    QString rootPath;
    QString sessionId;

    TaskStateFixture()
    {
        rootPath = QDir::temp().filePath(
            QStringLiteral("tmagent-task-state-test-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        sessionId = QStringLiteral("task-state-session-%1")
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        qputenv("TMAGENT_TEST_TASK_STATE_ROOT", QDir::toNativeSeparators(rootPath).toUtf8());
        QDir().mkpath(rootPath);
    }

    ~TaskStateFixture()
    {
        QDir(rootPath).removeRecursively();
        qunsetenv("TMAGENT_TEST_TASK_STATE_ROOT");
    }
};

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "      TaskStateService 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    TaskStateFixture fixture;
    ChatPersistenceService persistence;
    TaskStateService service;
    service.setPersistence(&persistence);

    TEST("updateState - 可写入 queued 初始状态并持久化") {
        QJsonObject merged;
        if (!service.updateState(
                fixture.sessionId,
                QJsonObject {
                    { QStringLiteral("state"), QStringLiteral("queued") },
                    { QStringLiteral("summary"), QStringLiteral("整理发布任务") },
                    { QStringLiteral("current_step"), QStringLiteral("等待调度") },
                    { QStringLiteral("next_step"), QStringLiteral("准备执行") }
                },
                &merged)) {
            return fail(QStringLiteral("updateState 成功"), QStringLiteral("返回 false"));
        }
        if (merged.value(QStringLiteral("state")).toString() != QStringLiteral("queued"))
            return fail(QStringLiteral("queued"), merged.value(QStringLiteral("state")).toString());
        if (merged.value(QStringLiteral("session_id")).toString() != fixture.sessionId)
            return fail(fixture.sessionId, merged.value(QStringLiteral("session_id")).toString());
        if (merged.value(QStringLiteral("updated_at_utc")).toString().trimmed().isEmpty())
            return fail(QStringLiteral("非空 updated_at_utc"), QStringLiteral("<empty>"));
        return 0;
    } END_TEST

    TEST("updateState - 可从 running 切到 blocked 并保留 waiting_job_id") {
        service.updateState(
            fixture.sessionId,
            QJsonObject {
                { QStringLiteral("state"), QStringLiteral("running") },
                { QStringLiteral("turn_id"), QStringLiteral("turn-1") },
                { QStringLiteral("summary"), QStringLiteral("执行发布前检查") }
            });
        QJsonObject merged;
        service.updateState(
            fixture.sessionId,
            QJsonObject {
                { QStringLiteral("state"), QStringLiteral("blocked") },
                { QStringLiteral("waiting_job_id"), QStringLiteral("job-123") },
                { QStringLiteral("current_step"), QStringLiteral("等待后台任务完成") },
                { QStringLiteral("next_step"), QStringLiteral("可查询进度") }
            },
            &merged);
        if (merged.value(QStringLiteral("state")).toString() != QStringLiteral("blocked"))
            return fail(QStringLiteral("blocked"), merged.value(QStringLiteral("state")).toString());
        if (merged.value(QStringLiteral("waiting_job_id")).toString() != QStringLiteral("job-123"))
            return fail(QStringLiteral("job-123"), merged.value(QStringLiteral("waiting_job_id")).toString());
        if (merged.value(QStringLiteral("turn_id")).toString() != QStringLiteral("turn-1"))
            return fail(QStringLiteral("turn-1"), merged.value(QStringLiteral("turn_id")).toString());
        return 0;
    } END_TEST

    TEST("updateState - 传入 Null 可清空 next_step / waiting_job_id") {
        QJsonObject merged;
        service.updateState(
            fixture.sessionId,
            QJsonObject {
                { QStringLiteral("state"), QStringLiteral("done") },
                { QStringLiteral("next_step"), QJsonValue::Null },
                { QStringLiteral("waiting_job_id"), QJsonValue::Null }
            },
            &merged);
        if (merged.contains(QStringLiteral("next_step")))
            return fail(QStringLiteral("next_step 被移除"), merged.value(QStringLiteral("next_step")).toString());
        if (merged.contains(QStringLiteral("waiting_job_id")))
            return fail(QStringLiteral("waiting_job_id 被移除"), merged.value(QStringLiteral("waiting_job_id")).toString());
        if (merged.value(QStringLiteral("state")).toString() != QStringLiteral("done"))
            return fail(QStringLiteral("done"), merged.value(QStringLiteral("state")).toString());
        return 0;
    } END_TEST

    TEST("stateForSession - 可从磁盘恢复上次状态") {
        TaskStateService reloaded;
        reloaded.setPersistence(&persistence);
        const QJsonObject state = reloaded.stateForSession(fixture.sessionId);
        if (state.value(QStringLiteral("state")).toString() != QStringLiteral("done"))
            return fail(QStringLiteral("done"), state.value(QStringLiteral("state")).toString());
        if (state.value(QStringLiteral("summary")).toString() != QStringLiteral("执行发布前检查"))
            return fail(QStringLiteral("执行发布前检查"), state.value(QStringLiteral("summary")).toString());
        return 0;
    } END_TEST

    TEST("clearState - 可删除状态文件与缓存") {
        if (!service.clearState(fixture.sessionId))
            return fail(QStringLiteral("clearState 成功"), QStringLiteral("返回 false"));
        const QJsonObject state = service.stateForSession(fixture.sessionId);
        if (!state.isEmpty())
            return fail(QStringLiteral("空对象"), QStringLiteral("仍有状态"));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

