#include "AgentRuntime.h"
#include "core/agent/LLMAgent.h"
#include "core/agent/ToolDispatcher.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "newCore/ModelFactory.h"

AgentRuntime::AgentRuntime(Identity* identity, QObject* parent)
    : QObject(parent)
    , m_identity(identity)
{
    m_llmAgent = new LLMAgent(this);
    connectAgentSignals();
}

AgentRuntime::~AgentRuntime() = default;

Identity* AgentRuntime::identity() const { return m_identity; }
LLMAgent* AgentRuntime::llmAgent() const { return m_llmAgent; }
QString AgentRuntime::identityId() const { return m_identity ? m_identity->id() : QString(); }

void AgentRuntime::sendMessage(const QString& sessionId, const QString& text)
{
    if (!m_llmAgent)
        return;

    // 如果切换了 Session，仅同步 IO 历史；LLM 对话历史由 ChatService 从 Message 主链路重建。
    if (m_currentSessionId != sessionId) {
        switchToSession(sessionId);
    }

    m_isStreaming = true;
    m_llmAgent->sendMessage(text);
}

void AgentRuntime::abort()
{
    if (m_llmAgent)
        m_llmAgent->abort();
    m_isStreaming = false;
}

bool AgentRuntime::isStreaming() const { return m_isStreaming; }

void AgentRuntime::setModelFactory(ModelFactory* factory)
{
    if (m_llmAgent)
        m_llmAgent->setModelFactory(factory);
}

void AgentRuntime::setToolDispatcher(ToolDispatcher* dispatcher)
{
    if (!m_llmAgent)
        return;

    QStringList allowedTools;
    if (m_identity && m_identity->profile())
        allowedTools = m_identity->profile()->allowedTools();
    m_llmAgent->setToolDispatcher(dispatcher, allowedTools);
}

void AgentRuntime::applyConfig()
{
    if (!m_identity || !m_llmAgent)
        return;
    if (m_identity->isAgent() && m_identity->profile()) {
        LLMConfig cfg = m_identity->profile()->llmConfig();
        cfg.userName = m_identity->name();
        cfg.uuid = m_identity->id();
        m_llmAgent->setConfig(cfg);
    }
}

void AgentRuntime::switchToSession(const QString& sessionId)
{
    // 保存当前 Session 的 IO 历史到 Runtime 内存缓存（不再依赖 Session::ioHistory）。
    if (!m_currentSessionId.isEmpty() && m_llmAgent)
        m_sessionIoHistory.insert(m_currentSessionId, m_llmAgent->getIoHistory());

    m_currentSessionId = sessionId;

    // 加载新 Session 的 IO 历史（LLM 对话历史仍由 ChatService 主链路注入）。
    if (m_llmAgent)
        m_llmAgent->setIoHistory(m_sessionIoHistory.value(sessionId));
}

QString AgentRuntime::currentSessionId() const { return m_currentSessionId; }

void AgentRuntime::setHistory(const QJsonArray& history)
{
    if (m_llmAgent)
        m_llmAgent->setHistory(history);
}

QJsonArray AgentRuntime::getHistory() const
{
    return m_llmAgent ? m_llmAgent->getHistory() : QJsonArray();
}

QJsonArray AgentRuntime::getIoHistory() const
{
    if (!m_currentSessionId.isEmpty())
        return m_sessionIoHistory.value(m_currentSessionId);
    return m_llmAgent ? m_llmAgent->getIoHistory() : QJsonArray();
}

void AgentRuntime::clearHistory()
{
    if (m_llmAgent)
        m_llmAgent->clearHistory();
    if (!m_currentSessionId.isEmpty())
        m_sessionIoHistory.insert(m_currentSessionId, QJsonArray());
}

QString AgentRuntime::abortAndRollback()
{
    m_isStreaming = false;
    return m_llmAgent ? m_llmAgent->abortAndRollback() : QString();
}

LLMConfig AgentRuntime::config() const
{
    return m_llmAgent ? m_llmAgent->config() : LLMConfig();
}

void AgentRuntime::setConfig(const LLMConfig& config)
{
    if (m_llmAgent)
        m_llmAgent->setConfig(config);
}

void AgentRuntime::connectAgentSignals()
{
    if (!m_llmAgent)
        return;
    connect(m_llmAgent, &LLMAgent::streamDataReceived, this, &AgentRuntime::onStreamDataReceived);
    connect(m_llmAgent, &LLMAgent::finished, this, &AgentRuntime::onFinished);
    connect(m_llmAgent, &LLMAgent::errorOccurred, this, &AgentRuntime::onErrorOccurred);
    connect(m_llmAgent, &LLMAgent::toolCallsStarted, this, &AgentRuntime::onToolCallsStarted);
    connect(m_llmAgent, &LLMAgent::toolEvent, this, &AgentRuntime::onToolEvent);
}

void AgentRuntime::saveCurrentIoHistory()
{
    if (!m_currentSessionId.isEmpty() && m_llmAgent)
        m_sessionIoHistory.insert(m_currentSessionId, m_llmAgent->getIoHistory());
}

void AgentRuntime::onStreamDataReceived(const QString& data)
{
    emit streamDataReceived(m_currentSessionId, data);
}

void AgentRuntime::onFinished(const QString& fullContent)
{
    m_isStreaming = false;
    saveCurrentIoHistory();
    emit finished(m_currentSessionId, fullContent);
}

void AgentRuntime::onErrorOccurred(const QString& errorMsg)
{
    m_isStreaming = false;
    saveCurrentIoHistory();
    emit errorOccurred(m_currentSessionId, errorMsg);
}

void AgentRuntime::onToolCallsStarted()
{
    saveCurrentIoHistory();
    emit toolCallsStarted(m_currentSessionId);
}

void AgentRuntime::onToolEvent(const ToolExecutionEvent& event)
{
    emit toolEvent(m_currentSessionId, event);
}
