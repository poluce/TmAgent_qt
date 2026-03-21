#include "TmagentDelegateBackend.h"

#include "DelegateBackendSupport.h"
#include "../LLMAgent.h"
#include "../ToolDispatcher.h"
#include "llm/ModelFactory.h"

#include <QCoreApplication>
#include <QPointer>
#include <memory>

namespace DelegateBackendInternal {
namespace {

struct TmagentSessionState {
    DelegateBackendStartRequest request;
    DelegateBackendCallbacks callbacks;
    QPointer<LLMAgent> agent;
};

void configureChildTools(const DelegateBackendStartRequest& request, LLMAgent* agent)
{
    if (!agent || !request.toolDispatcher)
        return;

    QStringList childAllowedTools = request.inheritedAllowedTools;
    if (request.restrictDelegation)
        childAllowedTools.removeAll(QStringLiteral("delegate_task"));
    childAllowedTools.removeAll(QStringLiteral("delegate_status"));
    childAllowedTools.removeAll(QStringLiteral("delegate_cancel"));
    childAllowedTools.removeAll(QStringLiteral("delegate_list_active"));
    if (childAllowedTools.isEmpty())
        agent->setToolDispatcher(request.toolDispatcher);
    else
        agent->setToolDispatcher(request.toolDispatcher, childAllowedTools);
}

class TmagentDelegateBackendSession final : public IDelegateBackendSession {
public:
    explicit TmagentDelegateBackendSession(
        const DelegateBackendStartRequest& request,
        const DelegateBackendCallbacks& callbacks)
        : m_state(std::make_shared<TmagentSessionState>())
    {
        m_state->request = request;
        m_state->callbacks = callbacks;
        m_state->agent = new LLMAgent(QCoreApplication::instance());
        m_state->agent->setModelFactory(ModelFactory::instance());
        m_state->agent->setConfig(request.childConfig);
        configureChildTools(request, m_state->agent);

        QObject::connect(
            m_state->agent,
            &LLMAgent::toolEvent,
            m_state->agent,
            [state = m_state](const ToolExecutionEvent& event) {
                if (state->callbacks.onActivity)
                    state->callbacks.onActivity();
                if (state->callbacks.onToolEvent)
                    state->callbacks.onToolEvent(event);
            });

        QObject::connect(
            m_state->agent,
            &LLMAgent::streamDataReceived,
            m_state->agent,
            [state = m_state](const QString& chunk) {
                if (state->callbacks.onActivity)
                    state->callbacks.onActivity();
                if (state->callbacks.onStreamDelta)
                    state->callbacks.onStreamDelta(chunk);
                if (state->callbacks.onSummary)
                    state->callbacks.onSummary(QStringLiteral("后台子代理执行中"));
            });

        QObject::connect(
            m_state->agent,
            &LLMAgent::finished,
            m_state->agent,
            [state = m_state](const QString& content) {
                QString normalized = ensureStructuredDelegateOutput(
                    state->request.task,
                    content.trimmed(),
                    extractStatusTag(content),
                    nullptr);
                if (normalized.size() > state->request.maxResponseChars) {
                    normalized = normalized.left(state->request.maxResponseChars)
                        + QStringLiteral("\n...[delegate response truncated]...");
                }
                if (state->callbacks.onSuccess)
                    state->callbacks.onSuccess(normalized);
            });

        QObject::connect(
            m_state->agent,
            &LLMAgent::errorOccurred,
            m_state->agent,
            [state = m_state](const QString& msg) {
                const QString err = msg.trimmed().isEmpty()
                    ? QStringLiteral("sub-agent error")
                    : msg.trimmed();
                if (state->callbacks.onFailure)
                    state->callbacks.onFailure(err);
            });
    }

    ~TmagentDelegateBackendSession() override
    {
        cancel();
    }

    QString backendId() const override
    {
        return QStringLiteral("tmagent");
    }

    QString backendProgram() const override
    {
        return QString();
    }

    void start() override
    {
        if (m_state && m_state->agent)
            m_state->agent->askOnce(m_state->request.executionPrompt);
    }

    void cancel() override
    {
        if (!m_state || !m_state->agent)
            return;
        m_state->agent->abort();
        m_state->agent->deleteLater();
        m_state->agent = nullptr;
    }

private:
    std::shared_ptr<TmagentSessionState> m_state;
};

} // namespace

QString TmagentDelegateBackend::backendId() const
{
    return QStringLiteral("tmagent");
}

std::unique_ptr<IDelegateBackendSession> TmagentDelegateBackend::createSession(
    const DelegateBackendStartRequest& request,
    const DelegateBackendCallbacks& callbacks,
    QString* error)
{
    if (error)
        error->clear();
    return std::make_unique<TmagentDelegateBackendSession>(request, callbacks);
}

} // namespace DelegateBackendInternal
