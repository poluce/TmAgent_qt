#include "AgentRuntime.h"
#include "core/agent/LLMAgent.h"
#include "core/agent/ToolDispatcher.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/tools/AgentToolNames.h"
#include "llm/ModelFactory.h"
#include <QUuid>

namespace {
void removeDelegateTools(QStringList& allowedTools)
{
    for (const QString& name : AgentToolNames::all())
        allowedTools.removeAll(name);
}

QStringList resolveAllowedToolsForProfile(const IdentityProfile* profile)
{
    if (!profile)
        return QStringList();

    QStringList allowedTools = profile->allowedTools();
    if (!profile->delegateEnabled())
        removeDelegateTools(allowedTools);
    allowedTools.removeDuplicates();
    return allowedTools;
}
} // namespace

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
    // 如果切换了 Session，仅同步 IO 历史；LLM 对话历史由 ApplicationServices 从 Message 主链路重建。
    if (m_currentSessionId != sessionId) {
        switchToSession(sessionId);
    }

    m_isStreaming = true;
    m_llmAgent->sendMessage(text);
}

void AgentRuntime::sendInternalMessage(const QString& sessionId,
                                       const QString& text,
                                       const QString& role)
{
    if (m_currentSessionId != sessionId) {
        switchToSession(sessionId);
    }

    m_isStreaming = true;
    m_llmAgent->sendInternalMessage(text, role);
}

QString AgentRuntime::runBackgroundTask(const BackgroundRunRequest& request)
{
    if (!m_llmAgent || request.prompt.trimmed().isEmpty())
        return QString();

    QString taskId = request.taskId.trimmed();
    if (taskId.isEmpty())
        taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_backgroundAgents.contains(taskId))
        return QString();

    auto* agent = new LLMAgent(this);
    agent->setModelFactory(m_llmAgent->modelFactory());
    agent->setConfig(config());
    if (m_toolDispatcher) {
        QStringList allowedTools;
        if (m_identity && m_identity->profile())
            allowedTools = resolveAllowedToolsForProfile(m_identity->profile());
        agent->setToolDispatcher(m_toolDispatcher, allowedTools);
    }

    m_backgroundAgents.insert(taskId, agent);
    connect(agent,
            &LLMAgent::finished,
            this,
            [this, taskId](const QString& fullContent) {
                LLMAgent* finishedAgent = m_backgroundAgents.take(taskId);
                if (finishedAgent)
                    finishedAgent->deleteLater();
                emit backgroundTaskFinished(taskId, fullContent);
            });
    connect(agent,
            &LLMAgent::errorOccurred,
            this,
            [this, taskId](const QString& errorMsg) {
                LLMAgent* failedAgent = m_backgroundAgents.take(taskId);
                if (failedAgent)
                    failedAgent->deleteLater();
                emit backgroundTaskError(taskId, errorMsg);
            });
    agent->sendMessage(request.prompt);
    return taskId;
}

QString AgentRuntime::runBackgroundTask(const QString& prompt)
{
    BackgroundRunRequest request;
    request.prompt = prompt;
    return runBackgroundTask(request);
}

void AgentRuntime::cancelBackgroundTask(const QString& taskId)
{
    LLMAgent* agent = m_backgroundAgents.take(taskId.trimmed());
    if (!agent)
        return;
    agent->abort();
    agent->deleteLater();
}

void AgentRuntime::abort()
{
    m_llmAgent->abort();
    const auto taskIds = m_backgroundAgents.keys();
    for (const QString& taskId : taskIds)
        cancelBackgroundTask(taskId);
    m_isStreaming = false;
}

bool AgentRuntime::isStreaming() const { return m_isStreaming; }

void AgentRuntime::setModelFactory(ModelFactory* factory)
{
    m_llmAgent->setModelFactory(factory);
}

void AgentRuntime::setToolDispatcher(ToolDispatcher* dispatcher)
{
    m_toolDispatcher = dispatcher;

    QStringList allowedTools;
    if (m_identity && m_identity->profile())
        allowedTools = resolveAllowedToolsForProfile(m_identity->profile());
    m_llmAgent->setToolDispatcher(dispatcher, allowedTools);
}

void AgentRuntime::applyConfig()
{
    if (!m_identity)
        return;
    if (m_identity->isAgent() && m_identity->profile()) {
        LLMConfig cfg = m_identity->profile()->llmConfig();
        cfg.userName = m_identity->name();
        cfg.uuid = m_identity->id();
        setConfig(cfg);
    }
}

void AgentRuntime::switchToSession(const QString& sessionId)
{
    // 保存当前 Session 的 IO 历史到 Runtime 内存缓存（不再依赖 Session::ioHistory）。
    if (!m_currentSessionId.isEmpty())
        m_sessionIoHistory.insert(m_currentSessionId, m_llmAgent->getIoHistory());

    m_currentSessionId = sessionId;

    // 加载新 Session 的 IO 历史（LLM 对话历史仍由 ApplicationServices 主链路注入）。
    m_llmAgent->setIoHistory(m_sessionIoHistory.value(sessionId));
}

QString AgentRuntime::currentSessionId() const { return m_currentSessionId; }

void AgentRuntime::setHistory(const QJsonArray& history)
{
    m_llmAgent->setHistory(history);
}

QJsonArray AgentRuntime::getHistory() const
{
    return m_llmAgent->getHistory();
}

QJsonArray AgentRuntime::getIoHistory() const
{
    if (!m_currentSessionId.isEmpty())
        return m_sessionIoHistory.value(m_currentSessionId);
    return m_llmAgent->getIoHistory();
}

void AgentRuntime::setIoContext(const QJsonObject& context)
{
    m_llmAgent->setIoContext(context);
}

void AgentRuntime::appendIoHistoryEntry(const QString& sessionId, const QJsonObject& entry)
{
    if (sessionId.trimmed().isEmpty())
        return;

    QJsonArray history;
    if (m_currentSessionId == sessionId && m_llmAgent)
        history = m_llmAgent->getIoHistory();
    else
        history = m_sessionIoHistory.value(sessionId);

    history.append(entry);
    m_sessionIoHistory.insert(sessionId, history);

    if (m_currentSessionId == sessionId && m_llmAgent)
        m_llmAgent->setIoHistory(history);
}

void AgentRuntime::clearHistory()
{
    m_llmAgent->clearHistory();
    if (!m_currentSessionId.isEmpty())
        m_sessionIoHistory.insert(m_currentSessionId, QJsonArray());
}

QString AgentRuntime::abortAndRollback()
{
    m_isStreaming = false;
    return m_llmAgent->abortAndRollback();
}

LLMConfig AgentRuntime::config() const
{
    return m_llmAgent->config();
}

void AgentRuntime::setConfig(const LLMConfig& config)
{
    m_llmAgent->setConfig(config);

    // delegate 系列工具的可见性与 recursionDepth 相关，配置变化后需要刷新工具白名单。
    if (m_toolDispatcher) {
        QStringList allowedTools;
        if (m_identity && m_identity->profile())
            allowedTools = resolveAllowedToolsForProfile(m_identity->profile());
        m_llmAgent->setToolDispatcher(m_toolDispatcher, allowedTools);
    }
}

void AgentRuntime::connectAgentSignals()
{
    connect(m_llmAgent, &LLMAgent::streamDataReceived, this, &AgentRuntime::onStreamDataReceived);
    connect(m_llmAgent, &LLMAgent::finished, this, &AgentRuntime::onFinished);
    connect(m_llmAgent, &LLMAgent::errorOccurred, this, &AgentRuntime::onErrorOccurred);
    connect(m_llmAgent, &LLMAgent::toolCallsStarted, this, &AgentRuntime::onToolCallsStarted);
    connect(m_llmAgent, &LLMAgent::toolEvent, this, &AgentRuntime::onToolEvent);
    connect(m_llmAgent, &LLMAgent::reasoningStarted, this, &AgentRuntime::onReasoningStarted);
    connect(m_llmAgent, &LLMAgent::reasoningStopped, this, &AgentRuntime::onReasoningStopped);
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

void AgentRuntime::onReasoningStarted()
{
    emit reasoningStarted(m_currentSessionId);
}

void AgentRuntime::onReasoningStopped()
{
    emit reasoningStopped(m_currentSessionId);
}
