#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QUuid>

#include "core/model/Message.h"
#include "core/persistence/ChatPersistenceService.h"

namespace {

int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
    return 1;
}

struct Fixture {
    QString rootPath;

    Fixture()
    {
        rootPath = QDir::temp().filePath(
            QStringLiteral("tmagent-teammate-reply-test-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        qputenv("TMAGENT_HOME", QDir::toNativeSeparators(rootPath).toUtf8());
        QDir().mkpath(rootPath);
    }

    ~Fixture()
    {
        QDir(rootPath).removeRecursively();
        qunsetenv("TMAGENT_HOME");
    }
};

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    Fixture fixture;
    Q_UNUSED(fixture);

    ChatPersistenceService persistence;

    const QString sessionId = QStringLiteral("session-%1")
                                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString teammateId = QStringLiteral("mate-123");
    const QString teammateName = QStringLiteral("worker_1");
    const QString status = QStringLiteral("completed");
    const QString rawContent = QStringLiteral("已在主助手工作区创建 worker_1_to_main.txt");
    const QString threadId = QStringLiteral("thread-xyz");

    Message msg = Message::createTeammateReply(
        sessionId, teammateId, teammateName, status, rawContent, threadId);
    msg.visibleInChat = false;

    if (msg.content.type != MessageContent::Type::TeammateReply)
        return fail(QStringLiteral("TeammateReply"), QStringLiteral("other"));
    if (msg.senderId != QLatin1String("system"))
        return fail(QStringLiteral("system"), msg.senderId);

    const QJsonObject json = persistence.messageToJson(msg);
    const Message restored = persistence.messageFromJson(json, sessionId);

    if (restored.content.type != MessageContent::Type::TeammateReply)
        return fail(QStringLiteral("TeammateReply"), QStringLiteral("roundtrip other"));
    if (restored.content.payload.value(QStringLiteral("teammate_id")).toString() != teammateId)
        return fail(teammateId,
                    restored.content.payload.value(QStringLiteral("teammate_id")).toString());
    if (restored.content.payload.value(QStringLiteral("teammate_name")).toString() != teammateName)
        return fail(teammateName,
                    restored.content.payload.value(QStringLiteral("teammate_name")).toString());
    if (restored.content.payload.value(QStringLiteral("status")).toString() != status)
        return fail(status,
                    restored.content.payload.value(QStringLiteral("status")).toString());
    if (restored.content.payload.value(QStringLiteral("thread_id")).toString() != threadId)
        return fail(threadId,
                    restored.content.payload.value(QStringLiteral("thread_id")).toString());
    if (restored.content.payload.value(QStringLiteral("raw_content")).toString() != rawContent)
        return fail(rawContent,
                    restored.content.payload.value(QStringLiteral("raw_content")).toString());
    if (restored.visibleInChat)
        return fail(QStringLiteral("visibleInChat=false"), QStringLiteral("true"));

    qDebug().noquote() << "TeammateReply message roundtrip test passed.";
    return 0;
}
