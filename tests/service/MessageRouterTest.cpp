#include <QCoreApplication>
#include <QDebug>

#include "core/service/MessageRouter.h"

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

static QString join(const QStringList& values)
{
    return QStringLiteral("[") + values.join(QStringLiteral(", ")) + QStringLiteral("]");
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "        MessageRouter 测试套件";
    qDebug().noquote() << "════════════════════════════════════════";

    const QString userId = QStringLiteral("user-1");
    const QStringList agents { QStringLiteral("agent-1"), QStringLiteral("agent-2"), QStringLiteral("agent-3") };
    QHash<QString, QString> names;
    names.insert(QStringLiteral("agent-1"), QStringLiteral("架构师"));
    names.insert(QStringLiteral("agent-2"), QStringLiteral("后端"));
    names.insert(QStringLiteral("agent-3"), QStringLiteral("测试"));

    TEST("parseMentions - 支持中文与英文") {
        const QStringList tokens = MessageRouter::parseMentions(QStringLiteral("请@后端 和 @agent-3 看一下，@ALL 也收到"));
        const QStringList expected { QStringLiteral("后端"), QStringLiteral("agent-3"), QStringLiteral("all") };
        if (tokens != expected)
            return fail(join(expected), join(tokens));
        return 0;
    } END_TEST

    TEST("group - 用户无 @ 默认广播所有 Agent") {
        MessageRouter::RouteInput input;
        input.sessionType = Session::SessionType::Group;
        input.senderIdentityId = userId;
        input.userIdentityId = userId;
        input.text = QStringLiteral("今天先同步一下任务");
        input.participantAgentIds = agents;
        input.agentDisplayNames = names;

        const MessageRouter::RouteResult result = MessageRouter::route(input);
        if (result.targetAgentIds != agents)
            return fail(join(agents), join(result.targetAgentIds));
        if (!result.isBroadcast)
            return fail(QStringLiteral("isBroadcast=true"), QStringLiteral("false"));
        if (!result.usedDefaultRoute)
            return fail(QStringLiteral("usedDefaultRoute=true"), QStringLiteral("false"));
        return 0;
    } END_TEST

    TEST("group - @all 广播所有 Agent") {
        MessageRouter::RouteInput input;
        input.sessionType = Session::SessionType::Group;
        input.senderIdentityId = userId;
        input.userIdentityId = userId;
        input.text = QStringLiteral("@all 帮我检查一下");
        input.participantAgentIds = agents;
        input.agentDisplayNames = names;

        const MessageRouter::RouteResult result = MessageRouter::route(input);
        if (result.targetAgentIds != agents)
            return fail(join(agents), join(result.targetAgentIds));
        if (!result.isBroadcast)
            return fail(QStringLiteral("isBroadcast=true"), QStringLiteral("false"));
        return 0;
    } END_TEST

    TEST("group - 按 name 与 id 精准路由") {
        MessageRouter::RouteInput input;
        input.sessionType = Session::SessionType::Group;
        input.senderIdentityId = userId;
        input.userIdentityId = userId;
        input.text = QStringLiteral("@后端 @agent-3 请处理");
        input.participantAgentIds = agents;
        input.agentDisplayNames = names;

        const MessageRouter::RouteResult result = MessageRouter::route(input);
        const QStringList expected { QStringLiteral("agent-2"), QStringLiteral("agent-3") };
        if (result.targetAgentIds != expected)
            return fail(join(expected), join(result.targetAgentIds));
        if (!result.unresolvedMentions.isEmpty())
            return fail(QStringLiteral("[]"), join(result.unresolvedMentions));
        return 0;
    } END_TEST

    TEST("group - 未识别 @ 保留 unresolvedMentions") {
        MessageRouter::RouteInput input;
        input.sessionType = Session::SessionType::Group;
        input.senderIdentityId = userId;
        input.userIdentityId = userId;
        input.text = QStringLiteral("@不存在的角色 看一下");
        input.participantAgentIds = agents;
        input.agentDisplayNames = names;

        const MessageRouter::RouteResult result = MessageRouter::route(input);
        if (!result.targetAgentIds.isEmpty())
            return fail(QStringLiteral("[]"), join(result.targetAgentIds));
        const QStringList expectedUnresolved { QStringLiteral("不存在的角色") };
        if (result.unresolvedMentions != expectedUnresolved)
            return fail(join(expectedUnresolved), join(result.unresolvedMentions));
        return 0;
    } END_TEST

    TEST("group - Agent 发言且无 @ 默认不广播") {
        MessageRouter::RouteInput input;
        input.sessionType = Session::SessionType::Group;
        input.senderIdentityId = QStringLiteral("agent-1");
        input.userIdentityId = userId;
        input.text = QStringLiteral("我先总结一下");
        input.participantAgentIds = agents;
        input.agentDisplayNames = names;

        const MessageRouter::RouteResult result = MessageRouter::route(input);
        if (!result.targetAgentIds.isEmpty())
            return fail(QStringLiteral("[]"), join(result.targetAgentIds));
        if (result.usedDefaultRoute)
            return fail(QStringLiteral("usedDefaultRoute=false"), QStringLiteral("true"));
        return 0;
    } END_TEST

    TEST("private - 无 @ 默认路由到会话 Agent") {
        MessageRouter::RouteInput input;
        input.sessionType = Session::SessionType::Private;
        input.senderIdentityId = userId;
        input.userIdentityId = userId;
        input.text = QStringLiteral("继续");
        input.participantAgentIds = QStringList { QStringLiteral("agent-2") };
        input.agentDisplayNames = names;

        const MessageRouter::RouteResult result = MessageRouter::route(input);
        const QStringList expected { QStringLiteral("agent-2") };
        if (result.targetAgentIds != expected)
            return fail(join(expected), join(result.targetAgentIds));
        if (!result.usedDefaultRoute)
            return fail(QStringLiteral("usedDefaultRoute=true"), QStringLiteral("false"));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";

    return (g_passCount == g_testCount) ? 0 : 1;
}
