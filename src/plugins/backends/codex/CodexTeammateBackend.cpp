#include "CodexTeammateBackend.h"
#include "CodexAppServerClient.h"
#include "core/model/TeammateRuntimeAccess.h"

#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

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

    // 使用 QEventLoop 等待 ready，避免 processEvents 轮询和重入风险
    QEventLoop loop;
    bool transportFailed = false;
    auto conn1 = connect(m_server, &CodexAppServerClient::readyChanged, &loop,
        [&](bool ready) { if (ready) loop.quit(); });
    auto conn2 = connect(m_server, &CodexAppServerClient::transportError, &loop,
        [&](const QString&) { transportFailed = true; loop.quit(); });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(conn1);
    disconnect(conn2);

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

    // ── error 通知：非重试错误视为 turn 失败 ──
    connect(m_server, &CodexAppServerClient::errorNotification, this,
        [this](const QString& threadId, const QString& /*turnId*/,
               const QString& errorMessage, const QString& /*errorCode*/, bool willRetry) {
            if (willRetry) {
                startTurnTimeout(threadId, kDefaultTurnTimeoutMs); // 重试中，重置超时
                return;
            }
            cancelTurnTimeout(threadId);
            Teammate* mate = findByThreadId(threadId);
            if (!mate || mate->status() != Teammate::Status::Busy)
                return;
            const QString accumulated = m_accumulatedText.take(threadId);
            TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Error);
            TeammateRuntimeAccess::setLastError(mate, errorMessage);
            TeammateRuntimeAccess::setActiveTurnId(mate, QString());
            TeammateRuntimeAccess::incrementTurnCount(mate);
            TeammateRuntimeAccess::touchLastActive(mate);
            emit mate->turnCompleted(QString(), false,
                accumulated.isEmpty() ? QStringLiteral("错误: %1").arg(errorMessage) : accumulated);
        });

    // ── 动态工具调用请求：返回不支持 ──
    connect(m_server, &CodexAppServerClient::dynamicToolCallRequested, this,
        [this](const QString& requestId, const QString& threadId, const QString& /*turnId*/,
               const QString& /*callId*/, const QString& toolName, const QJsonObject& /*arguments*/) {
            startTurnTimeout(threadId, kDefaultTurnTimeoutMs); // 有活动，重置超时
            if (m_server) {
                QJsonArray contentItems;
                QJsonObject textItem;
                textItem.insert(QStringLiteral("type"), QStringLiteral("text"));
                textItem.insert(QStringLiteral("text"),
                    QStringLiteral("工具 \"%1\" 在当前环境中不可用").arg(toolName));
                contentItems.append(textItem);
                QJsonObject result;
                result.insert(QStringLiteral("contentItems"), contentItems);
                result.insert(QStringLiteral("success"), false);
                m_server->sendServerRequestResult(requestId, result);
            }
        });

    // ── 活动信号：重置超时（统一转发第一个参数 threadId）──
    auto resetTimeout = [this](const QString& threadId) {
        startTurnTimeout(threadId, kDefaultTurnTimeoutMs);
    };
    connect(m_server, &CodexAppServerClient::commandExecutionOutputDelta, this,
        [resetTimeout](const QString& threadId, const QString&, const QString&, const QString&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::planDelta, this,
        [resetTimeout](const QString& threadId, const QString&, const QString&, const QString&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::fileChangeOutputDelta, this,
        [resetTimeout](const QString& threadId, const QString&, const QString&, const QString&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::reasoningTextDelta, this,
        [resetTimeout](const QString& threadId, const QString&, const QString&, const QString&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::reasoningSummaryTextDelta, this,
        [resetTimeout](const QString& threadId, const QString&, const QString&, const QString&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::itemStarted, this,
        [resetTimeout](const QString& threadId, const QString&, const QJsonObject&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::itemCompleted, this,
        [resetTimeout](const QString& threadId, const QString&, const QJsonObject&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::turnDiffUpdated, this,
        [resetTimeout](const QString& threadId, const QString&, const QString&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::turnPlanUpdated, this,
        [resetTimeout](const QString& threadId, const QString&, const QString&, const QJsonArray&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::guardianApprovalReviewStarted, this,
        [resetTimeout](const QString& threadId, const QString&, const QString&, const QJsonObject&) { resetTimeout(threadId); });
    connect(m_server, &CodexAppServerClient::guardianApprovalReviewCompleted, this,
        [resetTimeout](const QString& threadId, const QString&, const QString&, const QJsonObject&) { resetTimeout(threadId); });
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
            TeammateRuntimeAccess::setThreadId(mate, threadId);
            m_threadToMate.insert(threadId, mate);
        }
    } else if (pending.type == PendingRequest::TurnStart) {
        const QJsonObject obj = result.toObject();
        const QJsonObject turn = obj.value(QStringLiteral("turn")).toObject();
        const QString turnId = turn.value(QStringLiteral("id")).toString().trimmed();
        if (!turnId.isEmpty())
            TeammateRuntimeAccess::setActiveTurnId(mate, turnId);
        TeammateRuntimeAccess::touchLastActive(mate);
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

    TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Error);
    TeammateRuntimeAccess::setLastError(mate, message);

    if (pending.type == PendingRequest::TurnStart)
        emit mate->turnCompleted(QString(), false, message);
}

void CodexTeammateBackend::onTransportError(const QString& message)
{
    for (auto it = m_threadToMate.begin(); it != m_threadToMate.end(); ++it) {
        Teammate* mate = it.value();
        if (mate && mate->status() == Teammate::Status::Busy) {
            cancelTurnTimeout(it.key());
            m_accumulatedText.remove(it.key());
            TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Error);
            TeammateRuntimeAccess::setLastError(mate, message);
            emit mate->turnCompleted(QString(), false, message);
        }
    }
}

// ── Turn 生命周期 ──

void CodexTeammateBackend::onTurnCompleted(const QString& threadId, const QString& turnId,
                                            const QString& status, const QJsonObject& error)
{
    cancelTurnTimeout(threadId);

    Teammate* mate = findByThreadId(threadId);
    if (!mate)
        return;

    const QString accumulated = m_accumulatedText.take(threadId);
    const bool success = (status == QLatin1String("completed"));

    if (success) {
        TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Idle);
        TeammateRuntimeAccess::setLastError(mate, QString());
    } else {
        TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Error);
        TeammateRuntimeAccess::setLastError(mate, error.value(QStringLiteral("message")).toString());
    }
    TeammateRuntimeAccess::setActiveTurnId(mate, QString());
    TeammateRuntimeAccess::incrementTurnCount(mate);
    TeammateRuntimeAccess::touchLastActive(mate);

    emit mate->turnCompleted(turnId, success, accumulated);
}

void CodexTeammateBackend::onAssistantMessageDelta(const QString& threadId, const QString& turnId,
                                                    const QString& /*itemId*/, const QString& delta)
{
    m_accumulatedText[threadId].append(delta);
    startTurnTimeout(threadId, kDefaultTurnTimeoutMs); // 有活动，重置超时

    Teammate* mate = findByThreadId(threadId);
    if (mate)
        emit mate->messageDelta(turnId, delta);
}

void CodexTeammateBackend::onAssistantMessageCompleted(const QString& threadId, const QString& /*turnId*/,
                                                        const QString& /*itemId*/, const QString& text)
{
    if (!text.isEmpty())
        m_accumulatedText[threadId] = text;
    startTurnTimeout(threadId, kDefaultTurnTimeoutMs); // 有活动，重置超时
}

// ── 审批自动通过 ──

void CodexTeammateBackend::onCommandApproval(const QString& requestId, const QString& threadId,
                                              const QString& /*turnId*/, const QString& /*itemId*/,
                                              const QString& /*command*/, const QString& /*cwd*/,
                                              const QString& /*reason*/, const QStringList& /*decisions*/)
{
    startTurnTimeout(threadId, kDefaultTurnTimeoutMs); // 有活动，重置超时
    if (m_server)
        m_server->sendServerRequestResult(requestId,
            QJsonObject{{QStringLiteral("decision"), QStringLiteral("acceptForSession")}});
}

void CodexTeammateBackend::onFileChangeApproval(const QString& requestId, const QString& threadId,
                                                 const QString& /*turnId*/, const QString& /*itemId*/,
                                                 const QString& /*reason*/, const QString& /*grantRoot*/)
{
    startTurnTimeout(threadId, kDefaultTurnTimeoutMs); // 有活动，重置超时
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
    // 队友默认使用 workspace-write 沙箱 + 自动审批，允许在 cwd 下自由读写
    if (!overrides.contains(QStringLiteral("sandbox")))
        overrides.insert(QStringLiteral("sandbox"), QStringLiteral("workspace-write"));
    if (!overrides.contains(QStringLiteral("approvalPolicy")))
        overrides.insert(QStringLiteral("approvalPolicy"), QStringLiteral("never"));
    // 合并后端特有参数
    const QJsonObject extra = mate->backendOverrides();
    for (auto it = extra.begin(); it != extra.end(); ++it)
        overrides.insert(it.key(), it.value());

    const QString requestId = m_server->requestThreadStart(overrides);

    PendingRequest pending;
    pending.mate = mate;
    pending.type = PendingRequest::ThreadStart;
    m_pendingRequests.insert(requestId, pending);

    // 使用 QEventLoop 等待 thread/start 响应，避免 processEvents 轮询
    QEventLoop loop;
    bool gotResponse = false;
    bool gotError = false;

    auto conn1 = connect(m_server, &CodexAppServerClient::responseReceived, &loop,
        [&](const QString& respId, const QJsonValue&) {
            if (respId == requestId) { gotResponse = true; loop.quit(); }
        });
    auto conn2 = connect(m_server, &CodexAppServerClient::responseErrorReceived, &loop,
        [&](const QString& respId, int, const QString&, const QJsonObject&) {
            if (respId == requestId) { gotError = true; loop.quit(); }
        });
    auto conn3 = connect(m_server, &CodexAppServerClient::transportError, &loop,
        [&](const QString&) { gotError = true; loop.quit(); });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
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
    m_accumulatedText[mate->threadId()].reserve(4096);

    QJsonObject overrides;
    if (!mate->workingDirectory().isEmpty())
        overrides.insert(QStringLiteral("cwd"), QDir::cleanPath(mate->workingDirectory()));

    const QString requestId = m_server->requestTurnStartText(mate->threadId(), text, overrides);

    PendingRequest pending;
    pending.mate = mate;
    pending.type = PendingRequest::TurnStart;
    m_pendingRequests.insert(requestId, pending);

    // 启动 turn 超时定时器
    const int timeoutMs = mate->turnIdleTimeoutMs() > 0 ? mate->turnIdleTimeoutMs() : kDefaultTurnTimeoutMs;
    startTurnTimeout(mate->threadId(), timeoutMs);

    out.success = true;
    out.turnId = requestId;
    return out;
}

bool CodexTeammateBackend::cancelTurn(Teammate* mate, QString* error)
{
    if (error)
        error->clear();
    if (!mate) {
        if (error)
            *error = QStringLiteral("队友为空");
        return false;
    }
    if (!m_serverReady || !m_server) {
        if (error)
            *error = QStringLiteral("Codex app-server 未就绪");
        return false;
    }
    if (mate->threadId().trimmed().isEmpty() || mate->activeTurnId().trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("队友当前没有运行中的任务");
        return false;
    }

    const QString requestId =
        m_server->requestTurnInterrupt(mate->threadId().trimmed(), mate->activeTurnId().trimmed());
    if (requestId.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("turn/interrupt 请求发送失败");
        return false;
    }
    return true;
}

void CodexTeammateBackend::destroySession(Teammate* mate)
{
    if (!mate)
        return;
    const QString threadId = mate->threadId();
    if (!threadId.isEmpty()) {
        cancelTurnTimeout(threadId);
        m_threadToMate.remove(threadId);
        m_accumulatedText.remove(threadId);
    }
    TeammateRuntimeAccess::setActiveTurnId(mate, QString());
}

Teammate* CodexTeammateBackend::findByThreadId(const QString& threadId) const
{
    return m_threadToMate.value(threadId);
}

void CodexTeammateBackend::startTurnTimeout(const QString& threadId, int timeoutMs)
{
    QTimer* timer = m_turnTimeoutTimers.value(threadId);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, threadId]() {
            m_turnTimeoutTimers.remove(threadId);

            Teammate* mate = findByThreadId(threadId);
            if (!mate || mate->status() != Teammate::Status::Busy)
                return;

            qWarning() << "[CodexTeammateBackend] turn 超时，队友" << mate->name()
                       << "threadId:" << threadId;

            const QString accumulated = m_accumulatedText.take(threadId);
            TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Error);
            TeammateRuntimeAccess::setLastError(mate, QStringLiteral("turn 超时（未收到 turnCompleted）"));
            TeammateRuntimeAccess::setActiveTurnId(mate, QString());
            TeammateRuntimeAccess::incrementTurnCount(mate);
            TeammateRuntimeAccess::touchLastActive(mate);

            emit mate->turnCompleted(QString(), false,
                accumulated.isEmpty()
                    ? QStringLiteral("错误: turn 超时，队友未在规定时间内完成响应")
                    : accumulated);
        });
        m_turnTimeoutTimers.insert(threadId, timer);
    }
    timer->start(timeoutMs);
}

void CodexTeammateBackend::cancelTurnTimeout(const QString& threadId)
{
    QTimer* timer = m_turnTimeoutTimers.take(threadId);
    if (timer) {
        timer->stop();
        timer->deleteLater();
    }
}
