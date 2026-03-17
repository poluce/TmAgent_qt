#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>

#include "HeartbeatReplyUtils.h"

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

static int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
    return 1;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "    HeartbeatReplyUtils 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QString reply = QStringLiteral("项目巡检完成：后台任务已结束，Provider 状态正常。");
    const QString digest = HeartbeatReplyUtils::replyDigest(reply);

    TEST("normalizeReplyText - 会压缩空白") {
        const QString normalized = HeartbeatReplyUtils::normalizeReplyText(
            QStringLiteral("项目巡检完成：\n\n后台任务已结束。"));
        if (normalized != QStringLiteral("项目巡检完成： 后台任务已结束。"))
            return fail(QStringLiteral("项目巡检完成： 后台任务已结束。"), normalized);
        return 0;
    } END_TEST

    TEST("evaluateReplyDelivery - 背景心跳 24h 内重复内容会抑制") {
        const auto decision = HeartbeatReplyUtils::evaluateReplyDelivery(
            reply,
            true,
            24 * 60 * 60 * 1000,
            nowUtc.addSecs(-60),
            digest,
            nowUtc);
        if (!decision.suppress || decision.reason != QStringLiteral("duplicate_suppressed"))
            return fail(QStringLiteral("duplicate_suppressed"), decision.reason);
        return 0;
    } END_TEST

    TEST("evaluateReplyDelivery - 超过窗口后相同内容不再抑制") {
        const auto decision = HeartbeatReplyUtils::evaluateReplyDelivery(
            reply,
            true,
            24 * 60 * 60 * 1000,
            nowUtc.addSecs(-(24 * 60 * 60 + 5)),
            digest,
            nowUtc);
        if (decision.suppress)
            return fail(QStringLiteral("不抑制"), decision.reason);
        return 0;
    } END_TEST

    TEST("evaluateReplyDelivery - 手动心跳不做重复抑制") {
        const auto decision = HeartbeatReplyUtils::evaluateReplyDelivery(
            reply,
            false,
            24 * 60 * 60 * 1000,
            nowUtc.addSecs(-60),
            digest,
            nowUtc);
        if (decision.suppress)
            return fail(QStringLiteral("手动心跳直接放行"), decision.reason);
        return 0;
    } END_TEST

    TEST("evaluateReplyDelivery - 背景心跳无关键更新保持静默") {
        const auto decision = HeartbeatReplyUtils::evaluateReplyDelivery(
            QStringLiteral("当前无关键更新。"),
            true,
            24 * 60 * 60 * 1000,
            QDateTime(),
            QString(),
            nowUtc);
        if (!decision.suppress || decision.reason != QStringLiteral("no_change_reply"))
            return fail(QStringLiteral("no_change_reply"), decision.reason);
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

