#include "TmagentTeammateBackend.h"

#include "core/agent/LLMAgent.h"
#include "core/agent/ToolDispatcher.h"
#include "core/manager/IdentityManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/TeammateRuntimeAccess.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/tools/AgentToolNames.h"
#include "llm/ModelFactory.h"

#include <QDir>
#include <QList>
#include <QUuid>

TmagentTeammateBackend::TmagentTeammateBackend(QObject* parent)
    : QObject(parent)
{
}

TmagentTeammateBackend::~TmagentTeammateBackend()
{
    shutdown();
}

bool TmagentTeammateBackend::ensureReady(QString* error)
{
    if (error)
        error->clear();
    return ModelFactory::instance() != nullptr && ToolDispatcher::instance() != nullptr;
}

bool TmagentTeammateBackend::isReady() const
{
    return true;
}

QString TmagentTeammateBackend::defaultWorkspaceFor(const Teammate* mate) const
{
    if (!mate)
        return QString();
    ChatPersistenceService persistence;
    return QDir(QDir(persistence.agentsDirPath()).filePath(mate->ownerAgentId()))
        .filePath(QStringLiteral("teammates/%1/workspace").arg(mate->id()));
}

QStringList TmagentTeammateBackend::allowedToolsForOwner(const QString& ownerAgentId) const
{
    QStringList allowedTools;
    Identity* owner = IdentityManager::instance()->findById(ownerAgentId);
    if (owner && owner->profile())
        allowedTools = owner->profile()->allowedTools();

    ToolDispatcher* dispatcher = ToolDispatcher::instance();
    if (dispatcher && allowedTools.isEmpty()) {
        const QList<Tool> tools = dispatcher->getAllToolSchemas();
        for (const Tool& tool : tools) {
            const QString name = tool.name.trimmed();
            if (!name.isEmpty())
                allowedTools.append(name);
        }
    }

    for (const QString& teamTool : AgentToolNames::all())
        allowedTools.removeAll(teamTool);
    allowedTools.removeDuplicates();
    return allowedTools;
}

ITeammateBackend::CreateResult TmagentTeammateBackend::createSession(Teammate* mate)
{
    CreateResult out;
    if (!mate) {
        out.error = QStringLiteral("队友为空");
        return out;
    }
    if (m_sessions.contains(mate->id()) && m_sessions.value(mate->id()).agent) {
        out.success = true;
        out.threadId = mate->threadId();
        return out;
    }

    Identity* owner = IdentityManager::instance()->findById(mate->ownerAgentId());
    if (!owner || !owner->profile()) {
        out.error = QStringLiteral("未找到创建者助手或其配置");
        return out;
    }

    auto* agent = new LLMAgent(this);
    agent->setModelFactory(ModelFactory::instance());

    LLMConfig config = owner->profile()->llmConfig();
    config.uuid = mate->id();
    config.userName = mate->name();
    config.workspaceDir = mate->workingDirectory().trimmed().isEmpty()
        ? defaultWorkspaceFor(mate)
        : mate->workingDirectory().trimmed();
    if (!QDir().mkpath(config.workspaceDir)) {
        out.error = QStringLiteral("无法创建队友工作区: %1").arg(config.workspaceDir);
        agent->deleteLater();
        return out;
    }
    if (!mate->role().trimmed().isEmpty())
        config.systemPrompt = mate->role().trimmed();
    agent->setConfig(config);
    if (!config.systemPrompt.trimmed().isEmpty())
        agent->setSystemPrompt(config.systemPrompt);

    ToolDispatcher* dispatcher = ToolDispatcher::instance();
    if (dispatcher)
        agent->setToolDispatcher(dispatcher, allowedToolsForOwner(mate->ownerAgentId()));

    SessionState state;
    state.agent = agent;

    connect(agent, &LLMAgent::streamDataReceived, agent, [this, mate](const QString& chunk) {
        auto it = m_sessions.find(mate->id());
        if (it == m_sessions.end())
            return;
        TeammateRuntimeAccess::touchLastActive(mate);
        emit mate->messageDelta(it->activeTurnId, chunk);
    });

    connect(agent, &LLMAgent::finished, agent, [this, mate](const QString& content) {
        auto it = m_sessions.find(mate->id());
        if (it == m_sessions.end())
            return;
        const QString turnId = it->activeTurnId;
        it->activeTurnId.clear();
        TeammateRuntimeAccess::setActiveTurnId(mate, QString());
        TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Idle);
        TeammateRuntimeAccess::setLastError(mate, QString());
        TeammateRuntimeAccess::incrementTurnCount(mate);
        TeammateRuntimeAccess::touchLastActive(mate);
        emit mate->turnCompleted(turnId, true, content);
    });

    connect(agent, &LLMAgent::errorOccurred, agent, [this, mate](const QString& errorMsg) {
        auto it = m_sessions.find(mate->id());
        if (it == m_sessions.end())
            return;
        const QString turnId = it->activeTurnId;
        it->activeTurnId.clear();
        TeammateRuntimeAccess::setActiveTurnId(mate, QString());
        TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Error);
        TeammateRuntimeAccess::setLastError(mate, errorMsg);
        TeammateRuntimeAccess::incrementTurnCount(mate);
        TeammateRuntimeAccess::touchLastActive(mate);
        emit mate->turnCompleted(turnId, false, errorMsg);
    });

    m_sessions.insert(mate->id(), state);
    TeammateRuntimeAccess::setThreadId(mate, QStringLiteral("tmagent:%1").arg(mate->id()));
    out.success = true;
    out.threadId = mate->threadId();
    return out;
}

ITeammateBackend::SendResult TmagentTeammateBackend::sendMessage(Teammate* mate, const QString& text)
{
    SendResult out;
    auto it = m_sessions.find(mate ? mate->id() : QString());
    if (!mate || it == m_sessions.end() || !it->agent) {
        out.error = QStringLiteral("队友会话尚未建立");
        return out;
    }
    if (!it->activeTurnId.trimmed().isEmpty()) {
        out.error = QStringLiteral("队友当前已有运行中的任务");
        return out;
    }

    const QString turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    it->activeTurnId = turnId;
    TeammateRuntimeAccess::setActiveTurnId(mate, turnId);
    TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Busy);
    TeammateRuntimeAccess::setLastError(mate, QString());
    TeammateRuntimeAccess::touchLastActive(mate);
    emit mate->turnStarted(turnId);
    it->agent->sendMessage(text);
    out.success = true;
    out.turnId = turnId;
    return out;
}

bool TmagentTeammateBackend::cancelTurn(Teammate* mate, QString* error)
{
    if (error)
        error->clear();
    auto it = m_sessions.find(mate ? mate->id() : QString());
    if (!mate || it == m_sessions.end() || !it->agent) {
        if (error)
            *error = QStringLiteral("队友会话尚未建立");
        return false;
    }
    if (it->activeTurnId.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("队友当前没有运行中的任务");
        return false;
    }

    const QString turnId = it->activeTurnId;
    it->agent->abortAndRollback();
    it->activeTurnId.clear();
    TeammateRuntimeAccess::setActiveTurnId(mate, QString());
    TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Idle);
    TeammateRuntimeAccess::setLastError(mate, QString());
    TeammateRuntimeAccess::touchLastActive(mate);
    emit mate->turnCompleted(turnId, false, QStringLiteral("已取消"));
    return true;
}

void TmagentTeammateBackend::destroySession(Teammate* mate)
{
    auto it = m_sessions.find(mate ? mate->id() : QString());
    if (it == m_sessions.end())
        return;
    if (it->agent) {
        it->agent->abort();
        it->agent->deleteLater();
    }
    m_sessions.erase(it);
    if (mate) {
        TeammateRuntimeAccess::setActiveTurnId(mate, QString());
        TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Shutdown);
    }
}

void TmagentTeammateBackend::shutdown()
{
    const QList<QString> ids = m_sessions.keys();
    for (const QString& id : ids) {
        auto it = m_sessions.find(id);
        if (it == m_sessions.end())
            continue;
        if (it->agent) {
            it->agent->abort();
            it->agent->deleteLater();
        }
    }
    m_sessions.clear();
}
