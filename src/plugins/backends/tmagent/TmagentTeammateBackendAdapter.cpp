#include "TmagentTeammateBackendAdapter.h"
#include "core/agent/LLMAgent.h"
#include "core/agent/ToolDispatcher.h"
#include "core/manager/IdentityManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Teammate.h"
#include "core/model/TeammateRuntimeAccess.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/tools/AgentToolNames.h"
#include "llm/ModelFactory.h"
#include <QDir>
#include <QUuid>

TmagentTeammateBackendAdapter::TmagentTeammateBackendAdapter(QObject* parent)
    : QObject(parent)
{
}

TmagentTeammateBackendAdapter::~TmagentTeammateBackendAdapter()
{
    shutdown();
}

QString TmagentTeammateBackendAdapter::backendId() const
{
    return QStringLiteral("tmagent");
}

bool TmagentTeammateBackendAdapter::ensureReady(QString* error)
{
    if (error)
        error->clear();
    return ModelFactory::instance() != nullptr && ToolDispatcher::instance() != nullptr;
}

bool TmagentTeammateBackendAdapter::isReady() const
{
    return true;
}

QString TmagentTeammateBackendAdapter::defaultWorkspaceFor(
    const QString& teammateId,
    const QString& ownerAgentId) const
{
    ChatPersistenceService persistence;
    return QDir(QDir(persistence.agentsDirPath()).filePath(ownerAgentId))
        .filePath(QStringLiteral("teammates/%1/workspace").arg(teammateId));
}

QStringList TmagentTeammateBackendAdapter::allowedToolsForOwner(const QString& ownerAgentId) const
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

TmAgent::ITeammateBackend::CreateResult TmagentTeammateBackendAdapter::createSession(
    const QString& teammateId,
    const TmAgent::TeammateConfig& config)
{
    CreateResult out;
    
    if (teammateId.isEmpty()) {
        out.error = QStringLiteral("队友 ID 为空");
        return out;
    }
    
    if (m_sessions.contains(teammateId) && m_sessions.value(teammateId).agent) {
        out.success = true;
        out.threadId = QStringLiteral("tmagent:%1").arg(teammateId);
        return out;
    }

    Identity* owner = IdentityManager::instance()->findById(config.ownerAgentId);
    if (!owner || !owner->profile()) {
        out.error = QStringLiteral("未找到创建者助手或其配置");
        return out;
    }

    auto* agent = new LLMAgent(this);
    agent->setModelFactory(ModelFactory::instance());

    LLMConfig llmConfig = owner->profile()->llmConfig();
    llmConfig.uuid = teammateId;
    llmConfig.userName = config.name;
    llmConfig.workspaceDir = config.workingDirectory.trimmed().isEmpty()
        ? defaultWorkspaceFor(teammateId, config.ownerAgentId)
        : config.workingDirectory.trimmed();
    
    if (!QDir().mkpath(llmConfig.workspaceDir)) {
        out.error = QStringLiteral("无法创建队友工作区: %1").arg(llmConfig.workspaceDir);
        agent->deleteLater();
        return out;
    }
    
    if (!config.role.trimmed().isEmpty())
        llmConfig.systemPrompt = config.role.trimmed();
    
    agent->setConfig(llmConfig);
    if (!llmConfig.systemPrompt.trimmed().isEmpty())
        agent->setSystemPrompt(llmConfig.systemPrompt);

    ToolDispatcher* dispatcher = ToolDispatcher::instance();
    if (dispatcher)
        agent->setToolDispatcher(dispatcher, allowedToolsForOwner(config.ownerAgentId));

    // 创建临时 Teammate 对象用于信号连接（SDK 不使用 Teammate 对象）
    Teammate* mate = new Teammate(this);
    mate->setId(teammateId);
    mate->setName(config.name);
    mate->setRole(config.role);
    mate->setBackend(config.backend);
    mate->setOwnerAgentId(config.ownerAgentId);
    mate->setWorkingDirectory(llmConfig.workspaceDir);
    TeammateRuntimeAccess::setThreadId(mate, QStringLiteral("tmagent:%1").arg(teammateId));
    TeammateRuntimeAccess::setStatus(mate, Teammate::Status::Idle);

    SessionState state;
    state.agent = agent;
    state.teammate = mate;

    connect(agent, &LLMAgent::streamDataReceived, this, [this, teammateId](const QString& chunk) {
        auto it = m_sessions.find(teammateId);
        if (it == m_sessions.end())
            return;
        if (it->teammate)
            emit it->teammate->messageDelta(it->activeTurnId, chunk);
    });

    connect(agent, &LLMAgent::finished, this, [this, teammateId](const QString& content) {
        auto it = m_sessions.find(teammateId);
        if (it == m_sessions.end())
            return;
        const QString turnId = it->activeTurnId;
        it->activeTurnId.clear();
        
        if (it->teammate) {
            TeammateRuntimeAccess::setActiveTurnId(it->teammate, QString());
            TeammateRuntimeAccess::setStatus(it->teammate, Teammate::Status::Idle);
            TeammateRuntimeAccess::setLastError(it->teammate, QString());
            TeammateRuntimeAccess::incrementTurnCount(it->teammate);
            TeammateRuntimeAccess::touchLastActive(it->teammate);
            emit it->teammate->turnCompleted(turnId, true, content);
        }
    });

    connect(agent, &LLMAgent::errorOccurred, this, [this, teammateId](const QString& errorMsg) {
        auto it = m_sessions.find(teammateId);
        if (it == m_sessions.end())
            return;
        const QString turnId = it->activeTurnId;
        it->activeTurnId.clear();
        
        if (it->teammate) {
            TeammateRuntimeAccess::setActiveTurnId(it->teammate, QString());
            TeammateRuntimeAccess::setStatus(it->teammate, Teammate::Status::Error);
            TeammateRuntimeAccess::setLastError(it->teammate, errorMsg);
            TeammateRuntimeAccess::incrementTurnCount(it->teammate);
            TeammateRuntimeAccess::touchLastActive(it->teammate);
            emit it->teammate->turnCompleted(turnId, false, errorMsg);
        }
    });

    m_sessions.insert(teammateId, state);
    out.success = true;
    out.threadId = QStringLiteral("tmagent:%1").arg(teammateId);
    return out;
}

TmAgent::ITeammateBackend::SendResult TmagentTeammateBackendAdapter::sendMessage(
    const QString& teammateId,
    const QString& text)
{
    SendResult out;
    auto it = m_sessions.find(teammateId);
    
    if (it == m_sessions.end() || !it->agent) {
        out.error = QStringLiteral("队友会话尚未建立");
        return out;
    }
    
    if (!it->activeTurnId.trimmed().isEmpty()) {
        out.error = QStringLiteral("队友当前已有运行中的任务");
        return out;
    }

    const QString turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    it->activeTurnId = turnId;
    
    if (it->teammate) {
        TeammateRuntimeAccess::setActiveTurnId(it->teammate, turnId);
        TeammateRuntimeAccess::setStatus(it->teammate, Teammate::Status::Busy);
        TeammateRuntimeAccess::setLastError(it->teammate, QString());
        TeammateRuntimeAccess::touchLastActive(it->teammate);
        emit it->teammate->turnStarted(turnId);
    }
    
    it->agent->sendMessage(text);
    out.success = true;
    out.turnId = turnId;
    return out;
}

bool TmagentTeammateBackendAdapter::cancelTurn(const QString& teammateId, QString* error)
{
    if (error)
        error->clear();
    
    auto it = m_sessions.find(teammateId);
    if (it == m_sessions.end() || !it->agent) {
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
    
    if (it->teammate) {
        TeammateRuntimeAccess::setActiveTurnId(it->teammate, QString());
        TeammateRuntimeAccess::setStatus(it->teammate, Teammate::Status::Idle);
        TeammateRuntimeAccess::setLastError(it->teammate, QString());
        TeammateRuntimeAccess::touchLastActive(it->teammate);
        emit it->teammate->turnCompleted(turnId, false, QStringLiteral("已取消"));
    }
    
    return true;
}

void TmagentTeammateBackendAdapter::destroySession(const QString& teammateId)
{
    auto it = m_sessions.find(teammateId);
    if (it == m_sessions.end())
        return;
    
    if (it->agent) {
        it->agent->abort();
        it->agent->deleteLater();
    }
    
    if (it->teammate) {
        TeammateRuntimeAccess::setActiveTurnId(it->teammate, QString());
        TeammateRuntimeAccess::setStatus(it->teammate, Teammate::Status::Shutdown);
        it->teammate->deleteLater();
    }
    
    m_sessions.erase(it);
}

void TmagentTeammateBackendAdapter::shutdown()
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
        
        if (it->teammate) {
            it->teammate->deleteLater();
        }
    }
    m_sessions.clear();
}
