#include "CodexDelegateBackend.h"
#include "CodexAppServerClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <memory>

namespace {

// Helper functions extracted from DelegateBackendSupport to avoid core dependency
static constexpr int kStructuredRawOutputMaxChars = 1200;

QString truncateForData(const QString& text, int maxChars)
{
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars) + QStringLiteral("\n...[truncated]...");
}

QStringList collectReportLines(const QString& text, int maxLines)
{
    QStringList out;
    const QStringList sourceLines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (QString line : sourceLines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QStringLiteral("STATUS:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("DONE:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("PENDING:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("EVIDENCE:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("RISKS:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("NEXT:"), Qt::CaseInsensitive)
            || line.startsWith(QStringLiteral("RAW_OUTPUT:"), Qt::CaseInsensitive)) {
            continue;
        }
        if (line.startsWith(QLatin1Char('-')))
            line = line.mid(1).trimmed();
        if (line.size() > 200)
            line = line.left(200) + QStringLiteral("...");
        out.append(line);
        if (out.size() >= maxLines)
            break;
    }
    return out;
}

QString extractStatusTag(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral("(?im)^\\s*STATUS\\s*:\\s*(COMPLETED|PARTIAL|BLOCKED)\\b"));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return QString();
    return match.captured(1).trimmed().toUpper();
}

QString canonicalStatusTag(const QString& rawStatus)
{
    const QString status = rawStatus.trimmed().toUpper();
    if (status == QLatin1String("COMPLETED")
        || status == QLatin1String("PARTIAL")
        || status == QLatin1String("BLOCKED")) {
        return status;
    }
    return QStringLiteral("PARTIAL");
}

QString ensureStructuredDelegateOutput(
    const QString& task,
    const QString& rawText,
    const QString& statusHint,
    bool* normalizedByScheduler = nullptr)
{
    if (normalizedByScheduler)
        *normalizedByScheduler = false;

    const QString text = rawText.trimmed();
    const QString status = canonicalStatusTag(statusHint.isEmpty() ? extractStatusTag(text) : statusHint);
    const bool hasStatus = QRegularExpression(
                               QStringLiteral("(?im)^\\s*STATUS\\s*:\\s*(COMPLETED|PARTIAL|BLOCKED)\\b"))
                               .match(text)
                               .hasMatch();
    const bool hasEvidence = QRegularExpression(QStringLiteral("(?im)^\\s*EVIDENCE\\s*:")).match(text).hasMatch();
    const bool hasNext = QRegularExpression(QStringLiteral("(?im)^\\s*NEXT\\s*:")).match(text).hasMatch();
    if (hasStatus && hasEvidence && hasNext)
        return text;

    if (normalizedByScheduler)
        *normalizedByScheduler = true;

    const QStringList lines = collectReportLines(text, 6);
    QStringList report;
    report << QStringLiteral("[Sub-agent Report]");
    report << QStringLiteral("STATUS: %1").arg(status);
    if (!task.trimmed().isEmpty())
        report << QStringLiteral("TASK: %1").arg(task.trimmed().left(240));
    report << QStringLiteral("DONE:");
    if (!lines.isEmpty()) {
        for (const QString& line : lines)
            report << QStringLiteral("- %1").arg(line);
    } else {
        report << QStringLiteral("- (未提取到明确完成项)");
    }
    report << QStringLiteral("PENDING:");
    if (status == QLatin1String("COMPLETED"))
        report << QStringLiteral("- (无)");
    else
        report << QStringLiteral("- 需要主代理补充约束或继续拆分任务。");
    report << QStringLiteral("EVIDENCE:");
    if (!lines.isEmpty()) {
        for (const QString& line : lines.mid(0, 3))
            report << QStringLiteral("- %1").arg(line);
    } else if (!text.isEmpty()) {
        report << QStringLiteral("- 已返回文本输出，详见 RAW_OUTPUT。");
    } else {
        report << QStringLiteral("- 子代理未返回有效正文。");
    }
    report << QStringLiteral("RISKS:");
    if (status == QLatin1String("BLOCKED"))
        report << QStringLiteral("- 当前存在阻塞，需要补充信息/权限/路径。");
    else if (status == QLatin1String("PARTIAL"))
        report << QStringLiteral("- 当前结果可能不完整，建议复核后继续。");
    else
        report << QStringLiteral("- (无明显风险)");
    report << QStringLiteral("NEXT:");
    if (status == QLatin1String("COMPLETED"))
        report << QStringLiteral("- 主代理可汇总结果并向用户确认是否继续深化。");
    else
        report << QStringLiteral("- 主代理应先补齐阻塞条件，再发起下一轮委派。");
    if (!text.isEmpty()) {
        report << QStringLiteral("RAW_OUTPUT:");
        report << truncateForData(text, kStructuredRawOutputMaxChars);
    }
    return report.join(QStringLiteral("\n"));
}

// End of helper functions

struct CodexSessionState {
    TmAgent::DelegateRequest request;
    TmAgent::DelegateCallbacks callbacks;
    QPointer<CodexAppServerClient> client;
    QString accumulatedText;
    QString initializeRequestId;
    QString threadStartRequestId;
    QString turnStartRequestId;
};

class CodexDelegateBackendSession final : public TmAgent::IDelegateSession {
public:
    explicit CodexDelegateBackendSession(
        const TmAgent::DelegateRequest& request,
        const TmAgent::DelegateCallbacks& callbacks)
        : m_state(std::make_shared<CodexSessionState>())
    {
        m_state->request = request;
        m_state->callbacks = callbacks;
        m_state->client = new CodexAppServerClient(QCoreApplication::instance());

        CodexAppServerClient::LaunchOptions launch = CodexAppServerClient::defaultLaunchOptions();
        launch.clientName = QStringLiteral("tmagent-delegate");
        launch.clientTitle = QStringLiteral("TmAgent Delegate");
        launch.workingDirectory = request.childConfig.workspaceDir.trimmed().isEmpty()
            ? QDir::currentPath()
            : request.childConfig.workspaceDir.trimmed();
        launch.optOutNotificationMethods.clear();
        m_state->client->setLaunchOptions(launch);

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::started,
            m_state->client,
            [state = m_state]() {
                if (!state->client)
                    return;
                state->initializeRequestId = state->client->requestInitialize();
            });

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::responseReceived,
            m_state->client,
            [state = m_state](const QString& requestId, const QJsonValue& resultValue) {
                if (state->callbacks.onActivity)
                    state->callbacks.onActivity();
                if (!state->client)
                    return;

                if (requestId == state->initializeRequestId) {
                    state->client->completeInitializeHandshake();

                    QJsonObject threadOverrides;
                    threadOverrides.insert(QStringLiteral("approvalPolicy"), QStringLiteral("never"));
                    threadOverrides.insert(QStringLiteral("sandbox"), QStringLiteral("danger-full-access"));
                    threadOverrides.insert(QStringLiteral("developerInstructions"), state->request.childConfig.systemPrompt);
                    threadOverrides.insert(QStringLiteral("serviceName"), QStringLiteral("TmAgent Codex Delegate"));
                    state->threadStartRequestId = state->client->requestThreadStart(threadOverrides);
                    if (state->callbacks.onSummary)
                        state->callbacks.onSummary(QStringLiteral("正在建立 Codex 子代理线程"));
                    return;
                }

                if (requestId == state->threadStartRequestId) {
                    const QJsonObject thread = resultValue.toObject().value(QStringLiteral("thread")).toObject();
                    const QString threadId = thread.value(QStringLiteral("id")).toString().trimmed();
                    if (threadId.isEmpty()) {
                        if (state->callbacks.onFailure)
                            state->callbacks.onFailure(QStringLiteral("codex thread/start missing thread id"));
                        return;
                    }
                    if (state->callbacks.onSummary) {
                        state->callbacks.onSummary(
                            QStringLiteral("Codex 子代理线程已建立(%1)").arg(threadId.left(8)));
                    }
                    state->turnStartRequestId =
                        state->client->requestTurnStartText(threadId, state->request.executionPrompt);
                    return;
                }

                if (requestId == state->turnStartRequestId) {
                    const QJsonObject turn = resultValue.toObject().value(QStringLiteral("turn")).toObject();
                    const QString turnId = turn.value(QStringLiteral("id")).toString().trimmed();
                    if (state->callbacks.onSummary)
                        state->callbacks.onSummary(QStringLiteral("Codex 子代理执行中"));
                }
            });

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::responseErrorReceived,
            m_state->client,
            [state = m_state](const QString& requestId, int code, const QString& message, const QJsonObject&) {
                if (requestId == state->initializeRequestId
                    || requestId == state->threadStartRequestId
                    || requestId == state->turnStartRequestId) {
                    if (state->callbacks.onFailure) {
                        state->callbacks.onFailure(
                            QStringLiteral("codex rpc error [%1] %2").arg(code).arg(message));
                    }
                }
            });

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::assistantMessageDelta,
            m_state->client,
            [state = m_state](const QString&, const QString&, const QString&, const QString& delta) {
                if (state->callbacks.onActivity)
                    state->callbacks.onActivity();
                state->accumulatedText += delta;
                if (state->callbacks.onStreamDelta)
                    state->callbacks.onStreamDelta(delta);
                if (state->callbacks.onSummary)
                    state->callbacks.onSummary(QStringLiteral("Codex 子代理正在回复"));
            });

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::assistantMessageCompleted,
            m_state->client,
            [state = m_state](const QString&, const QString&, const QString&, const QString& text) {
                if (state->callbacks.onActivity)
                    state->callbacks.onActivity();
                if (state->accumulatedText.trimmed().isEmpty()) {
                    state->accumulatedText = text;
                } else if (text.size() > state->accumulatedText.size()
                           && text.contains(state->accumulatedText)) {
                    state->accumulatedText = text;
                }
                if (state->callbacks.onSummary)
                    state->callbacks.onSummary(QStringLiteral("Codex 子代理已生成回复"));
            });

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::commandExecutionApprovalRequested,
            m_state->client,
            [state = m_state](const QString& requestId,
                              const QString&,
                              const QString&,
                              const QString&,
                              const QString& command,
                              const QString& cwd,
                              const QString& reason,
                              const QStringList&) {
                if (state->callbacks.onActivity)
                    state->callbacks.onActivity();
                if (state->callbacks.onSummary)
                    state->callbacks.onSummary(QStringLiteral("Codex 请求命令执行权限，已自动放行"));
                if (!state->client)
                    return;
                QJsonObject response;
                response.insert(QStringLiteral("decision"), QStringLiteral("acceptForSession"));
                state->client->sendServerRequestResult(requestId, response);
            });

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::fileChangeApprovalRequested,
            m_state->client,
            [state = m_state](const QString& requestId,
                              const QString&,
                              const QString&,
                              const QString&,
                              const QString& reason,
                              const QString& grantRoot) {
                if (state->callbacks.onActivity)
                    state->callbacks.onActivity();
                if (state->callbacks.onSummary)
                    state->callbacks.onSummary(QStringLiteral("Codex 请求文件改动权限，已自动放行"));
                if (!state->client)
                    return;
                QJsonObject response;
                response.insert(QStringLiteral("decision"), QStringLiteral("acceptForSession"));
                state->client->sendServerRequestResult(requestId, response);
            });

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::serverRequestReceived,
            m_state->client,
            [state = m_state](const QString& requestId, const QString& method, const QJsonValue&) {
                if (method == QLatin1String("item/commandExecution/requestApproval")
                    || method == QLatin1String("item/fileChange/requestApproval")) {
                    return;
                }
                if (state->client) {
                    state->client->sendServerRequestError(
                        requestId,
                        -32601,
                        QStringLiteral("TmAgent delegate backend 暂不支持该 Codex server request"));
                }
                if (state->callbacks.onFailure) {
                    state->callbacks.onFailure(
                        QStringLiteral("unsupported codex server request: %1").arg(method));
                }
            });

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::turnCompleted,
            m_state->client,
            [state = m_state](const QString&, const QString& turnId, const QString& status, const QJsonObject& error) {
                if (state->callbacks.onActivity)
                    state->callbacks.onActivity();

                if (status == QLatin1String("failed")) {
                    const QString message = error.value(QStringLiteral("message")).toString().trimmed();
                    if (state->callbacks.onFailure) {
                        state->callbacks.onFailure(
                            message.isEmpty() ? QStringLiteral("codex turn failed") : message);
                    }
                    return;
                }

                if (status == QLatin1String("interrupted")) {
                    if (state->callbacks.onFailure)
                        state->callbacks.onFailure(QStringLiteral("codex turn interrupted"));
                    return;
                }

                QString normalized = ensureStructuredDelegateOutput(
                    state->request.task,
                    state->accumulatedText.trimmed(),
                    QStringLiteral("COMPLETED"),
                    nullptr);
                normalized.prepend(QStringLiteral("[Codex Delegate Report]\n"));
                if (normalized.size() > state->request.maxResponseChars) {
                    normalized = normalized.left(state->request.maxResponseChars)
                        + QStringLiteral("\n...[delegate response truncated]...");
                }
                if (state->callbacks.onSuccess)
                    state->callbacks.onSuccess(normalized);
            });

        QObject::connect(
            m_state->client,
            &CodexAppServerClient::transportError,
            m_state->client,
            [state = m_state](const QString& message) {
                if (state->callbacks.onFailure) {
                    state->callbacks.onFailure(
                        message.trimmed().isEmpty()
                            ? QStringLiteral("codex transport error")
                            : message.trimmed());
                }
            });
    }

    ~CodexDelegateBackendSession() override
    {
        cancel();
    }

    QString backendId() const override
    {
        return QStringLiteral("codex");
    }

    void start() override
    {
        if (m_state && m_state->client)
            m_state->client->start();
    }

    void cancel() override
    {
        if (!m_state || !m_state->client)
            return;
        m_state->client->shutdown();
        m_state->client->deleteLater();
        m_state->client = nullptr;
    }

private:
    std::shared_ptr<CodexSessionState> m_state;
};

} // namespace

QString CodexDelegateBackend::backendId() const
{
    return QStringLiteral("codex");
}

std::unique_ptr<TmAgent::IDelegateSession> CodexDelegateBackend::createSession(
    const TmAgent::DelegateRequest& request,
    const TmAgent::DelegateCallbacks& callbacks,
    QString* error)
{
    if (error)
        error->clear();
    return std::make_unique<CodexDelegateBackendSession>(request, callbacks);
}
