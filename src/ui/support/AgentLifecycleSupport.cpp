#include "AgentLifecycleSupport.h"

#include "AgentCreateDialog.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include <QDialog>
#include <QMessageBox>

namespace AgentLifecycleSupport {

QString createAgentWithDialog(QWidget* parent, const AgentLifecycleCapabilities& capabilities)
{
    if (!capabilities.governanceCommands
        || !capabilities.governanceQueries
        || !capabilities.modelCatalog
        || !capabilities.sessionCommands)
        return QString();

    const QStringList configIds = capabilities.modelCatalog->registeredModelConfigIds();
    const LLMConfig defaultAgentCfg = capabilities.governanceQueries->defaultAgentConfig();
    const QString defaultConfigId = ModelFactory::resolveConfigKey(defaultAgentCfg);

    AgentCreateDialog dlg(configIds, defaultConfigId, parent);

    {
        QList<AgentCreateDialog::ProviderEntry> providerEntries;
        const QStringList instanceIds = capabilities.modelCatalog->enabledProviderInstanceIds();
        for (const QString& instId : instanceIds) {
            AgentCreateDialog::ProviderEntry entry;
            entry.instanceId = instId;
            entry.displayName = capabilities.modelCatalog->displayNameForProviderInstance(instId);
            providerEntries.append(entry);
        }
        const QString defaultInstId = ModelFactory::resolveInstanceId(defaultAgentCfg);
        dlg.setProviderEntries(providerEntries, defaultInstId);

        QObject::connect(&dlg, &AgentCreateDialog::providerChanged, &dlg, [capabilities](const QString& instId) {
            if (!instId.isEmpty())
                capabilities.modelCatalog->fetchModelsForProviderInstanceAsync(instId);
        });

        if (capabilities.subscribeModelCatalogUpdated) {
            capabilities.subscribeModelCatalogUpdated(&dlg, [&dlg, capabilities, &defaultAgentCfg](const QString& instId) {
                const QList<AvailableModel> cached = capabilities.modelCatalog->cachedModelsForProviderInstance(instId);
                QList<AgentCreateDialog::ModelEntry> modelEntries;
                for (const AvailableModel& am : cached) {
                    AgentCreateDialog::ModelEntry me;
                    me.modelId = am.modelId;
                    me.displayName = am.displayName;
                    modelEntries.append(me);
                }
                dlg.setModelEntries(instId, modelEntries, defaultAgentCfg.selectedModelId);
            });
        }

        for (const QString& instId : instanceIds) {
            const QList<AvailableModel> cached = capabilities.modelCatalog->cachedModelsForProviderInstance(instId);
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
            capabilities.modelCatalog->fetchModelsForProviderInstanceAsync(currentInstId);
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

    {
        QStringList toolNames = capabilities.modelCatalog->registeredToolNames();
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

    capabilities.sessionCommands->createSessionForIdentity(agent->id(), name);
    return agent->id();
}

bool deleteAgentWithConfirmation(QWidget* parent,
                                 ISessionCommands* sessionCommands,
                                 IMemoryCommands* memoryCommands,
                                 IWorkspacePersistence* workspacePersistence,
                                 const QString& agentIdentityId)
{
    const QString trimmedId = agentIdentityId.trimmed();
    if (!sessionCommands || !memoryCommands || !workspacePersistence || trimmedId.isEmpty())
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
        sessionCommands->removeSessionAs(userId, sessionId);

    if (!memoryCommands->removeAgentMemoryAs(userId, trimmedId)) {
        QMessageBox::warning(
            parent,
            QObject::tr("删除助手失败"),
            QObject::tr("助手“%1”的记忆目录删除失败，已中止删除操作。请检查目录权限后重试。")
                .arg(agentName));
        return false;
    }

    if (!identityMgr->removeAgent(trimmedId))
        return false;

    workspacePersistence->saveSessionsToDisk();
    return true;
}

} // namespace AgentLifecycleSupport
