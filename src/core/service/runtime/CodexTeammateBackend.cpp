#include "CodexTeammateBackend.h"
#include "CodexAppServerClient.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>

CodexTeammateBackend::CodexTeammateBackend(QObject* parent)
    : QObject(parent)
{
}

CodexTeammateBackend::~CodexTeammateBackend()
{
    shutdown();
}

bool CodexTeammateBackend::ensureReady(QString* error)
{
    if (m_server && m_server->isRunning() && m_serverReady)
        return true;

    if (!m_server) {
        m_server = new CodexAppServerClient(this);
        auto launch = CodexAppServerClient::defaultLaunchOptions();
        launch.clientName = QStringLiteral("tmagent-teammate-host");
        launch.clientTitle = QStringLiteral("TmAgent Teammate Host");
        launch.optOutNotificationMethods.clear();
        m_server->setLaunchOptions(launch);
        connectServerSignals();
    }

    m_serverReady = false;
    m_server->start();

    // 同步等待 ready（使用 processEvents 轮询）
    bool transportFailed = false;
    auto conn = connect(m_server, &CodexAppServerClient::transportError,
        [&](const QString&) { transportFailed = true; });

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 10000;
    while (!m_serverReady && !transportFailed && QDateTime::currentMSecsSinceEpoch() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
    disconnect(conn);

    if (!m_serverReady) {
        if (error)
            *error = QStringLiteral("Codex app-server 启动超时");
        return false;
    }
    return true;
}

bool CodexTeammateBackend::isReady() const
{
    return m_server && m_server->isRunning() && m_serverReady;
}

void CodexTeammateBackend::shutdown()
{
    if (!m_server)
        return;
    m_server->shutdown();
    m_serverReady = false;
}

void CodexTeammateBackend::connectServerSignals()
{
    if (!m_server)
        return;

    connect(m_server, &CodexAppServerClient::started,
            this, &CodexTeammateBackend::onServerStarted);
    connect(m_server, &CodexAppServerClient::stopped,
            this, [this](int exitCode, QProcess::ExitStatus) { onServerStopped(exitCode); });
    connect(m_server, &CodexAppServerClient::responseReceived,
            this, &CodexTeammateBackend::onResponseReceived);
    connect(m_server, &CodexAppServerClient::responseErrorReceived,
            this, [this](const QString& reqId, int code, const QString& msg, const QJsonObject&) {
        onResponseError(reqId, code, msg);
    });
    connect(m_server, &CodexAppServerClient::transportError,
            this, &CodexTeammateBackend::onTransportError);
    connect(m_server, &CodexAppServerClient::turnCompleted,
            this, &CodexTeammateBackend::onTurnCompleted);
    connect(m_server, &CodexAppServerClient::assistantMessageDelta,
            this, &CodexTeammateBackend::onAssistantMessageDelta);
    connect(m_server, &CodexAppServerClient::assistantMessageCompleted,
            this, &CodexTeammateBackend::onAssistantMessageCompleted);
    connect(m_server, &CodexAppServerClient::commandExecutionApprovalRequested,
            this, &CodexTeammateBackend::onCommandApproval);
    connect(m_server, &CodexAppServerClient::fileChangeApprovalRequested,
            this, &CodexTeammateBackend::onFileChangeApproval);
}

void CodexTeammateBackend::onServerStarted()
{
    m_initializeRequestId = m_server->requestInitialize();
}

void CodexTeammateBackend::onServerStopped(int /*exitCode*/)
{
    m_serverReady = false;
}

// ── 请求路由 ──

void CodexTeammateBackend::onResponseReceived(const QString& requestId, const QJsonValue& result)
{
    if (requestId == m_initializeRequestId) {
        m_server->completeInitializeHandshake();
        m_serverReady = true;
        return;
    }

    if (!m_pendingRequests.contains(requestId))
        return;

    const PendingRequest pending = m_pendingRequests.take(requestId);
    Teammate* mate = pending.mate;
    if (!mate)
        return;

    if (pending.type == PendingRequest::ThreadStart) {
        const QJsonObject obj = result.toObject();
        const QJsonObject thread = obj.value(QStringLiteral("thread")).toObject();
        const QString threadId = thread.value(QStringLiteral("id")).toString().trimmed();
        if (!threadId.isEmpty()) {
            mate->setThreadId(threadId);
            m_threadToMate.insert(threadId, mate);
        }
    } else if (pending.type == PendingRequest::TurnStart) {
        mate->touchLastActive();
    }
}

void CodexTeammateBackend::onResponseError(const QString& requestId, int /*code*/, const QString& message)
{
    if (!m_pendingRequests.contains(requestId))
        return;

    const PendingRequest pending = m_pendingRequests.take(requestId);
    Teammate* mate = pending.mate;
    if (!mate)
        return;

    mate->setStatus(Teammate::Status::Error);
    mate->setLastError(message);

    if (pending.type == PendingRequest::TurnStart)
        emit mate->turnCompleted(QString(), false, message);
}

void CodexTeammateBackend::onTransportError(const QString& message)
{
    for (auto it = m_threadToMate.begin(); it != m_threadToMate.end(); ++it) {
        Teammate* mate = it.value();
        if (mate && mate->status() == Teammate::Status::Busy) {
            mate->setStatus(Teammate::Status::Error);
            mate->setLastError(message);
            emit mate->turnCompleted(QString(), false, message);
        }
    }
}

// ── Turn 生命周期 ──

void CodexTeammateBackend::onTurnCompleted(const QString& threadId, const QString& turnId,
                                            const QString& status, const QJsonObject& error)
{
    Teammate* mate = findByThreadId(threadId);
    if (!mate)
        return;

    const QString accumulated = m_accumulatedText.take(threadId);
    const bool success = (status == QLatin1String("completed"));

    if (success) {
        mate->setStatus(Teammate::Status::Idle);
        mate->setLastError(QString());
    } else {
        mate->setStatus(Teammate::Status::Error);
        mate->setLastError(error.value(QStringLiteral("message")).toString());
    }
    mate->incrementTurnCount();
    mate->touchLastActive();

    emit mate->turnCompleted(turnId, success, accumulated);
}

void CodexTeammateBackend::onAssistantMessageDelta(const QString& threadId, const QString& turnId,
                                                    const QString& /*itemId*/, const QString& delta)
{
    m_accumulatedText[threadId].append(delta);

    Teammate* mate = findByThreadId(threadId);
    if (mate)
        emit mate->messageDelta(turnId, delta);
}

void CodexTeammateBackend::onAssistantMessageCompleted(const QString& threadId, const QString& /*turnId*/,
                                                        const QString& /*itemId*/, const QString& text)
{
    if (!text.isEmpty())
        m_accumulatedText[threadId] = text;
}

// ── 审批自动通过 ──

void CodexTeammateBackend::onCommandApproval(const QString& requestId, const QString& /*threadId*/,
                                              const QString& /*turnId*/, const QString& /*itemId*/,
                                              const QString& /*command*/, const QString& /*cwd*/,
                                              const QString& /*reason*/, const QStringList& /*decisions*/)
{
    if (m_server)
        m_server->sendServerRequestResult(requestId,
            QJsonObject{{QStringLiteral("decision"), QStringLiteral("acceptForSession")}});
}

void CodexTeammateBackend::onFileChangeApproval(const QString& requestId, const QString& /*threadId*/,
                                                 const QString& /*turnId*/, const QString& /*itemId*/,
                                                 const QString& /*reason*/, const QString& /*grantRoot*/)
{
    if (m_server)
        m_server->sendServerRequestResult(requestId,
            QJsonObject{{QStringLiteral("decision"), QStringLiteral("acceptForSession")}});
}

// ── ITeammateBackend 实现 ──

ITeammateBackend::CreateResult CodexTeammateBackend::createSession(Teammate* mate)
{
    CreateResult out;

    if (!m_serverReady) {
        out.error = QStringLiteral("Codex app-server 未就绪");
        return out;
    }

    QJsonObject overrides;
    if (!mate->role().isEmpty())
        overrides.insert(QStringLiteral("developerInstructions"), mate->role());
    if (!mate->workingDirectory().isEmpty())
        overrides.insert(QStringLiteral("cwd"), QDir::cleanPath(mate->workingDirectory()));
    // 合并后端特有参数
    const QJsonObject& extra = mate->m_backendOverrides;
    for (auto it = extra.begin(); it != extra.end(); ++it)
        overrides.insert(it.key(), it.value());

    const QString requestId = m_server->requestThreadStart(overrides);

    PendingRequest pending;
    pending.mate = mate;
    pending.type = PendingRequest::ThreadStart;
    m_pendingRequests.insert(requestId, pending);

    // 同步等待 thread/start 响应（使用 processEvents 轮询）
    bool gotResponse = false;
    bool gotError = false;

    auto conn1 = connect(m_server, &CodexAppServerClient::responseReceived,
        [&](const QString& respId, const QJsonValue&) {
            if (respId == requestId)
                gotResponse = true;
        });
    auto conn2 = connect(m_server, &CodexAppServerClient::responseErrorReceived,
        [&](const QString& respId, int, const QString&, const QJsonObject&) {
            if (respId == requestId)
                gotError = true;
        });
    auto conn3 = connect(m_server, &CodexAppServerClient::transportError,
        [&](const QString&) { gotError = true; });

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 15000;
    while (!gotResponse && !gotError && QDateTime::currentMSecsSinceEpoch() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
    disconnect(conn1);
    disconnect(conn2);
    disconnect(conn3);

    if (mate->threadId().isEmpty()) {
        out.error = gotResponse ? QStringLiteral("thread/start 返回无效 threadId")
                                : QStringLiteral("等待 thread/start 响应超时");
        return out;
    }

    out.success = true;
    out.threadId = mate->threadId();
    return out;
}

ITeammateBackend::SendResult CodexTeammateBackend::sendMessage(Teammate* mate, const QString& text)
{
    SendResult out;

    if (!m_serverReady) {
        out.error = QStringLiteral("Codex app-server 未就绪");
        return out;
    }

    if (mate->threadId().isEmpty()) {
        out.error = QStringLiteral("队友尚未分配 Thread");
        return out;
    }

    m_accumulatedText[mate->threadId()].clear();

    QJsonObject overrides;
    if (!mate->workingDirectory().isEmpty())
        overrides.insert(QStringLiteral("cwd"), QDir::cleanPath(mate->workingDirectory()));

    const QString requestId = m_server->requestTurnStartText(mate->threadId(), text, overrides);

    PendingRequest pending;
    pending.mate = mate;
    pending.type = PendingRequest::TurnStart;
    m_pendingRequests.insert(requestId, pending);

    out.success = true;
    out.turnId = requestId;
    return out;
}

void CodexTeammateBackend::destroySession(Teammate* mate)
{
    if (!mate)
        return;
    const QString threadId = mate->threadId();
    if (!threadId.isEmpty()) {
        m_threadToMate.remove(threadId);
        m_accumulatedText.remove(threadId);
    }
}

Teammate* CodexTeammateBackend::findByThreadId(const QString& threadId) const
{
    return m_threadToMate.value(threadId);
}
