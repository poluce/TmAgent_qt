#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
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
#include "core/manager/SessionManager.h"
#include "core/logging/LogQueryEngine.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#define private public
#include "ChatService.h"
#undef private

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

struct PlannedResponse {
    QString text;
    int statusCode = 200;
};

int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
    return 1;
}

bool waitForCondition(int timeoutMs, const std::function<bool()>& predicate)
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

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }

    void enqueueTextReply(const QString& text)
    {
        PlannedResponse response;
        response.text = text;
        m_responses.append(response);
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
};

struct RoutingFixture {
    QString homeRoot;
    QString dataRoot;
    QString modelConfigPath;
    MockAnthropicServer server;
    ChatService chatService;
    QList<QJsonObject> events;
    QList<QString> createdSessionIds;
    QList<QString> createdAgentIds;

    explicit RoutingFixture(const QString& rootPath)
    {
        homeRoot = rootPath;
        dataRoot = QDir(homeRoot).filePath(QStringLiteral(".tmagent"));
        QDir().mkpath(homeRoot);

        QString error;
        if (!server.start(&error))
            qFatal("MockAnthropicServer start failed: %s", qPrintable(error));

        modelConfigPath = QDir(dataRoot).filePath(QStringLiteral("config/models.yaml"));
        writeModelConfig();

        QObject::connect(&chatService, &ChatService::conversationEvent, &chatService, [this](const QJsonObject& event) {
            events.append(event);
        });

        chatService.setModelConfigPathOverride(modelConfigPath);
        chatService.initialize();
    }

    ~RoutingFixture()
    {
        const QString currentUserId = userId();
        for (const QString& sessionId : createdSessionIds) {
            if (!sessionId.isEmpty())
                chatService.removeSessionAs(currentUserId, sessionId);
        }
        for (const QString& agentId : createdAgentIds) {
            if (!agentId.isEmpty()) {
                chatService.abortCurrent(resolveSessionForAgent(agentId));
                chatService.removeAgentMemoryAs(currentUserId, agentId);
                IdentityManager::instance()->removeAgent(agentId);
            }
        }
        chatService.saveSessionsToDisk();
        QDir(homeRoot).removeRecursively();
    }

    void writeModelConfig() const
    {
        const QString yaml = QStringLiteral(
                                 "# message routing integration test config\n"
                                 "schema_version: 2\n"
                                 "providers:\n"
                                 "  - instance_id: mock-routing\n"
                                 "    display_name: Mock Routing\n"
                                 "    provider_type: anthropic\n"
                                 "    base_url: %1\n"
                                 "    api_key: test-key\n"
                                 "    auth_type: X-API-Key\n"
                                 "    enabled: true\n"
                                 "    default_temperature: 0\n"
                                 "    default_max_tokens: 256\n"
                                 "    default_timeout_ms: 30000\n"
                                 "    tool_calling: false\n"
                                 "    context_length: 8192\n"
                                 "default_provider: mock-routing\n"
                                 "default_model: mock-routing-model\n")
                                 .arg(server.baseUrl());

        QDir().mkpath(QFileInfo(modelConfigPath).absolutePath());
        QFile file(modelConfigPath);
        file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
        file.write(yaml.toUtf8());
        file.close();
    }

    QString userId() const
    {
        Identity* user = IdentityManager::instance()->userIdentity();
        return user ? user->id() : QString();
    }

    Identity* createAgent(const QString& name)
    {
        auto* profile = new IdentityProfile();
        const LLMConfig cfg = chatService.defaultAgentConfig();
        profile->setLlmConfig(cfg);
        profile->setSystemPrompt(cfg.systemPrompt);
        profile->setDelegateEnabled(true);
        Identity* agent = IdentityManager::instance()->createAgent(name, profile);
        if (agent)
            createdAgentIds.append(agent->id());
        return agent;
    }

    Session* createGroupSession(const QString& title, const QStringList& agentNames)
    {
        events.clear();
        QStringList participantIds;
        for (const QString& name : agentNames) {
            Identity* agent = createAgent(name);
            if (agent)
                participantIds.append(agent->id());
        }
        Session* session = SessionManager::instance()->createGroupSession(userId(), participantIds, title);
        if (session)
            createdSessionIds.append(session->id());
        return session;
    }

    Session* createPrivateAgentSession(const QString& title)
    {
        events.clear();
        Session* session = chatService.createNewSession(title);
        if (!session)
            return nullptr;
        createdSessionIds.append(session->id());
        const QString agentId = agentIdForSession(session);
        if (!agentId.isEmpty() && !createdAgentIds.contains(agentId))
            createdAgentIds.append(agentId);
        events.clear();
        return session;
    }

    QString agentIdForSession(Session* session) const
    {
        if (!session)
            return QString();
        for (const QString& participantId : session->participantIds()) {
            if (participantId != userId())
                return participantId;
        }
        return QString();
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

    QString resolveSessionForAgent(const QString& agentId) const
    {
        const QList<Session*> sessions = SessionManager::instance()->sessionsForIdentity(agentId);
        if (sessions.isEmpty())
            return QString();
        return sessions.first()->id();
    }
};

} // namespace

int main(int argc, char* argv[])
{
    const QString tempHome = QDir::temp().filePath(
        QStringLiteral("tmagent-routing-e2e-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    qputenv("HOME", QDir::toNativeSeparators(tempHome).toUtf8());
    qputenv("USERPROFILE", QDir::toNativeSeparators(tempHome).toUtf8());
    qputenv("TMAGENT_MCP_SERVERS", QByteArray());

    qSetMessagePattern(QStringLiteral("%{message}"));
    QCoreApplication app(argc, argv);

    qDebug().noquote() << "════════════════════════════════════════";
    qDebug().noquote() << "      Message Routing 集成测试";
    qDebug().noquote() << "════════════════════════════════════════";

    RoutingFixture fixture(tempHome);

    TEST("group route - 精准 @ 会发出 message_routed 且写入 mentions") {
        Session* session = fixture.createGroupSession(
            QStringLiteral("Routing Explicit"),
            QStringList { QStringLiteral("架构师"), QStringLiteral("开发者"), QStringLiteral("测试") });
        if (!session)
            return fail(QStringLiteral("创建 group session 成功"), QStringLiteral("session=null"));

        const QStringList participantIds = session->participantIds();
        const QString architectId = participantIds.value(1);
        const QString testerId = participantIds.value(3);
        fixture.server.enqueueTextReply(QStringLiteral("收到，我来检查。"));

        const QString turnId = fixture.chatService.enqueueUserMessageAs(
            fixture.userId(),
            session->id(),
            QStringLiteral("@架构师 @测试 请看一下"));
        if (turnId.trimmed().isEmpty())
            return fail(QStringLiteral("enqueue 返回 turnId"), QStringLiteral("<empty>"));

        if (!waitForCondition(500, [&]() {
                return !fixture.eventsForSession(session->id(), QStringLiteral("message_routed")).isEmpty()
                    && session->messageCount() >= 1;
            })) {
            return fail(QStringLiteral("收到 message_routed 且写入用户消息"), QStringLiteral("超时"));
        }

        const QJsonObject routed = fixture.eventsForSession(session->id(), QStringLiteral("message_routed")).first();
        const QJsonArray targets = routed.value(QStringLiteral("target_agent_ids")).toArray();
        const QStringList expectedTargets { architectId, testerId };
        QStringList actualTargets;
        for (const QJsonValue& value : targets)
            actualTargets.append(value.toString());
        if (actualTargets != expectedTargets)
            return fail(expectedTargets.join(QStringLiteral(", ")), actualTargets.join(QStringLiteral(", ")));
        if (routed.value(QStringLiteral("is_broadcast")).toBool())
            return fail(QStringLiteral("is_broadcast=false"), QStringLiteral("true"));
        if (routed.value(QStringLiteral("used_default_route")).toBool())
            return fail(QStringLiteral("used_default_route=false"), QStringLiteral("true"));

        const Message firstMessage = session->messageAt(0);
        if (firstMessage.senderId != fixture.userId())
            return fail(fixture.userId(), firstMessage.senderId);
        if (firstMessage.mentions != expectedTargets)
            return fail(expectedTargets.join(QStringLiteral(", ")), firstMessage.mentions.join(QStringLiteral(", ")));
        return 0;
    } END_TEST

    TEST("group route - @all 会广播所有 Agent 并写入 mentions") {
        Session* session = fixture.createGroupSession(
            QStringLiteral("Routing Broadcast"),
            QStringList { QStringLiteral("产品"), QStringLiteral("后端"), QStringLiteral("前端") });
        if (!session)
            return fail(QStringLiteral("创建 group session 成功"), QStringLiteral("session=null"));

        QStringList expectedTargets;
        for (const QString& participantId : session->participantIds()) {
            if (participantId != fixture.userId())
                expectedTargets.append(participantId);
        }
        fixture.server.enqueueTextReply(QStringLiteral("大家都看到了。"));

        const QString turnId = fixture.chatService.enqueueUserMessageAs(
            fixture.userId(),
            session->id(),
            QStringLiteral("@all 今天同步一下进度"));
        if (turnId.trimmed().isEmpty())
            return fail(QStringLiteral("enqueue 返回 turnId"), QStringLiteral("<empty>"));

        if (!waitForCondition(500, [&]() {
                return !fixture.eventsForSession(session->id(), QStringLiteral("message_routed")).isEmpty()
                    && session->messageCount() >= 1;
            })) {
            return fail(QStringLiteral("收到 message_routed 且写入用户消息"), QStringLiteral("超时"));
        }

        const QJsonObject routed = fixture.eventsForSession(session->id(), QStringLiteral("message_routed")).first();
        const QJsonArray targets = routed.value(QStringLiteral("target_agent_ids")).toArray();
        QStringList actualTargets;
        for (const QJsonValue& value : targets)
            actualTargets.append(value.toString());
        if (actualTargets != expectedTargets)
            return fail(expectedTargets.join(QStringLiteral(", ")), actualTargets.join(QStringLiteral(", ")));
        if (!routed.value(QStringLiteral("is_broadcast")).toBool())
            return fail(QStringLiteral("is_broadcast=true"), QStringLiteral("false"));

        const Message firstMessage = session->messageAt(0);
        if (firstMessage.mentions != expectedTargets)
            return fail(expectedTargets.join(QStringLiteral(", ")), firstMessage.mentions.join(QStringLiteral(", ")));
        return 0;
    } END_TEST

    TEST("delegate tool traceability - 父 turn 标识与 child 标识可跨事件/消息/状态反查") {
        Session* session = fixture.createPrivateAgentSession(QStringLiteral("Traceability"));
        if (!session)
            return fail(QStringLiteral("创建 private session 成功"), QStringLiteral("session=null"));
        const QString agentId = fixture.agentIdForSession(session);
        if (agentId.trimmed().isEmpty())
            return fail(QStringLiteral("session 包含 agentId"), QStringLiteral("<empty>"));

        fixture.events.clear();

        const QString parentTraceId = QStringLiteral("trace-parent-%1")
                                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString parentTurnId = QStringLiteral("turn-parent-%1")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString parentRunId = QStringLiteral("run-parent-%1")
                                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString delegateToolId = QStringLiteral("tool-delegate-%1")
                                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString childRequestId = QStringLiteral("child-req-%1")
                                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString childTraceId = QStringLiteral("child-trace-%1")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString childAgentId = QStringLiteral("child-agent-%1")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString waitingJobId = QStringLiteral("job-%1")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

        TurnTask turn;
        turn.requestTraceId = parentTraceId;
        turn.turnId = parentTurnId;
        turn.runId = parentRunId;
        turn.actorIdentityId = fixture.userId();
        turn.userContent = QStringLiteral("请委派子代理做检查");

        SessionPipeline& pipeline = fixture.chatService.m_turnManager.ensurePipeline(session->id());
        pipeline.activeTurn = turn;
        pipeline.hasActiveTurn = true;

        ToolExecutionEvent started;
        started.toolName = QStringLiteral("delegate_task");
        started.toolId = delegateToolId;
        started.status = QStringLiteral("started");
        started.success = true;
        started.data.insert(QStringLiteral("task"), QStringLiteral("检查最近的实现差异"));
        started.data.insert(QStringLiteral("role_prompt"), QStringLiteral("你是测试子代理"));
        started.data.insert(QStringLiteral("_agent_id"), agentId);

        fixture.chatService.onRuntimeToolEvent(session->id(), started);

        ToolExecutionEvent completed;
        completed.toolName = QStringLiteral("delegate_task");
        completed.toolId = delegateToolId;
        completed.status = QStringLiteral("completed");
        completed.success = true;
        completed.formattedResult = QStringLiteral("子代理任务已受理");
        completed.rawResult = QStringLiteral("accepted");
        completed.data.insert(QStringLiteral("status"), QStringLiteral("accepted"));
        completed.data.insert(QStringLiteral("job_id"), waitingJobId);
        completed.data.insert(QStringLiteral("child_request_id"), childRequestId);
        completed.data.insert(QStringLiteral("child_trace_id"), childTraceId);
        completed.data.insert(QStringLiteral("child_agent_id"), childAgentId);
        completed.data.insert(QStringLiteral("child_model"), QStringLiteral("mock-child-model"));
        completed.data.insert(QStringLiteral("child_duration_ms"), 1234);
        completed.data.insert(QStringLiteral("child_tool_started_count"), 2);
        completed.data.insert(QStringLiteral("child_tool_completed_count"), 2);

        fixture.chatService.onRuntimeToolEvent(session->id(), completed);

        const QList<QJsonObject> startedEvents = fixture.eventsForSession(session->id(), QStringLiteral("delegate.tool_started"));
        const QList<QJsonObject> completedEvents = fixture.eventsForSession(session->id(), QStringLiteral("delegate.tool_completed"));
        if (startedEvents.isEmpty())
            return fail(QStringLiteral("收到 delegate.tool_started"), QStringLiteral("未收到"));
        if (completedEvents.isEmpty())
            return fail(QStringLiteral("收到 delegate.tool_completed"), QStringLiteral("未收到"));

        const QJsonObject startedEvent = startedEvents.last();
        const QJsonObject completedEvent = completedEvents.last();
        if (startedEvent.value(QStringLiteral("trace_id")).toString() != turn.requestTraceId)
            return fail(turn.requestTraceId, startedEvent.value(QStringLiteral("trace_id")).toString());
        if (startedEvent.value(QStringLiteral("turn_id")).toString() != turn.turnId)
            return fail(turn.turnId, startedEvent.value(QStringLiteral("turn_id")).toString());
        if (startedEvent.value(QStringLiteral("run_id")).toString() != turn.runId)
            return fail(turn.runId, startedEvent.value(QStringLiteral("run_id")).toString());

        if (completedEvent.value(QStringLiteral("trace_id")).toString() != turn.requestTraceId)
            return fail(turn.requestTraceId, completedEvent.value(QStringLiteral("trace_id")).toString());
        if (completedEvent.value(QStringLiteral("turn_id")).toString() != turn.turnId)
            return fail(turn.turnId, completedEvent.value(QStringLiteral("turn_id")).toString());
        if (completedEvent.value(QStringLiteral("run_id")).toString() != turn.runId)
            return fail(turn.runId, completedEvent.value(QStringLiteral("run_id")).toString());
        if (completedEvent.value(QStringLiteral("child_request_id")).toString() != childRequestId)
            return fail(QStringLiteral("child-req-1"), completedEvent.value(QStringLiteral("child_request_id")).toString());
        if (completedEvent.value(QStringLiteral("child_trace_id")).toString() != childTraceId)
            return fail(QStringLiteral("child-trace-1"), completedEvent.value(QStringLiteral("child_trace_id")).toString());
        if (completedEvent.value(QStringLiteral("child_agent_id")).toString() != childAgentId)
            return fail(QStringLiteral("child-agent-1"), completedEvent.value(QStringLiteral("child_agent_id")).toString());

        if (session->messageCount() < 2)
            return fail(QStringLiteral("至少 2 条工具消息"), QString::number(session->messageCount()));

        const Message toolCallMessage = session->messageAt(session->messageCount() - 2);
        const Message toolResultMessage = session->messageAt(session->messageCount() - 1);
        if (toolCallMessage.content.type != MessageContent::Type::ToolCall)
            return fail(QStringLiteral("ToolCall"), QString::number(static_cast<int>(toolCallMessage.content.type)));
        if (toolResultMessage.content.type != MessageContent::Type::ToolResult)
            return fail(QStringLiteral("ToolResult"), QString::number(static_cast<int>(toolResultMessage.content.type)));
        if (toolCallMessage.traceId != turn.requestTraceId)
            return fail(turn.requestTraceId, toolCallMessage.traceId);
        if (toolCallMessage.turnId != turn.turnId)
            return fail(turn.turnId, toolCallMessage.turnId);
        if (toolResultMessage.traceId != turn.requestTraceId)
            return fail(turn.requestTraceId, toolResultMessage.traceId);
        if (toolResultMessage.turnId != turn.turnId)
            return fail(turn.turnId, toolResultMessage.turnId);
        if (toolCallMessage.content.payload.value(QStringLiteral("tool_call_id")).toString() != delegateToolId)
            return fail(delegateToolId, toolCallMessage.content.payload.value(QStringLiteral("tool_call_id")).toString());
        if (toolResultMessage.content.payload.value(QStringLiteral("child_request_id")).toString() != childRequestId)
            return fail(childRequestId, toolResultMessage.content.payload.value(QStringLiteral("child_request_id")).toString());
        if (toolResultMessage.content.payload.value(QStringLiteral("child_trace_id")).toString() != childTraceId)
            return fail(childTraceId, toolResultMessage.content.payload.value(QStringLiteral("child_trace_id")).toString());
        if (toolResultMessage.content.payload.value(QStringLiteral("child_agent_id")).toString() != childAgentId)
            return fail(childAgentId, toolResultMessage.content.payload.value(QStringLiteral("child_agent_id")).toString());

        const QJsonObject taskState = fixture.chatService.taskStateForSession(session->id());
        if (taskState.value(QStringLiteral("state")).toString() != QStringLiteral("blocked"))
            return fail(QStringLiteral("blocked"), taskState.value(QStringLiteral("state")).toString());
        if (taskState.value(QStringLiteral("trace_id")).toString() != turn.requestTraceId)
            return fail(turn.requestTraceId, taskState.value(QStringLiteral("trace_id")).toString());
        if (taskState.value(QStringLiteral("turn_id")).toString() != turn.turnId)
            return fail(turn.turnId, taskState.value(QStringLiteral("turn_id")).toString());
        if (taskState.value(QStringLiteral("run_id")).toString() != turn.runId)
            return fail(turn.runId, taskState.value(QStringLiteral("run_id")).toString());
        if (taskState.value(QStringLiteral("waiting_job_id")).toString() != waitingJobId)
            return fail(waitingJobId, taskState.value(QStringLiteral("waiting_job_id")).toString());

        ChatPersistenceService queryPersistence;

        LogQueryEngine::Query eventQuery;
        eventQuery.dataRootPath = queryPersistence.dataRootPath();
        eventQuery.source = QStringLiteral("events");
        eventQuery.sessionId = session->id();
        eventQuery.turnId = turn.turnId;
        eventQuery.runId = turn.runId;
        eventQuery.requestId = childRequestId;
        eventQuery.limit = 20;

        LogQueryEngine::Result eventResult;
        if (!waitForCondition(500, [&]() {
                eventResult = LogQueryEngine::execute(eventQuery);
                return !eventResult.hits.isEmpty();
            })) {
            return fail(QStringLiteral("日志查询能命中 delegate 事件"), QStringLiteral("events 命中为空"));
        }

        bool foundCompletedEvent = false;
        for (const LogQueryEngine::Hit& hit : eventResult.hits) {
            if (hit.eventType == QStringLiteral("delegate.tool_completed")
                && hit.traceId == parentTraceId
                && hit.turnId == parentTurnId
                && hit.runId == parentRunId
                && hit.requestId == childRequestId) {
                foundCompletedEvent = true;
                break;
            }
        }
        if (!foundCompletedEvent)
            return fail(QStringLiteral("命中 delegate.tool_completed + 父/子追踪字段"), QStringLiteral("未找到匹配 event hit"));

        LogQueryEngine::Query messageQuery;
        messageQuery.dataRootPath = queryPersistence.dataRootPath();
        messageQuery.source = QStringLiteral("messages");
        messageQuery.sessionId = session->id();
        messageQuery.turnId = turn.turnId;
        messageQuery.toolCallId = delegateToolId;
        messageQuery.limit = 20;

        LogQueryEngine::Result messageResult;
        if (!waitForCondition(500, [&]() {
                messageResult = LogQueryEngine::execute(messageQuery);
                return messageResult.hits.size() >= 2;
            })) {
            return fail(QStringLiteral("日志查询能命中 delegate tool call/result 消息"), QStringLiteral("messages 命中不足"));
        }

        bool foundToolCall = false;
        bool foundToolResult = false;
        for (const LogQueryEngine::Hit& hit : messageResult.hits) {
            if (hit.traceId != parentTraceId || hit.turnId != parentTurnId)
                continue;
            if (hit.toolCallId != delegateToolId)
                continue;
            if (hit.source == QStringLiteral("message") && hit.eventType == QStringLiteral("tool_call"))
                foundToolCall = true;
            if (hit.source == QStringLiteral("message")
                && hit.eventType == QStringLiteral("tool_result")
                && hit.requestId == childRequestId) {
                foundToolResult = true;
            }
        }
        if (!foundToolCall)
            return fail(QStringLiteral("命中 delegate tool_call message"), QStringLiteral("未找到 tool_call hit"));
        if (!foundToolResult)
            return fail(QStringLiteral("命中带 child_request_id 的 tool_result message"), QStringLiteral("未找到 tool_result hit"));
        return 0;
    } END_TEST

    PRINT_DIVIDER();
    qDebug().noquote() << QString("结果: %1/%2 通过").arg(g_passCount).arg(g_testCount);
    qDebug().noquote() << "════════════════════════════════════════";
    return g_passCount == g_testCount ? 0 : 1;
}

