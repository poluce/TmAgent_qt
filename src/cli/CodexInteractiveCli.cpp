#include "CodexInteractiveCli.h"

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <cstdio>
#include <cstring>

extern QMutex g_consoleMutex;

namespace {

static void rawPrint(const char* text)
{
    QMutexLocker locker(&g_consoleMutex);
    std::fputs(text, stdout);
    std::fflush(stdout);
}

static void rawPrintLn(const char* text)
{
    QMutexLocker locker(&g_consoleMutex);
    std::fputs(text, stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

class StdinReaderThread : public QThread {
public:
    explicit StdinReaderThread(CodexInteractiveCli* cli, QObject* parent = nullptr)
        : QThread(parent)
        , m_cli(cli)
    {
    }

protected:
    void run() override
    {
        while (!isInterruptionRequested()) {
            char buf[4096];
            if (!std::fgets(buf, sizeof(buf), stdin)) {
                QMetaObject::invokeMethod(m_cli, "onStdinLine", Qt::QueuedConnection, Q_ARG(QString, QString()));
                return;
            }
            size_t len = std::strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
                buf[--len] = '\0';
            const QString line = QString::fromLocal8Bit(buf, static_cast<int>(len));
            QMetaObject::invokeMethod(m_cli, "onStdinLine", Qt::QueuedConnection, Q_ARG(QString, line));
        }
    }

private:
    CodexInteractiveCli* m_cli = nullptr;
};

QString firstNonEmpty(const QStringList& values)
{
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return QString();
}

} // namespace

CodexInteractiveCli::CodexInteractiveCli(const Options& opts, QObject* parent)
    : QObject(parent)
    , m_opts(opts)
{
}

void CodexInteractiveCli::run()
{
    m_client = new CodexAppServerClient(this);
    CodexAppServerClient::LaunchOptions launch = CodexAppServerClient::defaultLaunchOptions();
    if (!m_opts.codexBin.trimmed().isEmpty())
        launch.program = m_opts.codexBin.trimmed();
    launch.viaWsl = m_opts.viaWsl;
    launch.workingDirectory = m_opts.workspaceDir.trimmed().isEmpty()
        ? QCoreApplication::applicationDirPath()
        : m_opts.workspaceDir.trimmed();
    launch.optOutNotificationMethods.clear();
    m_client->setLaunchOptions(launch);

    connect(m_client, &CodexAppServerClient::stderrLineReceived, this, [this](const QString& line) {
        if (m_opts.verbose)
            printErr(QStringLiteral("[codex] %1").arg(line));
    });
    connect(m_client, &CodexAppServerClient::transportError, this, &CodexInteractiveCli::onTransportError);
    connect(m_client, &CodexAppServerClient::responseReceived, this, &CodexInteractiveCli::onResponseReceived);
    connect(m_client, &CodexAppServerClient::responseErrorReceived, this, &CodexInteractiveCli::onResponseErrorReceived);
    connect(m_client, &CodexAppServerClient::threadStarted, this, &CodexInteractiveCli::onThreadStarted);
    connect(m_client, &CodexAppServerClient::turnStarted, this, &CodexInteractiveCli::onTurnStarted);
    connect(m_client, &CodexAppServerClient::turnCompleted, this, &CodexInteractiveCli::onTurnCompleted);
    connect(m_client, &CodexAppServerClient::assistantMessageDelta, this, &CodexInteractiveCli::onAssistantMessageDelta);
    connect(m_client,
            &CodexAppServerClient::assistantMessageCompleted,
            this,
            &CodexInteractiveCli::onAssistantMessageCompleted);
    connect(m_client,
            &CodexAppServerClient::commandExecutionApprovalRequested,
            this,
            &CodexInteractiveCli::onCommandApprovalRequested);
    connect(m_client,
            &CodexAppServerClient::fileChangeApprovalRequested,
            this,
            &CodexInteractiveCli::onFileChangeApprovalRequested);
    connect(m_client, &CodexAppServerClient::started, this, [this]() {
        m_initializeRequestId = m_client->requestInitialize();
    });

    rawPrintLn("\n=== TmAgent Codex Interactive CLI ===\n");
    print(QStringLiteral("正在连接 Codex app-server..."));
    m_client->start();
}

void CodexInteractiveCli::onStdinLine(const QString& line)
{
    if (line.isNull()) {
        rawPrintLn("\nBye!");
        emit done(ExitSuccess);
        return;
    }

    if (m_pendingApproval.isActive()) {
        handleApprovalInput(line);
        return;
    }

    if (m_waitingForTurn) {
        return;
    }

    processLine(line);
}

void CodexInteractiveCli::enterRepl()
{
    if (m_startedRepl)
        return;
    m_startedRepl = true;

    rawPrintLn("输入消息直接与 Codex 对话。命令: /quit 退出, /thread 查看线程, /help 帮助\n");
    promptInput();

    StdinReaderThread* reader = new StdinReaderThread(this, this);
    connect(reader, &QThread::finished, reader, &QObject::deleteLater);
    reader->start();
}

void CodexInteractiveCli::promptInput()
{
    rawPrint("> ");
}

void CodexInteractiveCli::processLine(const QString& line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        promptInput();
        return;
    }

    if (trimmed == QStringLiteral("/quit") || trimmed == QStringLiteral("/exit")) {
        rawPrintLn("Bye!");
        emit done(ExitSuccess);
        return;
    }

    if (trimmed == QStringLiteral("/thread")) {
        print(m_threadId.isEmpty() ? QStringLiteral("(当前还没有 threadId)") : QStringLiteral("threadId: %1").arg(m_threadId));
        promptInput();
        return;
    }

    if (trimmed == QStringLiteral("/help")) {
        rawPrintLn("命令:");
        rawPrintLn("  /quit    - 退出");
        rawPrintLn("  /thread  - 显示当前 Codex threadId");
        rawPrintLn("  /help    - 显示帮助");
        promptInput();
        return;
    }

    sendUserTurn(trimmed);
}

void CodexInteractiveCli::print(const QString& msg)
{
    rawPrintLn(qPrintable(msg));
}

void CodexInteractiveCli::printErr(const QString& msg)
{
    QMutexLocker locker(&g_consoleMutex);
    std::fprintf(stderr, "%s\n", qPrintable(msg));
    std::fflush(stderr);
}

void CodexInteractiveCli::printApprovalPrompt()
{
    QString prompt = QStringLiteral("[approval] 输入 y 接受, s 本会话接受, 其它任意键拒绝: ");
    rawPrint(qPrintable(prompt));
}

void CodexInteractiveCli::handleApprovalInput(const QString& line)
{
    const QString trimmed = line.trimmed().toLower();
    if (!m_pendingApproval.isActive()) {
        promptInput();
        return;
    }

    QJsonObject result;
    if (m_pendingApproval.method == QLatin1String("item/commandExecution/requestApproval")) {
        const bool allowSession = (trimmed == QLatin1String("s") || trimmed == QLatin1String("session"));
        const bool allow = allowSession || trimmed == QLatin1String("y") || trimmed == QLatin1String("yes");
        result.insert(QStringLiteral("decision"),
                      allowSession ? QStringLiteral("acceptForSession")
                                   : (allow ? QStringLiteral("accept") : QStringLiteral("decline")));
    } else if (m_pendingApproval.method == QLatin1String("item/fileChange/requestApproval")) {
        const bool allowSession = (trimmed == QLatin1String("s") || trimmed == QLatin1String("session"));
        const bool allow = allowSession || trimmed == QLatin1String("y") || trimmed == QLatin1String("yes");
        result.insert(QStringLiteral("decision"),
                      allowSession ? QStringLiteral("acceptForSession")
                                   : (allow ? QStringLiteral("accept") : QStringLiteral("decline")));
    } else {
        m_client->sendServerRequestError(
            m_pendingApproval.requestId,
            -32601,
            QStringLiteral("TmAgent Codex CLI 暂不支持此类 server request"));
        m_pendingApproval.clear();
        return;
    }

    m_client->sendServerRequestResult(m_pendingApproval.requestId, result);
    m_pendingApproval.clear();
}

void CodexInteractiveCli::startThread()
{
    if (!m_opts.resumeThreadId.trimmed().isEmpty()) {
        m_threadStartRequestId = m_client->requestThreadResume(m_opts.resumeThreadId.trimmed());
        return;
    }
    m_threadStartRequestId = m_client->requestThreadStart();
}

void CodexInteractiveCli::sendUserTurn(const QString& text)
{
    if (m_threadId.trimmed().isEmpty()) {
        printErr(QStringLiteral("Codex thread 尚未准备好"));
        promptInput();
        return;
    }

    m_waitingForTurn = true;
    m_streamStarted = false;
    m_turnStartRequestId = m_client->requestTurnStartText(m_threadId, text);
}

void CodexInteractiveCli::finishTurn(bool success, const QString& summary)
{
    Q_UNUSED(success);
    if (m_streamStarted)
        rawPrint("\n\n");
    if (!summary.trimmed().isEmpty() && !m_streamStarted)
        rawPrintLn(qPrintable(summary));
    else if (!summary.trimmed().isEmpty())
        rawPrintLn(qPrintable(summary));

    m_waitingForTurn = false;
    m_streamStarted = false;
    m_activeTurnId.clear();
    promptInput();
}

void CodexInteractiveCli::onTransportError(const QString& message)
{
    printErr(message);
    emit done(ExitError);
}

void CodexInteractiveCli::onResponseReceived(const QString& requestId, const QJsonValue& result)
{
    if (requestId == m_initializeRequestId) {
        m_client->completeInitializeHandshake();
        startThread();
        return;
    }

    if (requestId == m_threadStartRequestId) {
        const QJsonObject thread = result.toObject().value(QStringLiteral("thread")).toObject();
        const QString threadId = thread.value(QStringLiteral("id")).toString().trimmed();
        if (!threadId.isEmpty()) {
            m_threadId = threadId;
            print(QStringLiteral("已连接 Codex thread: %1").arg(m_threadId));
            enterRepl();
            return;
        }

        printErr(QStringLiteral("thread/start 响应中未找到 threadId"));
        emit done(ExitError);
        return;
    }

    if (requestId == m_turnStartRequestId) {
        const QJsonObject turn = result.toObject().value(QStringLiteral("turn")).toObject();
        const QString turnId = turn.value(QStringLiteral("id")).toString().trimmed();
        if (!turnId.isEmpty())
            m_activeTurnId = turnId;
    }
}

void CodexInteractiveCli::onResponseErrorReceived(const QString& requestId,
                                                  int code,
                                                  const QString& message,
                                                  const QJsonObject& data)
{
    Q_UNUSED(data);
    printErr(QStringLiteral("Codex RPC 错误 [%1] %2").arg(code).arg(message));
    if (requestId == m_threadStartRequestId) {
        emit done(ExitError);
        return;
    }
    if (requestId == m_turnStartRequestId) {
        finishTurn(false, QStringLiteral("本轮失败。"));
    }
}

void CodexInteractiveCli::onThreadStarted(const QString& threadId, const QJsonObject& thread)
{
    Q_UNUSED(thread);
    if (!threadId.trimmed().isEmpty() && m_threadId.isEmpty())
        m_threadId = threadId.trimmed();
}

void CodexInteractiveCli::onTurnStarted(const QString& threadId, const QString& turnId, const QString& status)
{
    Q_UNUSED(status);
    if (threadId != m_threadId)
        return;
    if (!turnId.trimmed().isEmpty())
        m_activeTurnId = turnId.trimmed();
}

void CodexInteractiveCli::onTurnCompleted(const QString& threadId,
                                          const QString& turnId,
                                          const QString& status,
                                          const QJsonObject& error)
{
    if (threadId != m_threadId)
        return;

    const QString errorMessage = firstNonEmpty({
        error.value(QStringLiteral("message")).toString(),
        error.value(QStringLiteral("userMessage")).toString()
    });

    if (status == QLatin1String("failed")) {
        finishTurn(false, errorMessage.isEmpty() ? QStringLiteral("本轮失败。") : QStringLiteral("失败: %1").arg(errorMessage));
        return;
    }

    if (status == QLatin1String("interrupted")) {
        finishTurn(false, QStringLiteral("本轮被中断。"));
        return;
    }

    Q_UNUSED(turnId);
    finishTurn(true);
}

void CodexInteractiveCli::onAssistantMessageDelta(const QString& threadId,
                                                  const QString& turnId,
                                                  const QString& itemId,
                                                  const QString& delta)
{
    Q_UNUSED(turnId);
    Q_UNUSED(itemId);
    if (threadId != m_threadId)
        return;
    if (!m_streamStarted) {
        m_streamStarted = true;
        rawPrint("\n");
    }
    rawPrint(qPrintable(delta));
}

void CodexInteractiveCli::onAssistantMessageCompleted(const QString& threadId,
                                                      const QString& turnId,
                                                      const QString& itemId,
                                                      const QString& text)
{
    Q_UNUSED(turnId);
    Q_UNUSED(itemId);
    if (threadId != m_threadId || text.trimmed().isEmpty())
        return;
    if (m_streamStarted)
        return;

    m_streamStarted = true;
    rawPrint("\n");
    rawPrint(qPrintable(text));
}

void CodexInteractiveCli::onCommandApprovalRequested(const QString& requestId,
                                                     const QString& threadId,
                                                     const QString& turnId,
                                                     const QString& itemId,
                                                     const QString& command,
                                                     const QString& cwd,
                                                     const QString& reason,
                                                     const QStringList& availableDecisions)
{
    if (threadId != m_threadId)
        return;

    m_pendingApproval.requestId = requestId;
    m_pendingApproval.method = QStringLiteral("item/commandExecution/requestApproval");
    m_pendingApproval.threadId = threadId;
    m_pendingApproval.turnId = turnId;
    m_pendingApproval.itemId = itemId;
    m_pendingApproval.command = command;
    m_pendingApproval.cwd = cwd;
    m_pendingApproval.reason = reason;
    m_pendingApproval.availableDecisions = availableDecisions;

    rawPrintLn("");
    print(QStringLiteral("[approval] Codex 请求执行命令"));
    if (!reason.trimmed().isEmpty())
        print(QStringLiteral("原因: %1").arg(reason));
    if (!cwd.trimmed().isEmpty())
        print(QStringLiteral("目录: %1").arg(cwd));
    if (!command.trimmed().isEmpty())
        print(QStringLiteral("命令: %1").arg(command));
    printApprovalPrompt();
}

void CodexInteractiveCli::onFileChangeApprovalRequested(const QString& requestId,
                                                        const QString& threadId,
                                                        const QString& turnId,
                                                        const QString& itemId,
                                                        const QString& reason,
                                                        const QString& grantRoot)
{
    if (threadId != m_threadId)
        return;

    m_pendingApproval.requestId = requestId;
    m_pendingApproval.method = QStringLiteral("item/fileChange/requestApproval");
    m_pendingApproval.threadId = threadId;
    m_pendingApproval.turnId = turnId;
    m_pendingApproval.itemId = itemId;
    m_pendingApproval.reason = reason;
    m_pendingApproval.grantRoot = grantRoot;

    rawPrintLn("");
    print(QStringLiteral("[approval] Codex 请求文件改动权限"));
    if (!reason.trimmed().isEmpty())
        print(QStringLiteral("原因: %1").arg(reason));
    if (!grantRoot.trimmed().isEmpty())
        print(QStringLiteral("授权根目录: %1").arg(grantRoot));
    printApprovalPrompt();
}
