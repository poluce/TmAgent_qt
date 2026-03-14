#include "AgentLifecycleSupport.h"

#include "AgentCreateDialog.h"
#include "core/agent/ToolDispatcher.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/service/ChatService.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include <QDialog>
#include <QMessageBox>

namespace AgentLifecycleSupport {

QString createAgentWithDialog(QWidget* parent, ChatService* chatService)
{
    if (!chatService)
        return QString();

    QStringList configIds;
    ModelFactory* factory = chatService->modelFactory();
    if (factory)
        configIds = factory->registeredConfigIds();
    const LLMConfig defaultAgentCfg = chatService->defaultAgentConfig();
    const QString defaultConfigId = ModelFactory::resolveConfigKey(defaultAgentCfg);

    AgentCreateDialog dlg(configIds, defaultConfigId, parent);

    if (factory) {
        QList<AgentCreateDialog::ProviderEntry> providerEntries;
        const QStringList instanceIds = factory->enabledInstanceIds();
        for (const QString& instId : instanceIds) {
            AgentCreateDialog::ProviderEntry entry;
            entry.instanceId = instId;
            entry.displayName = factory->displayNameForInstance(instId);
            providerEntries.append(entry);
        }
        const QString defaultInstId = ModelFactory::resolveInstanceId(defaultAgentCfg);
        dlg.setProviderEntries(providerEntries, defaultInstId);

        QObject::connect(&dlg, &AgentCreateDialog::providerChanged, &dlg, [factory](const QString& instId) {
            if (!instId.isEmpty())
                factory->fetchModelsAsync(instId);
        });

        QObject::connect(factory, &ModelFactory::modelCacheUpdated, &dlg, [&dlg, factory, &defaultAgentCfg](const QString& instId) {
            const QList<AvailableModel> cached = factory->cachedModels(instId);
            QList<AgentCreateDialog::ModelEntry> modelEntries;
            for (const AvailableModel& am : cached) {
                AgentCreateDialog::ModelEntry me;
                me.modelId = am.modelId;
                me.displayName = am.displayName;
                modelEntries.append(me);
            }
            dlg.setModelEntries(instId, modelEntries, defaultAgentCfg.selectedModelId);
        });

        for (const QString& instId : instanceIds) {
            const QList<AvailableModel> cached = factory->cachedModels(instId);
            QList<AgentCreateDialog::ModelEntry> modelEntries;
            for (const AvailableModel& am : cached) {
                AgentCreateDialog::ModelEntry me;
                me.modelId = am.modelId;
                me.displayName = am.displayName;
                modelEntries.append(me);
            }
            dlg.setModelEntries(instId, modelEntries, defaultAgentCfg.selectedModelId);
        }

        const QString currentInstId = dlg.providerInstanceId();
        if (!currentInstId.isEmpty())
            factory->fetchModelsAsync(currentInstId);
    }

    if (dlg.exec() != QDialog::Accepted)
        return QString();

    const QString name = dlg.agentName();
    const QString prompt = dlg.systemPrompt();
    const QString roleName = dlg.roleName();
    const QString avatarPath = dlg.avatarPath();
    const QString selectedConfigId = dlg.configId();
    const QString selectedInstanceId = dlg.providerInstanceId();
    const QString selectedModelId = dlg.selectedModelId();
    const bool delegationEnabled = dlg.delegationEnabled();

    auto* profile = new IdentityProfile();
    LLMConfig agentCfg = defaultAgentCfg;
    if (!selectedInstanceId.isEmpty()) {
        agentCfg.providerInstanceId = selectedInstanceId;
        agentCfg.configId = selectedInstanceId;
    } else if (!selectedConfigId.isEmpty()) {
        agentCfg.configId = selectedConfigId;
    }
    if (!selectedModelId.isEmpty())
        agentCfg.selectedModelId = selectedModelId;
    if (!prompt.isEmpty())
        agentCfg.systemPrompt = prompt;
    profile->setLlmConfig(agentCfg);
    if (!prompt.isEmpty())
        profile->setSystemPrompt(prompt);
    if (!roleName.isEmpty())
        profile->setDescription(roleName);
    profile->setDelegateEnabled(delegationEnabled);

    if (ToolDispatcher* dispatcher = chatService->toolDispatcher()) {
        QStringList toolNames;
        const QList<Tool> tools = dispatcher->getAllToolSchemas();
        for (const Tool& tool : tools) {
            const QString toolName = tool.name.trimmed();
            if (!toolName.isEmpty())
                toolNames.append(toolName);
        }
        if (delegationEnabled && !toolNames.contains(QStringLiteral("delegate_task")))
            toolNames.append(QStringLiteral("delegate_task"));
        if (!delegationEnabled)
            toolNames.removeAll(QStringLiteral("delegate_task"));
        toolNames.removeDuplicates();
        profile->setAllowedTools(toolNames);
    }

    Identity* agent = IdentityManager::instance()->createAgent(name, profile);
    if (!agent)
        return QString();
    if (!avatarPath.isEmpty())
        agent->setAvatar(avatarPath);

    chatService->createSessionForIdentity(agent->id(), name);
    return agent->id();
}

bool deleteAgentWithConfirmation(QWidget* parent, ChatService* chatService, const QString& agentIdentityId)
{
    const QString trimmedId = agentIdentityId.trimmed();
    if (!chatService || trimmedId.isEmpty())
        return false;

    IdentityManager* identityMgr = IdentityManager::instance();
    Identity* agent = identityMgr ? identityMgr->findById(trimmedId) : nullptr;
    if (!agent || !agent->isAgent())
        return false;

    const QString agentName = agent->name().trimmed().isEmpty()
        ? QObject::tr("未命名助手")
        : agent->name().trimmed();
    const QList<Session*> agentSessions = SessionManager::instance()->sessionsForIdentity(trimmedId);

    const QMessageBox::StandardButton confirm = QMessageBox::warning(
        parent,
        QObject::tr("删除助手"),
        QObject::tr("将删除助手“%1”，并删除其相关会话（%2 个）。\n此操作不可恢复，是否继续？")
            .arg(agentName)
            .arg(agentSessions.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (confirm != QMessageBox::Yes)
        return false;

    const QString userId = identityMgr->userIdentity()->id();
    QStringList sessionIds;
    for (Session* session : agentSessions) {
        if (session)
            sessionIds.append(session->id());
    }
    sessionIds.removeDuplicates();
    for (const QString& sessionId : sessionIds)
        chatService->removeSessionAs(userId, sessionId);

    if (!chatService->removeAgentMemoryAs(userId, trimmedId)) {
        QMessageBox::warning(
            parent,
            QObject::tr("删除助手失败"),
            QObject::tr("助手“%1”的记忆目录删除失败，已中止删除操作。请检查目录权限后重试。")
                .arg(agentName));
        return false;
    }

    if (!identityMgr->removeAgent(trimmedId))
        return false;

    chatService->saveSessionsToDisk();
    return true;
}

} // namespace AgentLifecycleSupport
