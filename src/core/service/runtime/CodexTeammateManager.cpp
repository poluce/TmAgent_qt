#include "CodexTeammateManager.h"
#include "CodexAppServerClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>

CodexTeammateManager* CodexTeammateManager::instance()
{
    static CodexTeammateManager mgr(nullptr);
    return &mgr;
}

CodexTeammateManager::CodexTeammateManager(QObject* parent)
    : QObject(parent)
{
}

// ── 进程管理 ──

void CodexTeammateManager::ensureServerRunning()
{
    if (m_server && m_server->isRunning())
        return;

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
}

bool CodexTeammateManager::isServerRunning() const
{
    return m_server && m_server->isRunning() && m_serverReady;
}

void CodexTeammateManager::shutdownServer()
{
    if (!m_server)
        return;

    for (auto* mate : m_teammates)
        mate->setStatus(CodexTeammate::Status::Shutdown);

    m_server->shutdown();
    m_serverReady = false;
    emit serverStatusChanged(false);
}

void CodexTeammateManager::connectServerSignals()
{
    if (!m_server)
        return;

    connect(m_server, &CodexAppServerClient::started, this, &CodexTeammateManager::onServerStarted);
    connect(m_server, &CodexAppServerClient::stopped, this, [this](int exitCode) {
        onServerStopped(exitCode);
    });
    connect(m_server, &CodexAppServerClient::responseReceived,
            this, &CodexTeammateManager::onResponseReceived);
    connect(m_server, &CodexAppServerClient::responseErrorReceived,
            this, [this](const QString& reqId, int code, const QString& msg, const QJsonObject&) {
        onResponseError(reqId, code, msg);
    });
    connect(m_server, &CodexAppServerClient::transportError,
            this, &CodexTeammateManager::onTransportError);
    connect(m_server, &CodexAppServerClient::turnCompleted,
            this, &CodexTeammateManager::onTurnCompleted);
    connect(m_server, &CodexAppServerClient::assistantMessageDelta,
            this, &CodexTeammateManager::onAssistantMessageDelta);
    connect(m_server, &CodexAppServerClient::assistantMessageCompleted,
            this, &CodexTeammateManager::onAssistantMessageCompleted);
    connect(m_server, &CodexAppServerClient::commandExecutionApprovalRequested,
            this, &CodexTeammateManager::onCommandApproval);
    connect(m_server, &CodexAppServerClient::fileChangeApprovalRequested,
            this, &CodexTeammateManager::onFileChangeApproval);
}

void CodexTeammateManager::onServerStarted()
{
    m_initializeRequestId = m_server->requestInitialize();
}

void CodexTeammateManager::onServerStopped(int /*exitCode*/)
{
    m_serverReady = false;
    for (auto* mate : m_teammates) {
        if (mate->status() != CodexTeammate::Status::Shutdown)
            mate->setStatus(CodexTeammate::Status::Error);
    }
    emit serverStatusChanged(false);
}

// ── 请求路由 ──

void CodexTeammateManager::onResponseReceived(const QString& requestId, const QJsonValue& result)
{
    // initialize 握手完成
    if (requestId == m_initializeRequestId) {
        m_server->completeInitializeHandshake();
        m_serverReady = true;
        emit serverStatusChanged(true);
        return;
    }

    if (!m_pendingRequests.contains(requestId))
        return;

    const PendingRequest pending = m_pendingRequests.take(requestId);
    CodexTeammate* mate = m_teammates.value(pending.teammateId);
    if (!mate)
        return;

    if (pending.type == PendingRequest::ThreadStart) {
        const QJsonObject obj = result.toObject();
        const QJsonObject thread = obj.value(QStringLiteral("thread")).toObject();
        const QString threadId = thread.value(QStringLiteral("id")).toString().trimmed();
        if (!threadId.isEmpty()) {
            mate->setThreadId(threadId);
            m_threadToTeammate.insert(threadId, mate->id());
        }
    } else if (pending.type == PendingRequest::TurnStart) {
        // turn/start 响应，turnId 通过 turnStarted 通知获取
        mate->touchLastActive();
    }
}

void CodexTeammateManager::onResponseError(const QString& requestId, int /*code*/, const QString& message)
{
    if (!m_pendingRequests.contains(requestId))
        return;

    const PendingRequest pending = m_pendingRequests.take(requestId);
    CodexTeammate* mate = m_teammates.value(pending.teammateId);
    if (!mate)
        return;

    mate->setStatus(CodexTeammate::Status::Error);
    mate->setLastError(message);

    if (pending.type == PendingRequest::TurnStart)
        emit mate->turnCompleted(QString(), false, message);
}

void CodexTeammateManager::onTransportError(const QString& message)
{
    // 传输层错误影响所有正在 Busy 的队友
    for (auto* mate : m_teammates) {
        if (mate->status() == CodexTeammate::Status::Busy) {
            mate->setStatus(CodexTeammate::Status::Error);
            mate->setLastError(message);
            emit mate->turnCompleted(QString(), false, message);
        }
    }
}

// ── Turn 生命周期 ──

void CodexTeammateManager::onTurnCompleted(const QString& threadId, const QString& turnId,
                                            const QString& status, const QJsonObject& error)
{
    CodexTeammate* mate = findByThreadId(threadId);
    if (!mate)
        return;

    const QString accumulated = m_accumulatedText.take(threadId);
    const bool success = (status == QLatin1String("completed"));

    if (success) {
        mate->setStatus(CodexTeammate::Status::Idle);
        mate->setLastError(QString());
    } else {
        mate->setStatus(CodexTeammate::Status::Error);
        mate->setLastError(error.value(QStringLiteral("message")).toString());
    }
    mate->incrementTurnCount();
    mate->touchLastActive();

    emit mate->turnCompleted(turnId, success, accumulated);
}

void CodexTeammateManager::onAssistantMessageDelta(const QString& threadId, const QString& turnId,
                                                    const QString& /*itemId*/, const QString& delta)
{
    m_accumulatedText[threadId].append(delta);

    CodexTeammate* mate = findByThreadId(threadId);
    if (mate)
        emit mate->messageDelta(turnId, delta);
}

void CodexTeammateManager::onAssistantMessageCompleted(const QString& threadId, const QString& /*turnId*/,
                                                        const QString& /*itemId*/, const QString& text)
{
    if (!text.isEmpty())
        m_accumulatedText[threadId] = text;
}

// ── 审批自动通过 ──

void CodexTeammateManager::onCommandApproval(const QString& requestId, const QString& /*threadId*/,
                                              const QString& /*turnId*/, const QString& /*itemId*/,
                                              const QString& /*command*/, const QString& /*cwd*/,
                                              const QString& /*reason*/, const QStringList& /*decisions*/)
{
    if (m_server)
        m_server->sendServerRequestResult(requestId, QJsonObject{{QStringLiteral("decision"), QStringLiteral("acceptForSession")}});
}

void CodexTeammateManager::onFileChangeApproval(const QString& requestId, const QString& /*threadId*/,
                                                 const QString& /*turnId*/, const QString& /*itemId*/,
                                                 const QString& /*reason*/, const QString& /*grantRoot*/)
{
    if (m_server)
        m_server->sendServerRequestResult(requestId, QJsonObject{{QStringLiteral("decision"), QStringLiteral("acceptForSession")}});
}

// ── 队友生命周期 ──

CodexTeammateManager::CreateResult CodexTeammateManager::createTeammate(const CodexTeammate::Config& config)
{
    CreateResult out;

    if (config.name.trimmed().isEmpty()) {
        out.error = QStringLiteral("队友名称不能为空");
        return out;
    }

    if (findByName(config.name.trimmed())) {
        out.error = QStringLiteral("已存在同名队友: %1").arg(config.name);
        return out;
    }

    ensureServerRunning();

    // 等待 server ready（最多 10 秒）
    if (!m_serverReady) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(this, &CodexTeammateManager::serverStatusChanged, &loop, [&](bool ready) {
            if (ready)
                loop.quit();
        });
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(10000);
        loop.exec();

        if (!m_serverReady) {
            out.error = QStringLiteral("Codex app-server 启动超时");
            return out;
        }
    }

    const QString teammateId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto* mate = new CodexTeammate(teammateId, config, this);
    m_teammates.insert(teammateId, mate);

    // 发送 thread/start
    QJsonObject overrides;
    if (!config.role.isEmpty())
        overrides.insert(QStringLiteral("developerInstructions"), config.role);
    if (!config.workingDirectory.isEmpty())
        overrides.insert(QStringLiteral("cwd"), QDir::cleanPath(config.workingDirectory));
    for (auto it = config.threadOverrides.begin(); it != config.threadOverrides.end(); ++it)
        overrides.insert(it.key(), it.value());

    const QString requestId = m_server->requestThreadStart(overrides);

    PendingRequest pending;
    pending.teammateId = teammateId;
    pending.type = PendingRequest::ThreadStart;
    m_pendingRequests.insert(requestId, pending);

    // 同步等待 thread/start 响应（最多 15 秒）
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    bool gotResponse = false;

    auto conn1 = connect(this, &CodexTeammateManager::serverStatusChanged, &loop, [&](bool) {
        // server 挂了
        if (!m_serverReady)
            loop.quit();
    });
    auto conn2 = connect(m_server, &CodexAppServerClient::responseReceived, &loop,
        [&](const QString& respId, const QJsonValue&) {
            if (respId == requestId) {
                gotResponse = true;
                loop.quit();
            }
        });
    auto conn3 = connect(m_server, &CodexAppServerClient::responseErrorReceived, &loop,
        [&](const QString& respId, int, const QString&, const QJsonObject&) {
            if (respId == requestId)
                loop.quit();
        });
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(15000);
    loop.exec();
    disconnect(conn1);
    disconnect(conn2);
    disconnect(conn3);

    if (mate->threadId().isEmpty()) {
        // 创建失败，清理
        m_teammates.remove(teammateId);
        mate->deleteLater();
        out.error = gotResponse ? QStringLiteral("thread/start 返回无效 threadId")
                                : QStringLiteral("等待 thread/start 响应超时");
        return out;
    }

    out.success = true;
    out.teammateId = teammateId;
    out.threadId = mate->threadId();
    emit teammateCreated(teammateId);
    return out;
}

bool CodexTeammateManager::removeTeammate(const QString& teammateId, QString* error)
{
    CodexTeammate* mate = m_teammates.value(teammateId);
    if (!mate) {
        if (error)
            *error = QStringLiteral("队友不存在: %1").arg(teammateId);
        return false;
    }

    if (!mate->threadId().isEmpty())
        m_threadToTeammate.remove(mate->threadId());
    m_accumulatedText.remove(mate->threadId());
    m_teammates.remove(teammateId);

    mate->setStatus(CodexTeammate::Status::Shutdown);
    mate->deleteLater();

    emit teammateRemoved(teammateId);
    return true;
}

// ── 对话 ──

CodexTeammateManager::MessageResult CodexTeammateManager::sendMessage(const QString& teammateId,
                                                                       const QString& text)
{
    MessageResult out;

    CodexTeammate* mate = m_teammates.value(teammateId);
    if (!mate) {
        out.error = QStringLiteral("队友不存在: %1").arg(teammateId);
        return out;
    }

    if (mate->status() == CodexTeammate::Status::Busy) {
        out.error = QStringLiteral("队友 \"%1\" 正忙，请等待当前 Turn 完成").arg(mate->name());
        return out;
    }

    if (mate->status() == CodexTeammate::Status::Shutdown) {
        out.error = QStringLiteral("队友 \"%1\" 已关闭").arg(mate->name());
        return out;
    }

    if (!isServerRunning()) {
        out.error = QStringLiteral("Codex app-server 未运行");
        return out;
    }

    if (mate->threadId().isEmpty()) {
        out.error = QStringLiteral("队友 \"%1\" 尚未分配 Thread").arg(mate->name());
        return out;
    }

    mate->setStatus(CodexTeammate::Status::Busy);
    mate->setLastError(QString());
    mate->touchLastActive();
    m_accumulatedText[mate->threadId()].clear();

    const QString requestId = m_server->requestTurnStartText(mate->threadId(), text);

    PendingRequest pending;
    pending.teammateId = teammateId;
    pending.type = PendingRequest::TurnStart;
    m_pendingRequests.insert(requestId, pending);

    out.success = true;
    out.turnId = requestId;
    return out;
}

// ── 查询 ──

CodexTeammate* CodexTeammateManager::teammate(const QString& teammateId) const
{
    return m_teammates.value(teammateId);
}

CodexTeammate* CodexTeammateManager::findByName(const QString& name) const
{
    const QString trimmed = name.trimmed();
    for (auto* mate : m_teammates) {
        if (mate->name().trimmed().compare(trimmed, Qt::CaseInsensitive) == 0)
            return mate;
    }
    return nullptr;
}

QList<CodexTeammate*> CodexTeammateManager::allTeammates() const
{
    return m_teammates.values();
}

int CodexTeammateManager::teammateCount() const
{
    return m_teammates.size();
}

CodexTeammate* CodexTeammateManager::findByThreadId(const QString& threadId) const
{
    const QString teammateId = m_threadToTeammate.value(threadId);
    return teammateId.isEmpty() ? nullptr : m_teammates.value(teammateId);
}
