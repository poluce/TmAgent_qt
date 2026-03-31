#include "CodexAppServerClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QRegularExpression>

namespace {

QJsonArray toJsonArray(const QStringList& values)
{
    QJsonArray array;
    for (const QString& value : values) {
        if (!value.trimmed().isEmpty())
            array.append(value.trimmed());
    }
    return array;
}

QJsonObject mergeObjects(const QJsonObject& base, const QJsonObject& overrides)
{
    QJsonObject merged = base;
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
        merged.insert(it.key(), it.value());
    return merged;
}

} // namespace

CodexAppServerClient::CodexAppServerClient(QObject* parent)
    : QObject(parent)
    , m_launchOptions(defaultLaunchOptions())
{
}

CodexAppServerClient::~CodexAppServerClient()
{
    shutdown();
}

CodexAppServerClient::LaunchOptions CodexAppServerClient::defaultLaunchOptions()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    LaunchOptions options;
    const QString bin = env.value(QStringLiteral("TMAGENT_CODEX_BIN")).trimmed();
    options.program = bin.isEmpty() ? QStringLiteral("codex") : bin;

    const QString wslFlag = env.value(QStringLiteral("TMAGENT_CODEX_VIA_WSL")).trimmed().toLower();
    options.viaWsl = wslFlag == QLatin1String("1")
        || wslFlag == QLatin1String("true")
        || wslFlag == QLatin1String("yes")
        || wslFlag == QLatin1String("on");

    options.clientName = QStringLiteral("tmagent-qt");
    options.clientTitle = QStringLiteral("TmAgent Qt");
    options.clientVersion = QCoreApplication::applicationVersion().trimmed().isEmpty()
        ? QStringLiteral("1.0.0")
        : QCoreApplication::applicationVersion().trimmed();
    options.experimentalApi = true;
    options.optOutNotificationMethods = defaultOptOutNotificationMethods();
    return options;
}

QStringList CodexAppServerClient::defaultOptOutNotificationMethods()
{
    return QStringList {
        QStringLiteral("command/exec/outputDelta"),
        QStringLiteral("item/agentMessage/delta"),
        QStringLiteral("item/plan/delta"),
        QStringLiteral("item/fileChange/outputDelta"),
        QStringLiteral("item/reasoning/summaryTextDelta"),
        QStringLiteral("item/reasoning/textDelta")
    };
}

QJsonObject CodexAppServerClient::makeTextInput(const QString& text)
{
    QJsonObject input;
    input.insert(QStringLiteral("type"), QStringLiteral("text"));
    input.insert(QStringLiteral("text"), text);
    input.insert(QStringLiteral("text_elements"), QJsonArray());
    return input;
}

void CodexAppServerClient::setLaunchOptions(const LaunchOptions& options)
{
    m_launchOptions = options;
    if (m_launchOptions.program.trimmed().isEmpty())
        m_launchOptions.program = QStringLiteral("codex");
    if (m_launchOptions.clientName.trimmed().isEmpty())
        m_launchOptions.clientName = QStringLiteral("tmagent-qt");
    if (m_launchOptions.clientVersion.trimmed().isEmpty()) {
        m_launchOptions.clientVersion = QCoreApplication::applicationVersion().trimmed().isEmpty()
            ? QStringLiteral("1.0.0")
            : QCoreApplication::applicationVersion().trimmed();
    }
}

CodexAppServerClient::LaunchOptions CodexAppServerClient::launchOptions() const
{
    return m_launchOptions;
}

void CodexAppServerClient::start()
{
    if (isRunning())
        return;

    ensureProcess();
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    emitReadyChanged(false);
    m_nextRequestId = 1;

    const QString program = resolveProgram();
    const QStringList arguments = resolveArguments();

    if (!m_launchOptions.viaWsl) {
        const QString workDir = QDir::cleanPath(m_launchOptions.workingDirectory.trimmed());
        if (!workDir.isEmpty())
            m_process->setWorkingDirectory(workDir);
        else
            m_process->setWorkingDirectory(QDir::currentPath());
    } else {
        m_process->setWorkingDirectory(QDir::currentPath());
    }

    m_process->start(program, arguments);
    if (!m_process->waitForStarted(5000)) {
        emit transportError(QStringLiteral("Codex app-server 启动失败：%1").arg(m_process->errorString()));
        return;
    }

    emit started();
}

void CodexAppServerClient::shutdown(int timeoutMs)
{
    if (!m_process)
        return;

    if (m_process->state() == QProcess::NotRunning) {
        emitReadyChanged(false);
        return;
    }

    m_process->closeWriteChannel();
    m_process->terminate();
    if (!m_process->waitForFinished(timeoutMs)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool CodexAppServerClient::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool CodexAppServerClient::isReady() const
{
    return m_ready;
}

QString CodexAppServerClient::programDisplayName() const
{
    const QString program = m_launchOptions.program.trimmed().isEmpty()
        ? QStringLiteral("codex") : m_launchOptions.program.trimmed();
    if (m_launchOptions.viaWsl)
        return QStringLiteral("wsl.exe -e %1").arg(program);
#ifdef Q_OS_WIN
    return QStringLiteral("cmd.exe /c %1").arg(program);
#endif
    return program;
}

QString CodexAppServerClient::effectiveServerWorkingDirectory() const
{
    const QString workDir = m_launchOptions.workingDirectory.trimmed();
    if (workDir.isEmpty())
        return QString();
    return m_launchOptions.viaWsl ? toWslPath(workDir) : QDir::cleanPath(workDir);
}

QString CodexAppServerClient::sendRequest(const QString& method, const QJsonObject& params)
{
    const QString requestId = nextRequestId();
    QJsonObject request;
    request.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    request.insert(QStringLiteral("id"), requestId);
    request.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
        request.insert(QStringLiteral("params"), params);
    writeJsonRpcObject(request);
    return requestId;
}

void CodexAppServerClient::sendNotification(const QString& method, const QJsonObject& params, bool includeParams)
{
    QJsonObject notification;
    notification.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    notification.insert(QStringLiteral("method"), method);
    if (includeParams)
        notification.insert(QStringLiteral("params"), params);
    writeJsonRpcObject(notification);
}

void CodexAppServerClient::sendServerRequestResult(const QString& requestId, const QJsonValue& result)
{
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), requestId);
    response.insert(QStringLiteral("result"), result);
    writeJsonRpcObject(response);
}

void CodexAppServerClient::sendServerRequestError(const QString& requestId,
                                                  int code,
                                                  const QString& message,
                                                  const QJsonObject& data)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);
    if (!data.isEmpty())
        error.insert(QStringLiteral("data"), data);

    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), requestId);
    response.insert(QStringLiteral("error"), error);
    writeJsonRpcObject(response);
}

QString CodexAppServerClient::requestInitialize()
{
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), m_launchOptions.clientName);
    clientInfo.insert(QStringLiteral("title"), m_launchOptions.clientTitle.isEmpty() ? QJsonValue() : QJsonValue(m_launchOptions.clientTitle));
    clientInfo.insert(QStringLiteral("version"), m_launchOptions.clientVersion);

    QStringList optOuts = m_launchOptions.optOutNotificationMethods;
    if (optOuts.isEmpty())
        optOuts = defaultOptOutNotificationMethods();

    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("experimentalApi"), m_launchOptions.experimentalApi);
    if (!optOuts.isEmpty())
        capabilities.insert(QStringLiteral("optOutNotificationMethods"), toJsonArray(optOuts));

    QJsonObject params;
    params.insert(QStringLiteral("clientInfo"), clientInfo);
    params.insert(QStringLiteral("capabilities"), capabilities);

    return sendRequest(QStringLiteral("initialize"), params);
}

void CodexAppServerClient::completeInitializeHandshake()
{
    sendNotification(QStringLiteral("initialized"), QJsonObject(), false);
    emitReadyChanged(true);
}

QString CodexAppServerClient::requestThreadStart(const QJsonObject& overrides)
{
    QJsonObject params;
    params.insert(QStringLiteral("experimentalRawEvents"), false);
    params.insert(QStringLiteral("persistExtendedHistory"), true);
    const QString cwd = effectiveServerWorkingDirectory();
    if (!cwd.isEmpty())
        params.insert(QStringLiteral("cwd"), cwd);
    return sendRequest(QStringLiteral("thread/start"), mergeObjects(params, overrides));
}

QString CodexAppServerClient::requestThreadResume(const QString& threadId, const QJsonObject& overrides)
{
    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    params.insert(QStringLiteral("persistExtendedHistory"), true);
    const QString cwd = effectiveServerWorkingDirectory();
    if (!cwd.isEmpty())
        params.insert(QStringLiteral("cwd"), cwd);
    return sendRequest(QStringLiteral("thread/resume"), mergeObjects(params, overrides));
}

QString CodexAppServerClient::requestTurnStartText(const QString& threadId,
                                                   const QString& text,
                                                   const QJsonObject& overrides)
{
    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    QJsonArray input;
    input.append(makeTextInput(text));
    params.insert(QStringLiteral("input"), input);
    const QString cwd = effectiveServerWorkingDirectory();
    if (!cwd.isEmpty())
        params.insert(QStringLiteral("cwd"), cwd);
    return sendRequest(QStringLiteral("turn/start"), mergeObjects(params, overrides));
}

QString CodexAppServerClient::requestTurnInterrupt(const QString& threadId, const QString& turnId)
{
    QJsonObject params;
    params.insert(QStringLiteral("threadId"), threadId);
    params.insert(QStringLiteral("turnId"), turnId);
    return sendRequest(QStringLiteral("turn/interrupt"), params);
}

void CodexAppServerClient::onReadyReadStandardOutput()
{
    if (!m_process)
        return;
    m_stdoutBuffer.append(m_process->readAllStandardOutput());
    drainBufferedLines(&m_stdoutBuffer, false);
}

void CodexAppServerClient::onReadyReadStandardError()
{
    if (!m_process)
        return;
    m_stderrBuffer.append(m_process->readAllStandardError());
    drainBufferedLines(&m_stderrBuffer, true);
}

void CodexAppServerClient::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    emitReadyChanged(false);
    drainBufferedLines(&m_stdoutBuffer, false);
    drainBufferedLines(&m_stderrBuffer, true);
    emit stopped(exitCode, exitStatus);
}

void CodexAppServerClient::onProcessErrorOccurred(QProcess::ProcessError error)
{
    QString message;
    switch (error) {
    case QProcess::FailedToStart:
        message = QStringLiteral("Codex app-server 进程启动失败");
        break;
    case QProcess::Crashed:
        message = QStringLiteral("Codex app-server 进程异常退出");
        break;
    case QProcess::Timedout:
        message = QStringLiteral("Codex app-server 进程操作超时");
        break;
    case QProcess::WriteError:
        message = QStringLiteral("写入 Codex app-server 失败");
        break;
    case QProcess::ReadError:
        message = QStringLiteral("读取 Codex app-server 失败");
        break;
    default:
        message = QStringLiteral("Codex app-server 进程出现未知错误");
        break;
    }

    if (m_process && !m_process->errorString().trimmed().isEmpty())
        message += QStringLiteral("：%1").arg(m_process->errorString().trimmed());
    emit transportError(message);
}

QString CodexAppServerClient::nextRequestId()
{
    return QString::number(m_nextRequestId++);
}

void CodexAppServerClient::emitReadyChanged(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    emit readyChanged(m_ready);
}

void CodexAppServerClient::ensureProcess()
{
    if (m_process)
        return;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &CodexAppServerClient::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &CodexAppServerClient::onReadyReadStandardError);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &CodexAppServerClient::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &CodexAppServerClient::onProcessErrorOccurred);
}

void CodexAppServerClient::drainBufferedLines(QByteArray* buffer, bool stderrStream)
{
    if (!buffer)
        return;

    while (true) {
        int newlineIndex = buffer->indexOf('\n');
        if (newlineIndex < 0)
            break;

        QByteArray line = buffer->left(newlineIndex);
        buffer->remove(0, newlineIndex + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.trimmed().isEmpty())
            continue;

        if (stderrStream) {
            emit stderrLineReceived(QString::fromUtf8(line));
        } else {
            handleJsonRpcLine(line);
        }
    }
}

void CodexAppServerClient::handleJsonRpcLine(const QByteArray& rawLine)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawLine, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit transportError(QStringLiteral("Codex app-server 输出了无效 JSON-RPC：%1 | raw=%2")
                                .arg(parseError.errorString(), QString::fromUtf8(rawLine)));
        return;
    }

    const QJsonObject message = document.object();
    const QJsonValue idValue = message.value(QStringLiteral("id"));
    const QString requestId = normalizeId(idValue);
    const QString method = message.value(QStringLiteral("method")).toString().trimmed();

    if (!method.isEmpty()) {
        const QJsonValue params = message.value(QStringLiteral("params"));
        if (!requestId.isEmpty()) {
            emit serverRequestReceived(requestId, method, params);
            if (method == QLatin1String("item/commandExecution/requestApproval")) {
                const QJsonObject obj = params.toObject();
                QStringList decisions;
                for (const QJsonValue& value : obj.value(QStringLiteral("availableDecisions")).toArray()) {
                    if (value.isString())
                        decisions.append(value.toString());
                }
                emit commandExecutionApprovalRequested(
                    requestId,
                    obj.value(QStringLiteral("threadId")).toString(),
                    obj.value(QStringLiteral("turnId")).toString(),
                    obj.value(QStringLiteral("itemId")).toString(),
                    obj.value(QStringLiteral("command")).toString(),
                    obj.value(QStringLiteral("cwd")).toString(),
                    obj.value(QStringLiteral("reason")).toString(),
                    decisions);
            } else if (method == QLatin1String("item/fileChange/requestApproval")) {
                const QJsonObject obj = params.toObject();
                emit fileChangeApprovalRequested(
                    requestId,
                    obj.value(QStringLiteral("threadId")).toString(),
                    obj.value(QStringLiteral("turnId")).toString(),
                    obj.value(QStringLiteral("itemId")).toString(),
                    obj.value(QStringLiteral("reason")).toString(),
                    obj.value(QStringLiteral("grantRoot")).toString());
            } else if (method == QLatin1String("item/tool/call")) {
                const QJsonObject obj = params.toObject();
                emit dynamicToolCallRequested(
                    requestId,
                    obj.value(QStringLiteral("threadId")).toString(),
                    obj.value(QStringLiteral("turnId")).toString(),
                    obj.value(QStringLiteral("callId")).toString(),
                    obj.value(QStringLiteral("tool")).toString(),
                    obj.value(QStringLiteral("arguments")).toObject());
            }
        } else {
            emit notificationReceived(method, params);
            const QJsonObject obj = params.toObject();
            const QString threadId = obj.value(QStringLiteral("threadId")).toString();
            const QString turnId = obj.value(QStringLiteral("turnId")).toString();
            const QString itemId = obj.value(QStringLiteral("itemId")).toString();

            // ── Thread 生命周期 ──
            if (method == QLatin1String("thread/started")) {
                const QJsonObject thread = obj.value(QStringLiteral("thread")).toObject();
                emit threadStarted(thread.value(QStringLiteral("id")).toString(), thread);
            } else if (method == QLatin1String("thread/status/changed")) {
                emit threadStatusChanged(threadId, obj.value(QStringLiteral("status")).toString());
            } else if (method == QLatin1String("thread/closed")) {
                emit threadClosed(threadId);
            } else if (method == QLatin1String("thread/archived")) {
                emit threadArchived(threadId);
            } else if (method == QLatin1String("thread/unarchived")) {
                emit threadUnarchived(threadId);
            } else if (method == QLatin1String("thread/name/updated")) {
                emit threadNameUpdated(threadId, obj.value(QStringLiteral("threadName")).toString());
            } else if (method == QLatin1String("thread/tokenUsage/updated")) {
                emit threadTokenUsageUpdated(threadId, turnId, obj.value(QStringLiteral("tokenUsage")).toObject());
            } else if (method == QLatin1String("thread/compacted")) {
                emit contextCompacted(threadId, obj);

            // ── Turn 生命周期 ──
            } else if (method == QLatin1String("turn/started")) {
                const QJsonObject turn = obj.value(QStringLiteral("turn")).toObject();
                emit turnStarted(threadId,
                                 turn.value(QStringLiteral("id")).toString(),
                                 turn.value(QStringLiteral("status")).toString());
            } else if (method == QLatin1String("turn/completed")) {
                const QJsonObject turn = obj.value(QStringLiteral("turn")).toObject();
                emit turnCompleted(threadId,
                                   turn.value(QStringLiteral("id")).toString(),
                                   turn.value(QStringLiteral("status")).toString(),
                                   turn.value(QStringLiteral("error")).toObject());
            } else if (method == QLatin1String("turn/diff/updated")) {
                emit turnDiffUpdated(threadId, turnId, obj.value(QStringLiteral("diff")).toString());
            } else if (method == QLatin1String("turn/plan/updated")) {
                emit turnPlanUpdated(threadId, turnId,
                                     obj.value(QStringLiteral("explanation")).toString(),
                                     obj.value(QStringLiteral("plan")).toArray());

            // ── Error ──
            } else if (method == QLatin1String("error")) {
                const QJsonObject err = obj.value(QStringLiteral("error")).toObject();
                emit errorNotification(threadId, turnId,
                                       err.value(QStringLiteral("message")).toString(),
                                       err.value(QStringLiteral("code")).toString(),
                                       obj.value(QStringLiteral("willRetry")).toBool(false));

            // ── Item 生命周期 ──
            } else if (method == QLatin1String("item/started")) {
                emit itemStarted(threadId, turnId, obj.value(QStringLiteral("item")).toObject());
            } else if (method == QLatin1String("item/completed")) {
                const QJsonObject item = obj.value(QStringLiteral("item")).toObject();
                emit itemCompleted(threadId, turnId, item);
                if (item.value(QStringLiteral("type")).toString() == QLatin1String("agentMessage")) {
                    emit assistantMessageCompleted(threadId, turnId,
                                                   item.value(QStringLiteral("id")).toString(),
                                                   item.value(QStringLiteral("text")).toString());
                }

            // ── 流式 delta ──
            } else if (method == QLatin1String("item/agentMessage/delta")) {
                emit assistantMessageDelta(threadId, turnId, itemId,
                                           obj.value(QStringLiteral("delta")).toString());
            } else if (method == QLatin1String("item/plan/delta")) {
                emit planDelta(threadId, turnId, itemId,
                               obj.value(QStringLiteral("delta")).toString());
            } else if (method == QLatin1String("item/commandExecution/outputDelta")) {
                emit commandExecutionOutputDelta(threadId, turnId, itemId,
                                                 obj.value(QStringLiteral("delta")).toString());
            } else if (method == QLatin1String("item/fileChange/outputDelta")) {
                emit fileChangeOutputDelta(threadId, turnId, itemId,
                                           obj.value(QStringLiteral("delta")).toString());
            } else if (method == QLatin1String("item/reasoning/summaryTextDelta")) {
                emit reasoningSummaryTextDelta(threadId, turnId, itemId,
                                               obj.value(QStringLiteral("delta")).toString());
            } else if (method == QLatin1String("item/reasoning/textDelta")) {
                emit reasoningTextDelta(threadId, turnId, itemId,
                                        obj.value(QStringLiteral("delta")).toString());

            // ── 终端交互 ──
            } else if (method == QLatin1String("item/commandExecution/terminalInteraction")) {
                emit terminalInteraction(threadId, turnId, itemId,
                                         obj.value(QStringLiteral("processId")).toString(),
                                         obj.value(QStringLiteral("stdin")).toString());

            // ── Guardian 审批 ──
            } else if (method == QLatin1String("item/autoApprovalReview/started")) {
                emit guardianApprovalReviewStarted(threadId, turnId,
                                                    obj.value(QStringLiteral("targetItemId")).toString(),
                                                    obj.value(QStringLiteral("review")).toObject());
            } else if (method == QLatin1String("item/autoApprovalReview/completed")) {
                emit guardianApprovalReviewCompleted(threadId, turnId,
                                                     obj.value(QStringLiteral("targetItemId")).toString(),
                                                     obj.value(QStringLiteral("review")).toObject());

            // ── Hook ──
            } else if (method == QLatin1String("hook/started")) {
                emit hookStarted(threadId, obj);
            } else if (method == QLatin1String("hook/completed")) {
                emit hookCompleted(threadId, obj);

            // ── 服务端请求已解决 ──
            } else if (method == QLatin1String("serverRequest/resolved")) {
                emit serverRequestResolved(threadId, obj.value(QStringLiteral("requestId")).toString());

            // ── MCP 工具调用进度 ──
            } else if (method == QLatin1String("item/mcpToolCall/progress")) {
                emit mcpToolCallProgress(threadId, turnId, itemId, obj);

            // ── 其他 ──
            } else if (method == QLatin1String("model/rerouted")) {
                emit modelRerouted(threadId, obj);
            } else if (method == QLatin1String("configWarning")) {
                emit configWarning(obj);
            } else if (method == QLatin1String("deprecationNotice")) {
                emit deprecationNotice(obj);
            }
        }
        return;
    }

    if (!requestId.isEmpty()) {
        if (message.contains(QStringLiteral("error"))) {
            const QJsonObject error = message.value(QStringLiteral("error")).toObject();
            emit responseErrorReceived(
                requestId,
                error.value(QStringLiteral("code")).toInt(-32000),
                error.value(QStringLiteral("message")).toString(QStringLiteral("Unknown JSON-RPC error")),
                error.value(QStringLiteral("data")).toObject());
            return;
        }

        emit responseReceived(requestId, message.value(QStringLiteral("result")));
        return;
    }

    emit transportError(QStringLiteral("Codex app-server 输出了无法识别的消息：%1")
                            .arg(QString::fromUtf8(rawLine)));
}

void CodexAppServerClient::writeJsonRpcObject(const QJsonObject& object)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        emit transportError(QStringLiteral("Codex app-server 尚未启动，无法发送 JSON-RPC 消息"));
        return;
    }

    QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    payload.append('\n');
    m_process->write(payload);
}

QString CodexAppServerClient::normalizeId(const QJsonValue& idValue) const
{
    if (idValue.isString())
        return idValue.toString();
    if (idValue.isDouble()) {
        const qint64 numericId = static_cast<qint64>(idValue.toDouble());
        return QString::number(numericId);
    }
    return QString();
}

QString CodexAppServerClient::resolveProgram() const
{
#ifdef Q_OS_WIN
    if (m_launchOptions.viaWsl)
        return QStringLiteral("wsl.exe");

    // Windows 上 npm 全局安装的命令是 .cmd 包装器，
    // QProcess 无法直接执行无扩展名的 POSIX shell 脚本，
    // 需要通过 cmd.exe /c 来启动 .cmd 文件
    return QStringLiteral("cmd.exe");
#endif
    return m_launchOptions.program.trimmed().isEmpty() ? QStringLiteral("codex") : m_launchOptions.program.trimmed();
}

QStringList CodexAppServerClient::resolveArguments() const
{
    QStringList arguments;
#ifdef Q_OS_WIN
    if (m_launchOptions.viaWsl) {
        const QString cwd = effectiveServerWorkingDirectory();
        if (!cwd.isEmpty())
            arguments << QStringLiteral("--cd") << cwd;
        arguments << QStringLiteral("-e")
                  << (m_launchOptions.program.trimmed().isEmpty() ? QStringLiteral("codex") : m_launchOptions.program.trimmed())
                  << QStringLiteral("app-server");
        arguments << m_launchOptions.extraArguments;
        return arguments;
    }

    // Windows 非 WSL：通过 cmd.exe /c 启动
    const QString program = m_launchOptions.program.trimmed().isEmpty()
        ? QStringLiteral("codex") : m_launchOptions.program.trimmed();
    arguments << QStringLiteral("/c") << program << QStringLiteral("app-server");
    arguments << m_launchOptions.extraArguments;
    return arguments;
#endif
    arguments << QStringLiteral("app-server");
    arguments << m_launchOptions.extraArguments;
    return arguments;
}

QString CodexAppServerClient::toWslPath(const QString& path) const
{
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    static const QRegularExpression drivePattern(QStringLiteral("^([A-Za-z]):/(.*)$"));
    const QRegularExpressionMatch match = drivePattern.match(normalized);
    if (!match.hasMatch())
        return normalized;

    const QString drive = match.captured(1).toLower();
    const QString rest = match.captured(2);
    return QStringLiteral("/mnt/%1/%2").arg(drive, rest);
}
