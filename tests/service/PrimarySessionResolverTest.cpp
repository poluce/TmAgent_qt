#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QUuid>

#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/IdentityProfile.h"
#include "PrimarySessionResolver.h"

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
    IdentityManager* identityManager = IdentityManager::instance();
    SessionManager* sessionManager = SessionManager::instance();
    Identity* user = nullptr;
    Identity* agent = nullptr;

    Fixture()
    {
        const QString seed = QUuid::createUuid().toString(QUuid::WithoutBraces);
        user = identityManager->userIdentity(QStringLiteral("user-%1").arg(seed));
        agent = identityManager->createAgent(
            QStringLiteral("Agent-%1").arg(seed),
            new IdentityProfile(),
            QStringLiteral("agent-%1").arg(seed));
    }

    PrimarySessionResolver makeResolver()
    {
        PrimarySessionResolver::Dependencies dependencies;
        dependencies.identityManager = identityManager;
        dependencies.sessionManager = sessionManager;
        dependencies.userIdentityId = [this]() { return user ? user->id() : QString(); };
        dependencies.createSessionForIdentityAs = [this](const QString& actorIdentityId,
                                                         const QString& identityId,
                                                         const QString& title) {
            Q_UNUSED(actorIdentityId);
            Session* session = sessionManager->createPrivateSession(user->id(), identityId);
            if (session)
                session->setTitle(title);
            return session;
        };
        return PrimarySessionResolver(dependencies);
    }
};

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "  PrimarySessionResolver 测试";
    qDebug().noquote() << "════════════════════════════════════════";

    TEST("已有会话时返回最近活跃会话") {
        Fixture f;
        Session* older = f.sessionManager->createPrivateSession(f.user->id(), f.agent->id());
        QThread::msleep(20);
        Session* newer = f.sessionManager->createPrivateSession(f.user->id(), f.agent->id());
        if (!older || !newer)
            return fail(QStringLiteral("成功创建 2 个 session"), QStringLiteral("create failed"));
        QThread::msleep(20);
        older->addMessage(Message::createText(older->id(), f.user->id(), QStringLiteral("touch older")));

        PrimarySessionResolver resolver = f.makeResolver();
        const QString result = resolver.resolveForAgent(f.agent->id(), true, false, QString());
        if (result != older->id())
            return fail(older->id(), result);
        return 0;
    } END_TEST

    TEST("无会话且不允许创建时返回空") {
        Fixture f;
        PrimarySessionResolver resolver = f.makeResolver();
        const QString result = resolver.resolveForAgent(f.agent->id(), false, false, QString());
        if (!result.isEmpty())
            return fail(QStringLiteral("<empty>"), result);
        return 0;
    } END_TEST

    TEST("无会话且允许创建时创建新会话") {
        Fixture f;
        PrimarySessionResolver resolver = f.makeResolver();
        const QString result = resolver.resolveForAgent(f.agent->id(), true, false, QStringLiteral("scheduler"));
        Session* session = f.sessionManager->findById(result);
        if (!session)
            return fail(QStringLiteral("新会话"), QStringLiteral("<not found>"));
        if (!session->title().contains(QStringLiteral("[scheduler]")))
            return fail(QStringLiteral("标题包含 [scheduler]"), session->title());
        return 0;
    } END_TEST

    TEST("isolated=true 时即便已有会话也创建隔离会话") {
        Fixture f;
        Session* existing = f.sessionManager->createPrivateSession(f.user->id(), f.agent->id());
        existing->setTitle(QStringLiteral("Existing"));

        PrimarySessionResolver resolver = f.makeResolver();
        const QString result = resolver.resolveForAgent(f.agent->id(), true, true, QStringLiteral("heartbeat"));
        if (result == existing->id())
            return fail(QStringLiteral("新 sessionId"), result);
        Session* session = f.sessionManager->findById(result);
        if (!session)
            return fail(QStringLiteral("隔离会话"), QStringLiteral("<not found>"));
        if (!session->title().contains(QStringLiteral("[heartbeat]")))
            return fail(QStringLiteral("标题包含 [heartbeat]"), session->title());
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果：%1/%2 通过").arg(g_passCount).arg(g_testCount);
    PRINT_DIVIDER();
    return g_passCount == g_testCount ? 0 : 1;
}

