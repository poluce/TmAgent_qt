#include "MemoryService.h"

#include "ApplicationServices.h"
#include "ConversationService.h"
#include "GovernanceService.h"
#include "WorkspaceService.h"
#include "AgentPulse.h"
#include "AgentPulseRegistry.h"
#include "BackgroundTaskCoordinator.h"
#include "ChatCoordinatorFactory.h"
#include "ChatCoordinatorSupport.h"
#include "HealthMonitor.h"
#include "HeartbeatDispatchCoordinator.h"
#include "HeartbeatPromptBuilder.h"
#include "HeartbeatService.h"
#include "HeartbeatSnapshotCoordinator.h"
#include "HeartbeatStateStore.h"
#include "MemoryMaintenanceService.h"
#include "MemoryToolWriteService.h"
#include "SchedulerService.h"
#include "ConfigService.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/memory/MemoryManager.h"
#include "core/model/Identity.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "llm/ModelFactory.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace {
using ChatCoordinatorSupport::pulseStateToString;

QString normalizeHeartbeatSignalCompat(const QString& raw)
{
    const QString s = raw.trimmed().toLower();
    if (s == QLatin1String("provider") || s == QLatin1String("provider_status"))
        return QStringLiteral("provider_status");
    if (s == QLatin1String("delegate") || s == QLatin1String("delegate_jobs"))
        return QStringLiteral("delegate_jobs");
    if (s == QLatin1String("pulse") || s == QLatin1String("pulse_state"))
        return QStringLiteral("pulse_state");
    if (s == QLatin1String("scheduler") || s == QLatin1String("scheduler_jobs"))
        return QStringLiteral("scheduler_jobs");
    if (s == QLatin1String("memory") || s == QLatin1String("memory_progress"))
        return QStringLiteral("memory_progress");
    return s;
}

QStringList normalizeHeartbeatSignalsCompat(const QStringList& input)
{
    QStringList out;
    for (const QString& raw : input) {
        const QString s = normalizeHeartbeatSignalCompat(raw);
        if (s.isEmpty())
            continue;
        if (!out.contains(s))
            out.append(s);
    }
    if (out.isEmpty()) {
        out << QStringLiteral("provider_status")
            << QStringLiteral("delegate_jobs")
            << QStringLiteral("pulse_state");
    }
    return out;
}
} // namespace

MemoryService::MemoryService(ApplicationServices& app)
    : m_app(app)
    , m_memoryManager(new MemoryManager(app.m_persistence.get()))
    , m_healthMonitor(new HealthMonitor(&app))
    , m_heartbeatService(new HeartbeatService(&app))
    , m_schedulerService(new SchedulerService(&app))
    , m_agentPulseRegistry(new AgentPulseRegistry(
          AgentPulseRegistry::Dependencies {
              &app,
              &m_agentPulses,
              [](AgentPulse::State state) { return pulseStateToString(state); },
              [this](const QString& changedAgentId, const QString& stateText) {
                  QJsonObject extra;
                  extra.insert(QStringLiteral("agent_id"), changedAgentId);
                  extra.insert(QStringLiteral("state"), stateText);
                  if (m_app.m_conversationService) {
                      m_app.m_conversationService->emitPipelineEvent(
                          QStringLiteral("pulse.state_changed"),
                          QString(),
                          nullptr,
                          QString(),
                          QString(),
                          extra);
                  }
              },
              [this](const QString& changedAgentId) {
                  QJsonObject extra;
                  extra.insert(QStringLiteral("agent_id"), changedAgentId);
                  if (m_app.m_conversationService) {
                      m_app.m_conversationService->emitPipelineEvent(
                          QStringLiteral("pulse.hard_timeout"),
                          QString(),
                          nullptr,
                          QString(),
                          QStringLiteral("agent_no_progress"),
                          extra);
                  }
              }
          }))
{
}

MemoryService::~MemoryService() = default;

bool MemoryService::removeAgentMemoryAs(const QString& actorIdentityId,
                                        const QString& agentIdentityId)
{
    if (!m_app.m_workspaceService
        || !m_app.m_workspaceService->canIdentityManageSessions(actorIdentityId)) {
        qWarning() << "[ApplicationServices] 拒绝删除 Agent 记忆目录，actor 无权限:"
                   << actorIdentityId << "agent:" << agentIdentityId;
        return false;
    }
    if (!m_memoryManager)
        return true;

    QString err;
    const bool ok = m_memoryManager->removeAgentMemory(agentIdentityId, &err);
    if (ok) {
        m_memoryRetainedTurnsByAgent.remove(agentIdentityId.trimmed());
        m_heartbeatRuntimeByAgent.remove(agentIdentityId.trimmed());
    }
    if (!ok)
        qWarning() << "[ApplicationServices] 删除 Agent 记忆目录失败:" << agentIdentityId << err;
    return ok;
}

bool MemoryService::rememberMessageAs(const QString& actorIdentityId,
                                      const QString& sessionId,
                                      const QString& messageId,
                                      const QString& fallbackContent,
                                      QString* error)
{
    if (error)
        error->clear();
    if (!m_app.m_workspaceService
        || !m_app.m_workspaceService->canIdentityManageGlobalConfig(actorIdentityId)) {
        if (error)
            *error = QStringLiteral("actor has no permission");
        return false;
    }
    if (!m_memoryManager) {
        if (error)
            *error = QStringLiteral("memory manager unavailable");
        return false;
    }

    Session* session = m_app.m_sessionManager ? m_app.m_sessionManager->findById(sessionId) : nullptr;
    if (!session) {
        if (error)
            *error = QStringLiteral("session not found");
        return false;
    }

    const QString agentId = m_app.m_conversationService
        ? m_app.m_conversationService->agentIdentityIdForSession(sessionId)
        : QString();
    if (agentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent identity not found for session");
        return false;
    }

    QString selectedText;
    QString selectedTurnId;
    QString selectedTraceId;
    const QString trimmedMessageId = messageId.trimmed();
    const QString fallback = fallbackContent.trimmed();
    const QList<Message> allMessages = session->allMessages();

    auto findMessage = [&](auto&& match) -> bool {
        for (int i = allMessages.size() - 1; i >= 0; --i) {
            const Message& msg = allMessages.at(i);
            if (msg.content.type != MessageContent::Type::Text
                && msg.content.type != MessageContent::Type::System) {
                continue;
            }
            if (!match(msg))
                continue;
            selectedText = msg.content.text.trimmed();
            selectedTurnId = msg.turnId.trimmed();
            selectedTraceId = msg.traceId.trimmed();
            return true;
        }
        return false;
    };

    if (!trimmedMessageId.isEmpty())
        findMessage([&](const Message& msg) { return msg.id == trimmedMessageId; });
    if (selectedText.isEmpty() && !fallback.isEmpty())
        findMessage([&](const Message& msg) { return msg.content.text.trimmed() == fallback; });
    if (selectedText.isEmpty())
        selectedText = fallback;
    if (selectedText.isEmpty()) {
        if (error)
            *error = QStringLiteral("message content is empty");
        return false;
    }

    QString memorySummary;
    QString memoryPath;
    QJsonObject memoryMetadata;
    QString memoryError;
    const bool ok = m_memoryManager->rememberManual(agentId,
                                                    sessionId,
                                                    selectedTurnId,
                                                    selectedTraceId,
                                                    selectedText,
                                                    &memorySummary,
                                                    &memoryPath,
                                                    &memoryMetadata,
                                                    &memoryError);
    TurnTask* activeTurn = m_app.m_conversationService
        ? m_app.m_conversationService->turnManager().activeTurn(sessionId)
        : nullptr;
    TurnTask syntheticTurn;
    const TurnTask* eventTurn = activeTurn;
    if (!eventTurn && (!selectedTurnId.isEmpty() || !selectedTraceId.isEmpty())) {
        syntheticTurn.turnId = selectedTurnId;
        syntheticTurn.requestTraceId = selectedTraceId;
        syntheticTurn.runId = QStringLiteral("manual_remember");
        syntheticTurn.actorIdentityId = actorIdentityId;
        eventTurn = &syntheticTurn;
    }
    if (!ok) {
        QJsonObject memoryExtra;
        memoryExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        memoryExtra.insert(QStringLiteral("path"), memoryPath);
        memoryExtra.insert(QStringLiteral("manualRemember"), true);
        if (!selectedTraceId.isEmpty())
            memoryExtra.insert(QStringLiteral("source_trace_id"), selectedTraceId);
        if (!selectedTurnId.isEmpty())
            memoryExtra.insert(QStringLiteral("source_turn_id"), selectedTurnId);
        if (m_app.m_conversationService) {
            m_app.m_conversationService->emitPipelineEvent(
                QStringLiteral("memory.error"),
                sessionId,
                eventTurn,
                QString(),
                memoryError.isEmpty() ? QStringLiteral("manual remember failed") : memoryError,
                memoryExtra);
        }
        if (error) {
            *error = memoryError.isEmpty() ? QStringLiteral("manual remember failed") : memoryError;
        }
        return false;
    }

    QJsonObject updateExtra;
    updateExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
    updateExtra.insert(QStringLiteral("summary"), memorySummary);
    updateExtra.insert(QStringLiteral("path"), memoryPath);
    updateExtra.insert(QStringLiteral("manualRemember"), true);
    if (!selectedTraceId.isEmpty())
        updateExtra.insert(QStringLiteral("source_trace_id"), selectedTraceId);
    if (!selectedTurnId.isEmpty())
        updateExtra.insert(QStringLiteral("source_turn_id"), selectedTurnId);
    for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
        updateExtra.insert(it.key(), it.value());
    if (m_app.m_conversationService) {
        m_app.m_conversationService->emitPipelineEvent(
            QStringLiteral("memory.updated"),
            sessionId,
            eventTurn,
            QString(),
            QString(),
            updateExtra);
    }

    const int compactedCount = memoryMetadata.value(QStringLiteral("compacted_count")).toInt();
    if (compactedCount > 0 && m_app.m_conversationService) {
        QJsonObject compactExtra;
        compactExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        compactExtra.insert(QStringLiteral("summary"), memorySummary);
        compactExtra.insert(QStringLiteral("compacted_count"), compactedCount);
        compactExtra.insert(
            QStringLiteral("path"), memoryMetadata.value(QStringLiteral("longMemoryPath")).toString());
        compactExtra.insert(QStringLiteral("longMemoryAdded"),
                            memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt());
        compactExtra.insert(QStringLiteral("longMemoryDuplicate"),
                            memoryMetadata.value(QStringLiteral("longMemoryDuplicate")).toInt());
        compactExtra.insert(QStringLiteral("manualRemember"), true);
        for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
            compactExtra.insert(it.key(), it.value());
        if (!selectedTraceId.isEmpty())
            compactExtra.insert(QStringLiteral("source_trace_id"), selectedTraceId);
        if (!selectedTurnId.isEmpty())
            compactExtra.insert(QStringLiteral("source_turn_id"), selectedTurnId);
        m_app.m_conversationService->emitPipelineEvent(
            QStringLiteral("memory.compacted"),
            sessionId,
            eventTurn,
            QString(),
            QString(),
            compactExtra);
    }

    MemoryMaintenanceService memoryMaintenance = makeMemoryMaintenanceService();
    memoryMaintenance.refreshIndexAndEmit(
        sessionId, agentId, eventTurn, QStringLiteral("manual_remember"), memoryPath, memoryMetadata);

    return true;
}

bool MemoryService::rebuildMemoryIndexAs(const QString& actorIdentityId,
                                         const QString& agentIdentityId,
                                         QJsonObject* result,
                                         QString* error)
{
    if (result)
        *result = QJsonObject();
    if (error)
        error->clear();

    if (!m_app.m_workspaceService
        || !m_app.m_workspaceService->canIdentityManageGlobalConfig(actorIdentityId)) {
        if (error)
            *error = QStringLiteral("actor has no permission");
        return false;
    }
    if (!m_memoryManager || !m_app.m_identityManager) {
        if (error)
            *error = QStringLiteral("memory manager unavailable");
        return false;
    }

    QStringList targetAgents;
    const QString targetAgentId = agentIdentityId.trimmed();
    if (!targetAgentId.isEmpty()) {
        Identity* agent = m_app.m_identityManager->findById(targetAgentId);
        if (!agent || !agent->isAgent()) {
            if (error)
                *error = QStringLiteral("agent not found");
            return false;
        }
        targetAgents.append(agent->id());
    } else {
        const QList<Identity*> agents = m_app.m_identityManager->allAgents();
        for (Identity* agent : agents) {
            if (!agent || !agent->isAgent())
                continue;
            targetAgents.append(agent->id());
        }
    }

    if (targetAgents.isEmpty()) {
        if (result) {
            result->insert(QStringLiteral("agents_total"), 0);
            result->insert(QStringLiteral("agents_success"), 0);
            result->insert(QStringLiteral("agents_failed"), 0);
            result->insert(QStringLiteral("rows_indexed"), 0);
            result->insert(QStringLiteral("items"), QJsonArray());
        }
        return true;
    }

    int successCount = 0;
    int failedCount = 0;
    int totalRows = 0;
    QJsonArray items;
    QStringList failures;

    for (const QString& id : targetAgents) {
        QJsonObject indexMetadata;
        QString indexError;
        const bool ok = m_memoryManager->rebuildSearchIndex(id, &indexMetadata, &indexError);

        QJsonObject item;
        item.insert(QStringLiteral("agent_id"), id);
        item.insert(QStringLiteral("success"), ok);
        if (ok) {
            ++successCount;
            totalRows += indexMetadata.value(QStringLiteral("rows_indexed")).toInt();
            for (auto it = indexMetadata.constBegin(); it != indexMetadata.constEnd(); ++it)
                item.insert(it.key(), it.value());
        } else {
            ++failedCount;
            item.insert(QStringLiteral("error"), indexError);
            failures.append(QStringLiteral("%1: %2").arg(id, indexError));
        }
        items.append(item);

        QString sessionId;
        if (m_app.m_sessionManager) {
            const QList<Session*> sessions = m_app.m_sessionManager->sessionsForIdentity(id);
            if (!sessions.isEmpty())
                sessionId = sessions.first()->id();
        }
        QJsonObject eventExtra = indexMetadata;
        eventExtra.insert(QStringLiteral("agent_id"), id);
        eventExtra.insert(QStringLiteral("reason"), QStringLiteral("manual_rebuild"));
        eventExtra.insert(QStringLiteral("scope"),
                          targetAgentId.isEmpty() ? QStringLiteral("all") : QStringLiteral("single"));
        if (m_app.m_conversationService) {
            m_app.m_conversationService->emitPipelineEvent(
                ok ? QStringLiteral("memory.index.updated") : QStringLiteral("memory.index.error"),
                sessionId,
                nullptr,
                QString(),
                ok ? QString() : indexError,
                eventExtra);
        }
    }

    if (result) {
        result->insert(QStringLiteral("agents_total"), targetAgents.size());
        result->insert(QStringLiteral("agents_success"), successCount);
        result->insert(QStringLiteral("agents_failed"), failedCount);
        result->insert(QStringLiteral("rows_indexed"), totalRows);
        result->insert(QStringLiteral("items"), items);
    }

    if (failedCount > 0) {
        if (error)
            *error = failures.join(QStringLiteral("; "));
        return false;
    }
    return true;
}

QJsonObject MemoryService::loadMemoryPolicyObject(bool* ok) const
{
    return (m_app.m_governanceService && m_app.m_governanceService->configService())
        ? m_app.m_governanceService->configService()->loadMemoryPolicyObject(ok)
        : QJsonObject();
}

bool MemoryService::saveMemoryPolicyObject(const QJsonObject& obj) const
{
    return m_app.m_governanceService && m_app.m_governanceService->configService()
        && m_app.m_governanceService->configService()->saveMemoryPolicyObject(obj);
}

QString MemoryService::loadUserMemoryMarkdown(bool* ok) const
{
    return (m_app.m_governanceService && m_app.m_governanceService->configService())
        ? m_app.m_governanceService->configService()->loadUserMemoryMarkdown(ok)
        : QString();
}

bool MemoryService::saveUserMemoryMarkdown(const QString& markdown, QString* errOut) const
{
    return m_app.m_governanceService && m_app.m_governanceService->configService()
        && m_app.m_governanceService->configService()->saveUserMemoryMarkdown(markdown, errOut);
}

QString MemoryService::agentHeartbeatInstructionPath(const QString& agentId) const
{
    return (m_app.m_governanceService && m_app.m_governanceService->configService())
        ? m_app.m_governanceService->configService()->agentHeartbeatInstructionPath(agentId)
        : QString();
}

QString MemoryService::heartbeatRuntimeStateLocation(const QString& agentId) const
{
    return (m_app.m_governanceService && m_app.m_governanceService->configService())
        ? m_app.m_governanceService->configService()->heartbeatRuntimeStateLocation(agentId)
        : QString();
}

QJsonObject MemoryService::loadHeartbeatRuntimeState(const QString& agentId, bool* ok) const
{
    return (m_app.m_governanceService && m_app.m_governanceService->configService())
        ? m_app.m_governanceService->configService()->loadHeartbeatRuntimeState(agentId, ok)
        : QJsonObject();
}

QString MemoryService::readPossiblyMojibakeUtf8File(const QString& filePath, bool* ok) const
{
    return (m_app.m_governanceService && m_app.m_governanceService->configService())
        ? m_app.m_governanceService->configService()->readPossiblyMojibakeUtf8File(filePath, ok)
        : QString();
}

bool MemoryService::writeUtf8TextFile(const QString& filePath,
                                      const QString& text,
                                      QString* errOut) const
{
    return m_app.m_governanceService && m_app.m_governanceService->configService()
        && m_app.m_governanceService->configService()->writeUtf8TextFile(filePath, text, errOut);
}

HeartbeatConfig MemoryService::heartbeatConfigForAgent(const QString& agentId) const
{
    return m_heartbeatService ? m_heartbeatService->configForAgent(agentId) : HeartbeatConfig();
}

QString MemoryService::heartbeatPathForAgent(const QString& agentId) const
{
    return m_heartbeatService ? m_heartbeatService->heartbeatPathForAgent(agentId) : QString();
}

void MemoryService::updateHeartbeatConfig(const QString& agentId, const HeartbeatConfig& config)
{
    if (m_heartbeatService)
        m_heartbeatService->updateConfig(agentId, config);
}

void MemoryService::startHeartbeatForAgent(const QString& agentId)
{
    if (m_heartbeatService)
        m_heartbeatService->startHeartbeat(agentId);
}

void MemoryService::stopHeartbeatForAgent(const QString& agentId)
{
    if (m_heartbeatService)
        m_heartbeatService->stopHeartbeat(agentId);
}

void MemoryService::triggerHeartbeatForAgent(const QString& agentId, const QString& reason)
{
    if (m_heartbeatService)
        m_heartbeatService->triggerHeartbeat(agentId, reason);
}

QList<ScheduledJob> MemoryService::allScheduledJobs() const
{
    return m_schedulerService ? m_schedulerService->allJobs() : QList<ScheduledJob>();
}

bool MemoryService::scheduledJobById(const QString& jobId, ScheduledJob* outJob) const
{
    return m_schedulerService && m_schedulerService->jobById(jobId, outJob);
}

QString MemoryService::addScheduledJob(const ScheduledJob& job)
{
    return m_schedulerService ? m_schedulerService->addJob(job) : QString();
}

bool MemoryService::updateScheduledJob(const QString& jobId, const ScheduledJob& job)
{
    return m_schedulerService && m_schedulerService->updateJob(jobId, job);
}

bool MemoryService::removeScheduledJob(const QString& jobId)
{
    return m_schedulerService && m_schedulerService->removeJob(jobId);
}

void MemoryService::triggerScheduledJob(const QString& jobId)
{
    if (m_schedulerService)
        m_schedulerService->triggerJob(jobId);
}

void MemoryService::initialize(RuntimeManager* runtimeManager, ModelFactory* modelFactory)
{
    if (m_healthMonitor) {
        m_healthMonitor->setRuntimeManager(runtimeManager);
        m_healthMonitor->setModelFactory(modelFactory);
        QObject::connect(m_healthMonitor.get(),
                         &HealthMonitor::providerDown,
                         &m_app,
                         [this](const QString& configId, const QString& reason) {
                             QJsonObject extra;
                             extra.insert(QStringLiteral("provider_id"), configId);
                             extra.insert(QStringLiteral("state"), QStringLiteral("down"));
                             extra.insert(QStringLiteral("reason"), reason);
                             if (m_app.m_conversationService) {
                                 m_app.m_conversationService->emitPipelineEvent(
                                     QStringLiteral("infra.provider_down"),
                                     QString(),
                                     nullptr,
                                     QString(),
                                     reason,
                                     extra);
                             }
                         });
        QObject::connect(m_healthMonitor.get(),
                         &HealthMonitor::providerRecovered,
                         &m_app,
                         [this](const QString& configId) {
                             QJsonObject extra;
                             extra.insert(QStringLiteral("provider_id"), configId);
                             extra.insert(QStringLiteral("state"), QStringLiteral("recovered"));
                             if (m_app.m_conversationService) {
                                 m_app.m_conversationService->emitPipelineEvent(
                                     QStringLiteral("infra.provider_recovered"),
                                     QString(),
                                     nullptr,
                                     QString(),
                                     QString(),
                                     extra);
                             }
                         });
        m_healthMonitor->start();
    }

    if (m_heartbeatService)
        m_heartbeatService->setPersistence(m_app.m_persistence.get());
    if (m_schedulerService) {
        m_schedulerService->setPersistence(m_app.m_persistence.get());
        m_schedulerService->start();
    }
}

MemoryManager* MemoryService::memoryManager() const { return m_memoryManager.get(); }
HealthMonitor* MemoryService::healthMonitor() const { return m_healthMonitor.get(); }
HeartbeatService* MemoryService::heartbeatService() const { return m_heartbeatService.get(); }
SchedulerService* MemoryService::schedulerService() const { return m_schedulerService.get(); }
AgentPulseRegistry* MemoryService::agentPulseRegistry() const { return m_agentPulseRegistry.get(); }
QHash<QString, int>& MemoryService::memoryRetainedTurnsByAgent() { return m_memoryRetainedTurnsByAgent; }
const QHash<QString, int>& MemoryService::memoryRetainedTurnsByAgent() const { return m_memoryRetainedTurnsByAgent; }
QHash<QString, HeartbeatRuntimeState>& MemoryService::heartbeatRuntimeByAgent() { return m_heartbeatRuntimeByAgent; }
const QHash<QString, HeartbeatRuntimeState>& MemoryService::heartbeatRuntimeByAgent() const { return m_heartbeatRuntimeByAgent; }
QHash<QString, AgentPulse*>& MemoryService::agentPulses() { return m_agentPulses; }
const QHash<QString, AgentPulse*>& MemoryService::agentPulses() const { return m_agentPulses; }

void MemoryService::onDelegateJobSettled(const QString& jobId,
                                         const QString& ownerAgentId,
                                         bool success,
                                         const QString& result)
{
    if (!m_app.m_conversationService)
        return;
    ChatCoordinatorFactory factory(m_app.m_conversationService->makeConversationCoreDeps());
    BackgroundTaskCoordinator coordinator(factory.makeBackgroundTaskDependencies());
    coordinator.onDelegateJobSettled(jobId, ownerAgentId, success, result);
}

void MemoryService::onHeartbeatTriggered(const QString& agentId, const QString& reason)
{
    if (!m_app.m_identityManager || !m_app.m_conversationService)
        return;

    Identity* agent = m_app.m_identityManager->findById(agentId);
    if (!agent || !agent->isAgent())
        return;

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return;
    HeartbeatStateStore stateStore(
        HeartbeatStateStore::Dependencies {
            [this](const QString& key) {
                return m_app.m_persistence ? m_app.m_persistence->getAppState(key) : QString();
            },
            [this](const QString& key, const QString& value) {
                return m_app.m_persistence ? m_app.m_persistence->setAppState(key, value) : false;
            },
            [this](const QString& path) {
                return m_app.m_persistence ? m_app.m_persistence->readJsonObject(path) : QJsonObject();
            },
            [this]() { return m_app.m_persistence ? m_app.m_persistence->agentsDirPath() : QString(); },
            []() { return DatabaseManager::instance()->isReady(); }
        });

    const QString reasonLabel = reason.trimmed().isEmpty() ? QStringLiteral("interval") : reason.trimmed();
    HeartbeatConfig hbCfg;
    if (m_heartbeatService)
        hbCfg = m_heartbeatService->configForAgent(trimmedAgentId);
    hbCfg.snapshotSignals = normalizeHeartbeatSignalsCompat(hbCfg.snapshotSignals);
    const QSet<QString> enabledSignals(hbCfg.snapshotSignals.begin(), hbCfg.snapshotSignals.end());
    const bool watchProvider = enabledSignals.contains(QStringLiteral("provider_status"));
    const bool watchDelegate = enabledSignals.contains(QStringLiteral("delegate_jobs"));
    const bool watchPulse = enabledSignals.contains(QStringLiteral("pulse_state"));
    const bool watchScheduler = enabledSignals.contains(QStringLiteral("scheduler_jobs"));
    const bool watchMemory = enabledSignals.contains(QStringLiteral("memory_progress"));

    QString providerId;
    bool providerDown = false;
    if (watchProvider && m_healthMonitor && m_app.m_conversationService) {
        const LLMConfig cfg = m_app.m_conversationService->composeConfigForIdentity(agent);
        providerId = ModelFactory::resolveInstanceId(cfg);
        providerDown = (!providerId.isEmpty() && m_healthMonitor->isProviderDown(providerId));
    }

    const QList<DelegateTaskScheduler::JobInfo> activeJobs =
        DelegateTaskScheduler::instance()->listJobs(trimmedAgentId, true, 50);

    int schedulerEnabledJobs = 0;
    QDateTime schedulerNextFireAtUtc;
    if (watchPulse)
        ensureAgentPulse(trimmedAgentId);
    QString pulseState;
    if (watchPulse) {
        AgentPulse* pulse = m_agentPulseRegistry ? m_agentPulseRegistry->find(trimmedAgentId) : nullptr;
        if (pulse)
            pulseState = pulseStateToString(pulse->currentState());
    }
    if (watchScheduler && m_schedulerService) {
        const QList<ScheduledJob> jobs = m_schedulerService->allJobs();
        for (const ScheduledJob& job : jobs) {
            if (job.agentId.trimmed() != trimmedAgentId)
                continue;
            if (job.enabled)
                ++schedulerEnabledJobs;
            if (job.nextFireAtUtc.isValid()
                && (!schedulerNextFireAtUtc.isValid() || job.nextFireAtUtc < schedulerNextFireAtUtc)) {
                schedulerNextFireAtUtc = job.nextFireAtUtc;
            }
        }
    }
    qint64 memoryDocSizeBytes = -1;
    if (watchMemory) {
        const QString memoryMdPath =
            QDir(QDir(m_app.m_persistence ? m_app.m_persistence->agentsDirPath() : QString())
                     .filePath(trimmedAgentId))
                .filePath(QStringLiteral("memory.md"));
        if (!memoryMdPath.trimmed().isEmpty() && QFile::exists(memoryMdPath))
            memoryDocSizeBytes = QFileInfo(memoryMdPath).size();
    }
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    HeartbeatRuntimeState& runtimeState = m_heartbeatRuntimeByAgent[trimmedAgentId];
    if (!runtimeState.loaded)
        stateStore.load(trimmedAgentId, &runtimeState);

    HeartbeatSnapshotCoordinator::Inputs snapshotInputs;
    snapshotInputs.agentId = trimmedAgentId;
    snapshotInputs.reason = reasonLabel;
    snapshotInputs.config = hbCfg;
    snapshotInputs.providerId = providerId;
    snapshotInputs.providerDown = providerDown;
    snapshotInputs.activeJobs = activeJobs;
    snapshotInputs.pulseState = pulseState;
    snapshotInputs.schedulerEnabledJobs = schedulerEnabledJobs;
    snapshotInputs.schedulerNextFireAtUtc = schedulerNextFireAtUtc;
    snapshotInputs.memoryRetainedTurns = m_memoryRetainedTurnsByAgent.value(trimmedAgentId, 0);
    snapshotInputs.memoryDocSizeBytes = memoryDocSizeBytes;
    snapshotInputs.runtimeState.hasSnapshot = runtimeState.hasSnapshot;
    snapshotInputs.runtimeState.stateObj = runtimeState.stateObj;
    snapshotInputs.runtimeState.lastSnapshotObj = runtimeState.lastSnapshotObj;
    snapshotInputs.runtimeState.lastSnapshotDigest = runtimeState.lastSnapshotDigest;
    snapshotInputs.runtimeState.lastNotifyAtUtc = runtimeState.lastNotifyAtUtc;
    snapshotInputs.runtimeState.lastPersistAtUtc = runtimeState.lastPersistAtUtc;
    snapshotInputs.nowUtc = nowUtc;

    const HeartbeatSnapshotCoordinator::Result snapshotResult =
        HeartbeatSnapshotCoordinator::evaluate(snapshotInputs);
    if (!snapshotResult.valid)
        return;

    runtimeState.hasSnapshot = snapshotResult.runtimeState.hasSnapshot;
    runtimeState.stateObj = snapshotResult.runtimeState.stateObj;
    runtimeState.lastSnapshotObj = snapshotResult.runtimeState.lastSnapshotObj;
    runtimeState.lastSnapshotDigest = snapshotResult.runtimeState.lastSnapshotDigest;
    runtimeState.lastNotifyAtUtc = snapshotResult.runtimeState.lastNotifyAtUtc;
    runtimeState.lastPersistAtUtc = snapshotResult.runtimeState.lastPersistAtUtc;

    bool shouldPersistState = snapshotResult.shouldPersistState;
    auto persistStateIfNeeded = [&](bool forcePersist) mutable {
        const bool doPersist = forcePersist || shouldPersistState;
        stateStore.persist(trimmedAgentId, &runtimeState, nowUtc, doPersist);
    };
    const QJsonObject triggeredExtra = snapshotResult.triggeredExtra;
    m_app.m_conversationService->emitPipelineEvent(
        QStringLiteral("heartbeat.triggered"), QString(), nullptr, QString(), QString(), triggeredExtra);

    if (providerDown) {
        persistStateIfNeeded(false);
        QJsonObject extra = triggeredExtra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("provider_down"));
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("heartbeat.skipped"),
                                                       QString(),
                                                       nullptr,
                                                       QString(),
                                                       QStringLiteral("provider_down"),
                                                       extra);
        return;
    }

    if (!snapshotResult.shouldNotify) {
        persistStateIfNeeded(false);
        QJsonObject completeExtra = triggeredExtra;
        completeExtra.insert(QStringLiteral("silent"), true);
        completeExtra.insert(QStringLiteral("silent_reason"), snapshotResult.skipReason);
        m_app.m_conversationService->emitPipelineEvent(
            QStringLiteral("heartbeat.completed"), QString(), nullptr, QString(), QString(), completeExtra);
        return;
    }

    ChatCoordinatorFactory factory(m_app.m_conversationService->makeConversationCoreDeps());
    const PrimarySessionResolver resolver = factory.makePrimarySessionResolver();
    const QString sessionId =
        resolver.resolveForAgent(trimmedAgentId, true, false, QStringLiteral("heartbeat"));
    if (sessionId.isEmpty()) {
        persistStateIfNeeded(false);
        QJsonObject extra = triggeredExtra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("no_session"));
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("heartbeat.skipped"),
                                                       QString(),
                                                       nullptr,
                                                       QString(),
                                                       QStringLiteral("no_session"),
                                                       extra);
        return;
    }
    HeartbeatDispatchCoordinator coordinator(
        factory.makeHeartbeatDispatchDependencies(runtimeState, shouldPersistState, nowUtc));
    coordinator.dispatch(trimmedAgentId,
                         sessionId,
                         reasonLabel,
                         snapshotResult.forceInteractive,
                         snapshotResult.hasChange,
                         watchDelegate,
                         watchProvider,
                         watchPulse,
                         providerDown,
                         providerId,
                         activeJobs,
                         triggeredExtra);
}

void MemoryService::onHeartbeatSkipped(const QString& agentId, const QString& reason)
{
    if (!m_app.m_conversationService)
        return;
    QJsonObject extra;
    extra.insert(QStringLiteral("agent_id"), agentId);
    extra.insert(QStringLiteral("reason"), reason);
    m_app.m_conversationService->emitPipelineEvent(
        QStringLiteral("heartbeat.skipped"), QString(), nullptr, QString(), reason, extra);
}

void MemoryService::onScheduledJobTriggered(const QString& jobId, const QString& jobName)
{
    if (!m_schedulerService || !m_app.m_identityManager || !m_app.m_conversationService)
        return;
    ChatCoordinatorFactory factory(m_app.m_conversationService->makeConversationCoreDeps());
    BackgroundTaskCoordinator coordinator(factory.makeBackgroundTaskDependencies());
    coordinator.onScheduledJobTriggered(jobId, jobName);
}

void MemoryService::ensureAgentPulse(const QString& agentId)
{
    if (m_agentPulseRegistry)
        m_agentPulseRegistry->ensure(agentId);
}

void MemoryService::reportPulseProgress(const QString& agentId, const QString& summary)
{
    if (m_agentPulseRegistry)
        m_agentPulseRegistry->reportProgress(agentId, summary);
}

ToolResult MemoryService::executeMemoryWriteTool(const QJsonObject& args)
{
    return makeMemoryToolWriteService().execute(args);
}

MemoryMaintenanceService MemoryService::makeMemoryMaintenanceService()
{
    return MemoryMaintenanceService(
        MemoryMaintenanceService::Dependencies {
            [this](const QString& agentId, QJsonObject* metadata, QString* error) {
                return m_memoryManager && m_memoryManager->rebuildSearchIndex(agentId, metadata, error);
            },
            [this]() { return m_memoryManager && m_memoryManager->reflectionEnabled(); },
            [this]() { return m_memoryManager ? m_memoryManager->reflectionIntervalTurns() : 0; },
            [this](const QString& agentId,
                   const QString& sessionId,
                   const QString& turnId,
                   const QString& traceId,
                   QString* summary,
                   QString* writtenPath,
                   QJsonObject* metadata,
                   QString* error) {
                return m_memoryManager
                    && m_memoryManager->reflectAndScore(
                        agentId, sessionId, turnId, traceId, summary, writtenPath, metadata, error);
            },
            [this](const QString& agentId) {
                return m_memoryRetainedTurnsByAgent.value(agentId.trimmed(), 0);
            },
            [this](const QString& agentId, int retainedTurns) {
                m_memoryRetainedTurnsByAgent.insert(agentId.trimmed(), retainedTurns);
            },
            [this](const QString& sessionId,
                   const QString& type,
                   const TurnTask* turn,
                   const QString& delta,
                   const QString& error,
                   const QJsonObject& extra,
                   bool persistToDisk) {
                if (m_app.m_conversationService) {
                    m_app.m_conversationService->emitPipelineEvent(
                        type, sessionId, turn, delta, error, extra, persistToDisk);
                }
            }
        });
}

MemoryToolWriteService MemoryService::makeMemoryToolWriteService()
{
    MemoryMaintenanceService memoryMaintenance = makeMemoryMaintenanceService();
    return MemoryToolWriteService(
        MemoryToolWriteService::Dependencies {
            [this](const QString& agentId) {
                return m_app.m_conversationService
                    ? m_app.m_conversationService->activeSessionByAgent().value(agentId).trimmed()
                    : QString();
            },
            [this](const QString& agentId) {
                if (!m_app.m_conversationService)
                    return QString();
                ChatCoordinatorFactory factory(m_app.m_conversationService->makeConversationCoreDeps());
                const PrimarySessionResolver resolver = factory.makePrimarySessionResolver();
                return resolver.resolveForAgent(agentId, false, false, QStringLiteral("memory_write"));
            },
            [this](const QString& sessionId) {
                return m_app.m_conversationService
                    ? m_app.m_conversationService->turnManager().activeTurn(sessionId)
                    : nullptr;
            },
            [this](const QString& agentId,
                   const QString& sessionId,
                   const QString& turnId,
                   const QString& traceId,
                   const QString& memoryText,
                   const QString& reason,
                   QString* summary,
                   QString* writtenPath,
                   QJsonObject* metadata,
                   QString* error) {
                return m_memoryManager
                    && m_memoryManager->rememberToolRequested(agentId,
                                                              sessionId,
                                                              turnId,
                                                              traceId,
                                                              memoryText,
                                                              reason,
                                                              summary,
                                                              writtenPath,
                                                              metadata,
                                                              error);
            },
            [this](const QString& sessionId,
                   const QString& type,
                   const TurnTask* turn,
                   const QString& delta,
                   const QString& error,
                   const QJsonObject& extra,
                   bool persistToDisk) {
                if (m_app.m_conversationService) {
                    m_app.m_conversationService->emitPipelineEvent(
                        type, sessionId, turn, delta, error, extra, persistToDisk);
                }
            },
            [memoryMaintenance](const QString& sessionId,
                                const QString& agentId,
                                const TurnTask* turn,
                                const QString& reason,
                                const QString& sourcePath,
                                const QJsonObject& sourceMetadata) {
                memoryMaintenance.refreshIndexAndEmit(
                    sessionId, agentId, turn, reason, sourcePath, sourceMetadata);
            }
        });
}

void MemoryService::ensureMemoryInitializedForAgent(Identity* agentIdentity)
{
    if (!m_memoryManager || !agentIdentity || !agentIdentity->isAgent())
        return;

    if (m_app.m_persistence) {
        const QString agentId = agentIdentity->id().trimmed();
        const QString workspacePath =
            QDir(m_app.m_persistence->agentsDirPath()).filePath(agentId + QStringLiteral("/workspace"));
        if (!QDir().mkpath(workspacePath)) {
            qWarning() << "[ApplicationServices] agent workspace init failed:" << agentId
                       << workspacePath;
        }

        const QString heartbeatMdPath = m_app.m_persistence->agentHeartbeatInstructionPath(agentId);
        if (!QFile::exists(heartbeatMdPath)) {
            HeartbeatPromptBuilder::repairInstructionFileIfNeeded(heartbeatMdPath);
            QFile file(heartbeatMdPath);
            if (!file.exists()) {
                if (!QDir().mkpath(QFileInfo(heartbeatMdPath).absolutePath()))
                    qWarning() << "[ApplicationServices] heartbeat template dir init failed:"
                               << heartbeatMdPath;
                else if (file.open(QFile::WriteOnly | QFile::Text)) {
                    const QByteArray bytes = HeartbeatPromptBuilder::defaultTemplate().toUtf8();
                    file.write(bytes);
                    file.close();
                }
            }
        } else {
            HeartbeatPromptBuilder::repairInstructionFileIfNeeded(heartbeatMdPath);
        }

        const QString heartbeatCfgPath = m_app.m_persistence->agentHeartbeatConfigPath(agentId);
        if (!QFile::exists(heartbeatCfgPath)) {
            QJsonObject cfg;
            cfg.insert(QStringLiteral("enabled"), true);
            cfg.insert(QStringLiteral("intervalMs"), 30 * 60 * 1000);
            cfg.insert(QStringLiteral("coalesceMs"), 250);
            cfg.insert(QStringLiteral("duplicateWindowMs"), 24 * 60 * 60 * 1000);
            cfg.insert(QStringLiteral("silentWhenNoChange"), true);
            cfg.insert(QStringLiteral("notifyOnChangeOnly"), true);
            cfg.insert(QStringLiteral("notifyMinIntervalMs"), 30 * 60 * 1000);
            cfg.insert(QStringLiteral("persistStateOnNoChange"), false);
            cfg.insert(QStringLiteral("statePersistIntervalMs"), 60 * 1000);
            QJsonArray snapshotSignals;
            snapshotSignals.append(QStringLiteral("provider_status"));
            snapshotSignals.append(QStringLiteral("delegate_jobs"));
            snapshotSignals.append(QStringLiteral("pulse_state"));
            cfg.insert(QStringLiteral("snapshotSignals"), snapshotSignals);
            cfg.insert(QStringLiteral("heartbeatPath"), heartbeatMdPath);
            QJsonObject activeHours;
            activeHours.insert(QStringLiteral("start"), QStringLiteral("08:00"));
            activeHours.insert(QStringLiteral("end"), QStringLiteral("23:00"));
            activeHours.insert(QStringLiteral("timezone"), QStringLiteral("Asia/Shanghai"));
            cfg.insert(QStringLiteral("activeHours"), activeHours);
            m_app.m_persistence->writeJsonObject(heartbeatCfgPath, cfg);
        }
    }

    QString memoryError;
    if (!m_memoryManager->initializeForAgent(agentIdentity, &memoryError) && !memoryError.isEmpty()) {
        qWarning() << "[ApplicationServices] agent memory init failed:" << agentIdentity->id()
                   << memoryError;
    }
}

bool MemoryService::ensureUserMemoryDocument()
{
    if (!m_memoryManager)
        return false;
    QString memoryError;
    const bool ok = m_memoryManager->ensureUserMemoryDocument(&memoryError);
    if (!ok && !memoryError.isEmpty())
        qWarning() << "[ApplicationServices] user memory init failed:" << memoryError;
    return ok;
}
