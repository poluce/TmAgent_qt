#include "AgentLifecycleSupport.h"

#include "AgentCreateDialog.h"
#include "core/tools/AgentToolNames.h"
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

QString createAgentWithDialog(QWidget* parent, IAppFacade& app)
{
    auto* governance = &app.governance();
    auto* workspace = &app.workspace();
    AppEventHub* events = app.events();
    if (!governance || !workspace)
        return QString();

    const QStringList configIds = governance->registeredModelConfigIds();
    const LLMConfig defaultAgentCfg = governance->defaultAgentConfig();
    const QString defaultConfigId = ModelFactory::resolveConfigKey(defaultAgentCfg);

    AgentCreateDialog dlg(configIds, defaultConfigId, parent);

    {
        QList<AgentCreateDialog::ProviderEntry> providerEntries;
        const QStringList instanceIds = governance->enabledProviderInstanceIds();
        for (const QString& instId : instanceIds) {
            AgentCreateDialog::ProviderEntry entry;
            entry.instanceId = instId;
            entry.displayName = governance->displayNameForProviderInstance(instId);
            providerEntries.append(entry);
        }
        const QString defaultInstId = ModelFactory::resolveInstanceId(defaultAgentCfg);
        dlg.setProviderEntries(providerEntries, defaultInstId);

        QObject::connect(&dlg, &AgentCreateDialog::providerChanged, &dlg, [governance](const QString& instId) {
            if (!instId.isEmpty())
                governance->fetchModelsForProviderInstanceAsync(instId);
        });

        if (events) {
            QObject::connect(events, &AppEventHub::modelCatalogUpdated, &dlg, [&dlg, governance, defaultAgentCfg](const QString& instId) {
                const QList<AvailableModel> cached = governance->cachedModelsForProviderInstance(instId);
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
            const QList<AvailableModel> cached = governance->cachedModelsForProviderInstance(instId);
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
            governance->fetchModelsForProviderInstanceAsync(currentInstId);

        dlg.setToolPluginInfos(governance->toolPluginInfos());
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
    QStringList toolNames = dlg.allowedTools();
    toolNames.removeDuplicates();
    if (!delegationEnabled) {
        for (const QString& name : AgentToolNames::all())
            toolNames.removeAll(name);
    }

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
    profile->setAllowedTools(toolNames);

    Identity* agent = IdentityManager::instance()->createAgent(name, profile);
    if (!agent)
        return QString();
    if (!avatarPath.isEmpty())
        agent->setAvatar(avatarPath);

    workspace->createSessionForIdentity(agent->id(), name);
    return agent->id();
}

bool deleteAgentWithConfirmation(QWidget* parent, IAppFacade& app, const QString& agentIdentityId)
{
    auto* sessionCommands = &app.workspace();
    auto* memoryCommands = &app.memory();
    auto* workspacePersistence = &app.workspace();
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
