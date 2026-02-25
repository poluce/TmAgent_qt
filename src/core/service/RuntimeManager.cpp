#include "RuntimeManager.h"
#include "AgentRuntime.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/manager/SessionManager.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/utils/DefaultPrompts.h"
#include "llm/ModelFactory.h"
#include "core/agent/ToolDispatcher.h"
#include <QDebug>
#include <QDir>

RuntimeManager::RuntimeManager(QObject* parent)
    : QObject(parent)
{
}

RuntimeManager::~RuntimeManager()
{
    qDeleteAll(m_runtimes);
    m_runtimes.clear();
}

void RuntimeManager::setModelFactory(ModelFactory* factory) { m_modelFactory = factory; }
void RuntimeManager::setToolDispatcher(ToolDispatcher* dispatcher) { m_toolDispatcher = dispatcher; }
void RuntimeManager::setSessionManager(SessionManager* manager) { m_sessionManager = manager; }
void RuntimeManager::setPersistence(ChatPersistenceService* persistence) { m_persistence = persistence; }

AgentRuntime* RuntimeManager::runtimeForAgent(const QString& agentIdentityId) const
{
    return m_runtimes.value(agentIdentityId, nullptr);
}

AgentRuntime* RuntimeManager::ensureRuntimeForAgent(Identity* agentIdentity)
{
    if (!agentIdentity || !agentIdentity->isAgent())
        return nullptr;

    const QString agentId = agentIdentity->id();
    if (AgentRuntime* existing = m_runtimes.value(agentId, nullptr))
        return existing;

    auto* runtime = new AgentRuntime(agentIdentity, this);
    runtime->setModelFactory(m_modelFactory);
    runtime->setConfig(composeConfigForIdentity(agentIdentity));
    runtime->setToolDispatcher(m_toolDispatcher);
    m_runtimes.insert(agentId, runtime);

    emit runtimeCreated(runtime);
    return runtime;
}

void RuntimeManager::releaseRuntimeIfUnused(const QString& agentIdentityId)
{
    if (agentIdentityId.trimmed().isEmpty())
        return;

    if (m_sessionManager && !m_sessionManager->sessionsForIdentity(agentIdentityId).isEmpty())
        return;

    AgentRuntime* runtime = m_runtimes.take(agentIdentityId);
    if (runtime) {
        if (runtime->isStreaming())
            runtime->abort();
        runtime->deleteLater();
    }
}

void RuntimeManager::setDefaultAgentConfig(const LLMConfig& config)
{
    m_defaultAgentConfig = config;
}

LLMConfig RuntimeManager::defaultAgentConfig() const
{
    return m_defaultAgentConfig;
}

LLMConfig RuntimeManager::composeConfigForIdentity(Identity* identity) const
{
    LLMConfig cfg = m_defaultAgentConfig;
    if (!identity) {
        cfg.systemPrompt = DefaultPrompts::ensureExecutionDiscipline(cfg.systemPrompt);
        return cfg;
    }

    cfg.userName = identity->name();
    cfg.uuid = identity->id();

    if (identity->profile()) {
        const LLMConfig profileCfg = identity->profile()->llmConfig();
        // 新路径优先
        if (!profileCfg.providerInstanceId.trimmed().isEmpty()) {
            cfg.providerInstanceId = profileCfg.providerInstanceId;
            cfg.selectedModelId = profileCfg.selectedModelId;
        }
        // 兼容旧路径
        if (cfg.providerInstanceId.isEmpty() && !profileCfg.configId.trimmed().isEmpty()) {
            cfg.configId = profileCfg.configId;
        }
        if (!identity->profile()->systemPrompt().trimmed().isEmpty())
            cfg.systemPrompt = identity->profile()->systemPrompt().trimmed();
    }

    if (m_persistence) {
        const QString workspacePath = QDir(
                                          m_persistence->agentsDirPath())
                                          .filePath(identity->id().trimmed() + QStringLiteral("/workspace"));
        QDir().mkpath(workspacePath);
        cfg.workspaceDir = workspacePath;
    }
    cfg.systemPrompt = DefaultPrompts::ensureExecutionDiscipline(cfg.systemPrompt);
    return cfg;
}

void RuntimeManager::applyConfigToAllRuntimes()
{
    for (auto it = m_runtimes.begin(); it != m_runtimes.end(); ++it) {
        AgentRuntime* runtime = it.value();
        if (!runtime)
            continue;
        runtime->setConfig(composeConfigForIdentity(runtime->identity()));
    }
}

void RuntimeManager::applyToolDispatcherToAllRuntimes()
{
    if (!m_toolDispatcher)
        return;
    for (AgentRuntime* runtime : m_runtimes) {
        if (runtime)
            runtime->setToolDispatcher(m_toolDispatcher);
    }
}

QHash<QString, AgentRuntime*>& RuntimeManager::runtimes()
{
    return m_runtimes;
}

const QHash<QString, AgentRuntime*>& RuntimeManager::runtimes() const
{
    return m_runtimes;
}
