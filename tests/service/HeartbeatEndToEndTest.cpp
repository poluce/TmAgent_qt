#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUuid>

#include <functional>

#include "core/manager/IdentityManager.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include "ApplicationServices.h"
#include "HeartbeatService.h"
#include "SchedulerService.h"

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

struct CapturedFinish {
    QString sessionId;
    QString fullContent;
};

struct PlannedResponse {
    QString text;
    int statusCode = 200;
};

static int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
    return 1;
}

static bool waitForCondition(int timeoutMs, const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

class MockAnthropicServer final : public QObject {
public:
    explicit MockAnthropicServer(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &MockAnthropicServer::onNewConnection);
    }

    bool start(QString* error = nullptr)
    {
        if (m_server.listen(QHostAddress::LocalHost, 0))
            return true;
        if (error)
            *error = m_server.errorString();
        return false;
    }

    quint16 port() const
    {
        return m_server.serverPort();
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(port());
    }

    void enqueueTextReply(const QString& text)
    {
        PlannedResponse response;
        response.text = text;
        m_responses.append(response);
    }

    int receivedMessageRequests() const
    {
        return m_messageRequests.size();
    }

    void clearCapturedRequests()
    {
        m_messageRequests.clear();
    }

private:
    void onNewConnection()
    {
        while (m_server.hasPendingConnections()) {
            QTcpSocket* socket = m_server.nextPendingConnection();
            if (!socket)
                continue;
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                handleReadyRead(socket);
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

private:
    void handleReadyRead(QTcpSocket* socket)
    {
        if (!socket)
            return;

        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();

        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;

        const QByteArray headerPart = buffer.left(headerEnd);
        const QList<QByteArray> headerLines = headerPart.split('\n');
        if (headerLines.isEmpty())
            return;

        const QByteArray requestLine = headerLines.first().trimmed();
        const QList<QByteArray> requestParts = requestLine.split(' ');
        if (requestParts.size() < 2)
            return;

        const QByteArray method = requestParts.at(0).trimmed().toUpper();
        const QByteArray path = requestParts.at(1).trimmed();

        int contentLength = 0;
        for (const QByteArray& rawLine : headerLines.mid(1)) {
            const QByteArray line = rawLine.trimmed();
            const int colon = line.indexOf(':');
            if (colon <= 0)
                continue;
            const QByteArray key = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (key == "content-length")
                contentLength = value.toInt();
        }

        const QByteArray body = buffer.mid(headerEnd + 4);
        if (body.size() < contentLength)
            return;

        if (method == "POST" && path == "/v1/messages") {
            const QJsonDocument doc = QJsonDocument::fromJson(body.left(contentLength));
            if (doc.isObject())
                m_messageRequests.append(doc.object());
            const PlannedResponse response = m_responses.isEmpty() ? PlannedResponse {} : m_responses.takeFirst();
            writeStreamingResponse(socket, response.text, response.statusCode);
        } else {
            writePlainResponse(socket, QByteArrayLiteral("OK"), 200, QByteArrayLiteral("text/plain; charset=utf-8"));
        }

        m_buffers.remove(socket);
    }

    void writePlainResponse(QTcpSocket* socket,
                            const QByteArray& body,
                            int statusCode,
                            const QByteArray& contentType)
    {
        if (!socket)
            return;
        QByteArray response;
        response += "HTTP/1.1 ";
        response += QByteArray::number(statusCode);
        response += " OK\r\n";
        response += "Content-Type: ";
        response += contentType;
        response += "\r\nContent-Length: ";
        response += QByteArray::number(body.size());
        response += "\r\nConnection: close\r\n\r\n";
        response += body;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    void writeStreamingResponse(QTcpSocket* socket, const QString& text, int statusCode)
    {
        QJsonObject deltaObj;
        deltaObj.insert(QStringLiteral("type"), QStringLiteral("text_delta"));
        deltaObj.insert(QStringLiteral("text"), text);

        QJsonObject contentDelta;
        contentDelta.insert(QStringLiteral("type"), QStringLiteral("content_block_delta"));
        contentDelta.insert(QStringLiteral("delta"), deltaObj);

        QJsonObject messageDelta;
        messageDelta.insert(QStringLiteral("type"), QStringLiteral("message_delta"));
        messageDelta.insert(QStringLiteral("delta"), QJsonObject {
                                                    { QStringLiteral("stop_reason"), QStringLiteral("end_turn") }
                                                });

        QByteArray body;
        body += "data: ";
        body += QJsonDocument(contentDelta).toJson(QJsonDocument::Compact);
        body += "\n\n";
        body += "data: ";
        body += QJsonDocument(messageDelta).toJson(QJsonDocument::Compact);
        body += "\n\n";

        QByteArray response;
        response += "HTTP/1.1 ";
        response += QByteArray::number(statusCode);
        response += " OK\r\n";
        response += "Content-Type: text/event-stream\r\n";
        response += "Cache-Control: no-cache\r\n";
        response += "Connection: close\r\n";
        response += "Content-Length: ";
        response += QByteArray::number(body.size());
        response += "\r\n\r\n";
        response += body;
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QList<PlannedResponse> m_responses;
    QList<QJsonObject> m_messageRequests;
};

struct HeartbeatE2EFixture {
    struct CreatedAgentSession {
        QString sessionId;
        QString agentId;
    };

    QString homeRoot;
    QString dataRoot;
    QString modelConfigPath;
    MockAnthropicServer server;
    ApplicationServices chatService;
    QList<QJsonObject> events;
    QList<CapturedFinish> finishes;
    QList<CreatedAgentSession> created;

    explicit HeartbeatE2EFixture(const QString& rootPath)
    {
        homeRoot = rootPath;
        dataRoot = QDir(homeRoot).filePath(QStringLiteral(".tmagent"));
        QDir().mkpath(homeRoot);

        QString error;
        if (!server.start(&error)) {
            qFatal("MockAnthropicServer start failed: %s", qPrintable(error));
        }

        modelConfigPath = QDir(dataRoot).filePath(QStringLiteral("config/models.yaml"));
        writeModelConfig();

        AppEventHub* eventHub = chatService.events();
        Q_ASSERT(eventHub);
        QObject::connect(eventHub, &AppEventHub::conversationEvent, &chatService, [this](const QJsonObject& event) {
            events.append(event);
        });
        QObject::connect(eventHub, &AppEventHub::finished, &chatService, [this](const QString& sessionId, const QString& fullContent) {
            finishes.append({ sessionId, fullContent });
        });

        chatService.setModelConfigPathOverride(modelConfigPath);
        chatService.initialize();
    }

    ~HeartbeatE2EFixture()
    {
        const QString currentUserId = userId();
        for (const CreatedAgentSession& item : created) {
            if (!item.sessionId.isEmpty())
                chatService.removeSessionAs(currentUserId, item.sessionId);
        }
        for (const CreatedAgentSession& item : created) {
            if (!item.agentId.isEmpty()) {
                chatService.stopHeartbeatForAgent(item.agentId);
                chatService.removeAgentMemoryAs(currentUserId, item.agentId);
                IdentityManager::instance()->removeAgent(item.agentId);
            }
        }
        chatService.saveSessionsToDisk();
        QDir(homeRoot).removeRecursively();
    }

    void writeModelConfig() const
    {
        const QString yaml = QStringLiteral(
                                 "# heartbeat e2e test config\n"
                                 "schema_version: 2\n"
                                 "providers:\n"
                                 "  - instance_id: mock-heartbeat\n"
                                 "    display_name: Mock Heartbeat\n"
                                 "    provider_type: anthropic\n"
                                 "    base_url: %1\n"
                                 "    api_key: test-key\n"
                                 "    auth_type: X-API-Key\n"
                                 "    enabled: true\n"
                                 "    default_temperature: 0\n"
                                 "    default_max_tokens: 512\n"
                                 "    default_timeout_ms: 30000\n"
                                 "    tool_calling: false\n"
                                 "    context_length: 8192\n"
                                 "default_provider: mock-heartbeat\n"
                                 "default_model: mock-heartbeat-model\n")
                                 .arg(server.baseUrl());

        QDir().mkpath(QFileInfo(modelConfigPath).absolutePath());
        QFile file(modelConfigPath);
        file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
        file.write(yaml.toUtf8());
        file.close();
    }

    Session* createAgentSession(const QString& title)
    {
        events.clear();
        finishes.clear();
        server.clearCapturedRequests();
        Session* session = chatService.createNewSession(title);
        const QString agentId = agentIdForSession(session);
        if (session && !agentId.isEmpty())
            created.append({ session->id(), agentId });
        return session;
    }

    QString userId() const
    {
        Identity* user = IdentityManager::instance()->userIdentity();
        return user ? user->id() : QString();
    }

    QString agentIdForSession(Session* session) const
    {
        if (!session)
            return QString();
        const QString currentUserId = userId();
        for (const QString& participantId : session->participantIds()) {
            if (participantId != currentUserId)
                return participantId;
        }
        return QString();
    }

    QString heartbeatStatePath(const QString& agentId) const
    {
        ChatPersistenceService persistence;
        return QDir(QDir(persistence.agentsDirPath()).filePath(agentId.trimmed()))
            .filePath(QStringLiteral("heartbeat_state.json"));
    }

    int assistantMessageCount(Session* session, const QString& agentId) const
    {
        if (!session)
            return 0;
        int count = 0;
        const QList<Message> all = session->allMessages();
        for (const Message& msg : all) {
            if (msg.senderId == agentId)
                ++count;
        }
        return count;
    }

    QList<QJsonObject> eventsForSession(const QString& sessionId, const QString& type) const
    {
        QList<QJsonObject> result;
        for (const QJsonObject& event : events) {
            if (event.value(QStringLiteral("type")).toString() != type)
                continue;
            if (!sessionId.isEmpty() && event.value(QStringLiteral("session_id")).toString() != sessionId)
                continue;
            result.append(event);
        }
        return result;
    }

    QList<QJsonObject> eventsForAgent(const QString& agentId, const QString& type) const
    {
        QList<QJsonObject> result;
        for (const QJsonObject& event : events) {
            if (event.value(QStringLiteral("type")).toString() != type)
                continue;
            if (!agentId.isEmpty() && event.value(QStringLiteral("agent_id")).toString() != agentId)
                continue;
            result.append(event);
        }
        return result;
    }

    HeartbeatConfig defaultHeartbeatConfig() const
    {
        HeartbeatConfig cfg;
        cfg.enabled = true;
        cfg.intervalMs = 60 * 60 * 1000;
        cfg.coalesceMs = 10;
        cfg.duplicateWindowMs = 24 * 60 * 60 * 1000;
        cfg.silentWhenNoChange = true;
        cfg.notifyOnChangeOnly = true;
        cfg.notifyMinIntervalMs = 0;
        cfg.persistStateOnNoChange = true;
        cfg.statePersistIntervalMs = 1000;
        cfg.snapshotSignals = QStringList { QStringLiteral("scheduler_jobs") };
        cfg.activeHours.start = QTime(0, 0);
        cfg.activeHours.end = QTime(23, 59);
        cfg.activeHours.timezone = QStringLiteral("UTC");
        return cfg;
    }
};

} // namespace

int main(int argc, char* argv[])
{
    const QString tempHome = QDir::temp().filePath(
        QStringLiteral("tmagent-heartbeat-e2e-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    qputenv("HOME", QDir::toNativeSeparators(tempHome).toUtf8());
    qputenv("USERPROFILE", QDir::toNativeSeparators(tempHome).toUtf8());
    qputenv("TMAGENT_MCP_SERVERS", QByteArray());

    qSetMessagePattern(QStringLiteral("%{message}"));
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "      Heartbeat 端到端验收";
    qDebug().noquote() << "════════════════════════════════════════";

    HeartbeatE2EFixture fixture(tempHome);

    TEST("手动心跳：消息可见，且会触发记忆反思") {
        Session* session = fixture.createAgentSession(QStringLiteral("HB Manual"));
        if (!session)
            return fail(QStringLiteral("创建会话成功"), QStringLiteral("创建会话失败"));
        const QString agentId = fixture.agentIdForSession(session);
        if (agentId.isEmpty())
            return fail(QStringLiteral("找到 agentId"), QStringLiteral("agentId 为空"));

        HeartbeatConfig cfg = fixture.defaultHeartbeatConfig();
        fixture.chatService.updateHeartbeatConfig(agentId, cfg);

        const int assistantBefore = fixture.assistantMessageCount(session, agentId);
        fixture.server.enqueueTextReply(QStringLiteral("手动巡检：发现 1 个需要关注的事项。"));
        fixture.events.clear();
        fixture.finishes.clear();

        fixture.chatService.triggerHeartbeatForAgent(agentId, QStringLiteral("manual_ui"));

        if (!waitForCondition(8000, [&]() {
                return !fixture.eventsForSession(session->id(), QStringLiteral("memory.reflected")).isEmpty();
            })) {
            return fail(QStringLiteral("收到 memory.reflected"), QStringLiteral("超时未收到"));
        }

        const QList<QJsonObject> reflected = fixture.eventsForSession(session->id(), QStringLiteral("memory.reflected"));
        const QList<QJsonObject> completed = fixture.eventsForSession(session->id(), QStringLiteral("turn_completed"));
        if (completed.isEmpty())
            return fail(QStringLiteral("收到 turn_completed"), QStringLiteral("未收到"));
        if (completed.last().value(QStringLiteral("fullContent")).toString().trimmed().isEmpty())
            return fail(QStringLiteral("手动心跳 fullContent 非空"), QStringLiteral("fullContent 为空"));
        if (fixture.finishes.isEmpty() || fixture.finishes.last().fullContent.trimmed().isEmpty())
            return fail(QStringLiteral("finished 信号带正文"), QStringLiteral("finished 为空"));
        if (fixture.assistantMessageCount(session, agentId) != assistantBefore + 1)
            return fail(QStringLiteral("新增 1 条助手消息"),
                        QString::number(fixture.assistantMessageCount(session, agentId) - assistantBefore));
        if (reflected.last().value(QStringLiteral("reflection_trigger")).toString() != QStringLiteral("heartbeat_turn"))
            return fail(QStringLiteral("reflection_trigger=heartbeat_turn"),
                        reflected.last().value(QStringLiteral("reflection_trigger")).toString());
        const QString reflectedPath = reflected.last().value(QStringLiteral("path")).toString();
        if (reflectedPath.trimmed().isEmpty() || !QFileInfo::exists(reflectedPath))
            return fail(QStringLiteral("反思产物文件存在"), reflectedPath);
        return 0;
    } END_TEST

    TEST("后台心跳：无关键更新静默，但仍触发记忆反思") {
        Session* session = fixture.createAgentSession(QStringLiteral("HB No Change"));
        if (!session)
            return fail(QStringLiteral("创建会话成功"), QStringLiteral("创建会话失败"));
        const QString agentId = fixture.agentIdForSession(session);
        if (agentId.isEmpty())
            return fail(QStringLiteral("找到 agentId"), QStringLiteral("agentId 为空"));

        HeartbeatConfig cfg = fixture.defaultHeartbeatConfig();
        cfg.silentWhenNoChange = false;
        cfg.notifyOnChangeOnly = false;
        fixture.chatService.updateHeartbeatConfig(agentId, cfg);

        const int assistantBefore = fixture.assistantMessageCount(session, agentId);
        fixture.server.enqueueTextReply(QStringLiteral("当前无关键更新。"));
        fixture.events.clear();
        fixture.finishes.clear();

        fixture.chatService.triggerHeartbeatForAgent(agentId, QStringLiteral("background_probe"));

        if (!waitForCondition(8000, [&]() {
                return !fixture.eventsForSession(session->id(), QStringLiteral("memory.reflected")).isEmpty();
            })) {
            return fail(QStringLiteral("收到 memory.reflected"), QStringLiteral("超时未收到"));
        }

        const QList<QJsonObject> completed = fixture.eventsForSession(session->id(), QStringLiteral("turn_completed"));
        const QList<QJsonObject> skipped = fixture.eventsForSession(session->id(), QStringLiteral("heartbeat.skipped"));
        const QList<QJsonObject> reflected = fixture.eventsForSession(session->id(), QStringLiteral("memory.reflected"));
        if (completed.isEmpty())
            return fail(QStringLiteral("收到 turn_completed"), QStringLiteral("未收到"));
        if (!completed.last().value(QStringLiteral("fullContent")).toString().trimmed().isEmpty())
            return fail(QStringLiteral("后台无变化 fullContent 为空"),
                        completed.last().value(QStringLiteral("fullContent")).toString());
        if (!fixture.finishes.isEmpty())
            return fail(QStringLiteral("finished 不应可见"), fixture.finishes.last().fullContent);
        if (fixture.assistantMessageCount(session, agentId) != assistantBefore)
            return fail(QStringLiteral("不新增助手消息"),
                        QString::number(fixture.assistantMessageCount(session, agentId) - assistantBefore));
        if (skipped.isEmpty() || skipped.last().value(QStringLiteral("reason")).toString() != QStringLiteral("no_change_reply"))
            return fail(QStringLiteral("heartbeat.skipped reason=no_change_reply"),
                        skipped.isEmpty() ? QStringLiteral("<none>") : skipped.last().value(QStringLiteral("reason")).toString());
        if (reflected.last().value(QStringLiteral("reflection_trigger")).toString() != QStringLiteral("heartbeat_turn"))
            return fail(QStringLiteral("reflection_trigger=heartbeat_turn"),
                        reflected.last().value(QStringLiteral("reflection_trigger")).toString());

        bool ok = false;
        const QJsonObject state = fixture.chatService.loadHeartbeatRuntimeState(agentId, &ok);
        if (!ok)
            return fail(QStringLiteral("heartbeat 运行时状态可读取"), QStringLiteral("read failed"));
        if (state.value(QStringLiteral("last_duplicate_reason")).toString() != QStringLiteral("no_change_reply"))
            return fail(QStringLiteral("state.last_duplicate_reason=no_change_reply"),
                        state.value(QStringLiteral("last_duplicate_reason")).toString());
        return 0;
    } END_TEST

    TEST("后台心跳：24h 内相同回复会被抑制") {
        Session* session = fixture.createAgentSession(QStringLiteral("HB Duplicate"));
        if (!session)
            return fail(QStringLiteral("创建会话成功"), QStringLiteral("创建会话失败"));
        const QString agentId = fixture.agentIdForSession(session);
        if (agentId.isEmpty())
            return fail(QStringLiteral("找到 agentId"), QStringLiteral("agentId 为空"));

        HeartbeatConfig cfg = fixture.defaultHeartbeatConfig();
        cfg.silentWhenNoChange = false;
        cfg.notifyOnChangeOnly = false;
        fixture.chatService.updateHeartbeatConfig(agentId, cfg);

        const QString duplicateText = QStringLiteral("巡检结论：缓存命中率保持稳定。");

        fixture.server.enqueueTextReply(duplicateText);
        fixture.events.clear();
        fixture.finishes.clear();
        const int assistantBeforeFirst = fixture.assistantMessageCount(session, agentId);
        fixture.chatService.triggerHeartbeatForAgent(agentId, QStringLiteral("background_first"));
        if (!waitForCondition(8000, [&]() {
                return !fixture.eventsForSession(session->id(), QStringLiteral("turn_completed")).isEmpty();
            })) {
            return fail(QStringLiteral("首次收到 turn_completed"), QStringLiteral("超时未收到"));
        }
        if (fixture.assistantMessageCount(session, agentId) != assistantBeforeFirst + 1)
            return fail(QStringLiteral("首次新增 1 条助手消息"),
                        QString::number(fixture.assistantMessageCount(session, agentId) - assistantBeforeFirst));

        fixture.server.enqueueTextReply(duplicateText);
        fixture.events.clear();
        fixture.finishes.clear();
        const int assistantBeforeSecond = fixture.assistantMessageCount(session, agentId);
        fixture.chatService.triggerHeartbeatForAgent(agentId, QStringLiteral("background_second"));

        if (!waitForCondition(8000, [&]() {
                return !fixture.eventsForSession(session->id(), QStringLiteral("heartbeat.skipped")).isEmpty()
                    && !fixture.eventsForSession(session->id(), QStringLiteral("memory.reflected")).isEmpty();
            })) {
            return fail(QStringLiteral("二次收到 skipped+reflected"), QStringLiteral("超时未收到"));
        }

        const QList<QJsonObject> completed = fixture.eventsForSession(session->id(), QStringLiteral("turn_completed"));
        const QList<QJsonObject> skipped = fixture.eventsForSession(session->id(), QStringLiteral("heartbeat.skipped"));
        if (completed.isEmpty())
            return fail(QStringLiteral("二次收到 turn_completed"), QStringLiteral("未收到"));
        if (!completed.last().value(QStringLiteral("fullContent")).toString().trimmed().isEmpty())
            return fail(QStringLiteral("重复回复 fullContent 为空"),
                        completed.last().value(QStringLiteral("fullContent")).toString());
        if (!fixture.finishes.isEmpty())
            return fail(QStringLiteral("重复回复不触发 finished"), fixture.finishes.last().fullContent);
        if (fixture.assistantMessageCount(session, agentId) != assistantBeforeSecond)
            return fail(QStringLiteral("重复回复不新增助手消息"),
                        QString::number(fixture.assistantMessageCount(session, agentId) - assistantBeforeSecond));
        if (skipped.isEmpty() || skipped.last().value(QStringLiteral("reason")).toString() != QStringLiteral("duplicate_suppressed"))
            return fail(QStringLiteral("heartbeat.skipped reason=duplicate_suppressed"),
                        skipped.isEmpty() ? QStringLiteral("<none>") : skipped.last().value(QStringLiteral("reason")).toString());

        bool ok = false;
        const QJsonObject state = fixture.chatService.loadHeartbeatRuntimeState(agentId, &ok);
        if (!ok)
            return fail(QStringLiteral("heartbeat 运行时状态可读取"), QStringLiteral("read failed"));
        if (state.value(QStringLiteral("last_duplicate_reason")).toString() != QStringLiteral("duplicate_suppressed"))
            return fail(QStringLiteral("state.last_duplicate_reason=duplicate_suppressed"),
                        state.value(QStringLiteral("last_duplicate_reason")).toString());
        return 0;
    } END_TEST

    TEST("后台心跳：watch signal 变化后会投递可见消息") {
        Session* session = fixture.createAgentSession(QStringLiteral("HB Change"));
        if (!session)
            return fail(QStringLiteral("创建会话成功"), QStringLiteral("创建会话失败"));
        const QString agentId = fixture.agentIdForSession(session);
        if (agentId.isEmpty())
            return fail(QStringLiteral("找到 agentId"), QStringLiteral("agentId 为空"));

        HeartbeatConfig cfg = fixture.defaultHeartbeatConfig();
        cfg.silentWhenNoChange = true;
        cfg.notifyOnChangeOnly = true;
        fixture.chatService.updateHeartbeatConfig(agentId, cfg);

        fixture.events.clear();
        fixture.finishes.clear();
        fixture.chatService.triggerHeartbeatForAgent(agentId, QStringLiteral("baseline_snapshot"));
        if (!waitForCondition(3000, [&]() {
                return !fixture.eventsForAgent(agentId, QStringLiteral("heartbeat.completed")).isEmpty();
            })) {
            return fail(QStringLiteral("收到 heartbeat.completed"), QStringLiteral("超时未收到"));
        }
        if (fixture.server.receivedMessageRequests() != 0)
            return fail(QStringLiteral("基线快照不发 LLM 请求"),
                        QString::number(fixture.server.receivedMessageRequests()));

        ScheduledJob job;
        job.name = QStringLiteral("Heartbeat E2E Job");
        job.agentId = agentId;
        job.prompt = QStringLiteral("noop");
        job.cronExpr = QStringLiteral("* * * * *");
        job.timezone = QStringLiteral("UTC");
        fixture.chatService.addScheduledJob(job);

        const int assistantBefore = fixture.assistantMessageCount(session, agentId);
        fixture.server.clearCapturedRequests();
        fixture.server.enqueueTextReply(QStringLiteral("检测到计划任务变化：新增了 1 个调度任务。"));
        fixture.events.clear();
        fixture.finishes.clear();

        fixture.chatService.triggerHeartbeatForAgent(agentId, QStringLiteral("background_change"));

        if (!waitForCondition(8000, [&]() {
                return !fixture.eventsForSession(session->id(), QStringLiteral("turn_completed")).isEmpty();
            })) {
            return fail(QStringLiteral("变化后收到 turn_completed"), QStringLiteral("超时未收到"));
        }

        const QList<QJsonObject> triggered = fixture.eventsForAgent(agentId, QStringLiteral("heartbeat.triggered"));
        const QList<QJsonObject> completed = fixture.eventsForSession(session->id(), QStringLiteral("turn_completed"));
        if (triggered.isEmpty())
            return fail(QStringLiteral("收到 heartbeat.triggered"), QStringLiteral("未收到"));
        if (!triggered.last().value(QStringLiteral("has_actionable_change")).toBool())
            return fail(QStringLiteral("has_actionable_change=true"), QStringLiteral("false"));
        if (completed.last().value(QStringLiteral("fullContent")).toString().trimmed().isEmpty())
            return fail(QStringLiteral("变化回复 fullContent 非空"),
                        completed.last().value(QStringLiteral("fullContent")).toString());
        if (fixture.finishes.isEmpty() || fixture.finishes.last().fullContent.trimmed().isEmpty())
            return fail(QStringLiteral("变化回复 finished 非空"), QStringLiteral("finished 为空"));
        if (fixture.assistantMessageCount(session, agentId) != assistantBefore + 1)
            return fail(QStringLiteral("变化后新增 1 条助手消息"),
                        QString::number(fixture.assistantMessageCount(session, agentId) - assistantBefore));

        bool ok = false;
        const QJsonObject state = fixture.chatService.loadHeartbeatRuntimeState(agentId, &ok);
        if (!ok)
            return fail(QStringLiteral("heartbeat 运行时状态可读取"), QStringLiteral("read failed"));
        if (state.value(QStringLiteral("last_delivered_digest")).toString().trimmed().isEmpty())
            return fail(QStringLiteral("state.last_delivered_digest 已写入"), QStringLiteral("<empty>"));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

