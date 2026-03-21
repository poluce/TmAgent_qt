#include "CodexDelegateBackend.h"

#include "DelegateBackendSupport.h"
#include "core/service/include/CodexAppServerClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <memory>

namespace DelegateBackendInternal {
namespace {

struct CodexSessionState {
    DelegateBackendStartRequest request;
    DelegateBackendCallbacks callbacks;
    QPointer<CodexAppServerClient> client;
    QString accumulatedText;
    QString initializeRequestId;
    QString threadStartRequestId;
    QString turnStartRequestId;
};

class CodexDelegateBackendSession final : public IDelegateBackendSession {
public:
    explicit CodexDelegateBackendSession(
        const DelegateBackendStartRequest& request,
        const DelegateBackendCallbacks& callbacks)
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
                    if (state->callbacks.onBackendIdentity)
                        state->callbacks.onBackendIdentity(threadId, QString(), QString());
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
                    if (state->callbacks.onBackendIdentity)
                        state->callbacks.onBackendIdentity(QString(), turnId, QString());
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
                if (state->callbacks.onTimelineEvent) {
                    QJsonObject extra;
                    if (!command.trimmed().isEmpty())
                        extra.insert(QStringLiteral("command"), command.left(200));
                    if (!cwd.trimmed().isEmpty())
                        extra.insert(QStringLiteral("cwd"), cwd.left(160));
                    state->callbacks.onTimelineEvent(
                        QStringLiteral("approval_auto_accepted"),
                        !reason.trimmed().isEmpty() ? reason : command.left(160),
                        extra);
                }
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
                if (state->callbacks.onTimelineEvent) {
                    QJsonObject extra;
                    if (!grantRoot.trimmed().isEmpty())
                        extra.insert(QStringLiteral("grant_root"), grantRoot.left(160));
                    state->callbacks.onTimelineEvent(
                        QStringLiteral("file_change_auto_accepted"),
                        !reason.trimmed().isEmpty() ? reason : grantRoot.left(160),
                        extra);
                }
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
                if (state->callbacks.onBackendIdentity)
                    state->callbacks.onBackendIdentity(QString(), turnId.trimmed(), QString());

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

    QString backendProgram() const override
    {
        if (!m_state || !m_state->client)
            return QStringLiteral("codex");
        return m_state->client->programDisplayName();
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

std::unique_ptr<IDelegateBackendSession> CodexDelegateBackend::createSession(
    const DelegateBackendStartRequest& request,
    const DelegateBackendCallbacks& callbacks,
    QString* error)
{
    if (error)
        error->clear();
    return std::make_unique<CodexDelegateBackendSession>(request, callbacks);
}

} // namespace DelegateBackendInternal
