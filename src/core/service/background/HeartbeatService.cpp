#include "HeartbeatService.h"

#include "ApplicationServices.h"
#include "AgentRuntime.h"
#include "AgentPulse.h"
#include "AgentPulseRegistry.h"
#include "ConversationService.h"
#include "HealthMonitor.h"
#include "HeartbeatDecisionEngine.h"
#include "HeartbeatExecutionService.h"
#include "HeartbeatPromptBuilder.h"
#include "HeartbeatSnapshotService.h"
#include "HeartbeatStateStore.h"
#include "MemoryService.h"
#include "RuntimeManager.h"
#include "ChatCoordinatorSupport.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/manager/IdentityManager.h"
#include "core/model/Identity.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "llm/ModelFactory.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>

namespace {

QString normalizeAgentId(const QString& agentId)
{
    return agentId.trimmed();
}

QString normalizeReason(const QString& reason, const QString& fallback)
{
    const QString normalized = reason.trimmed();
    return normalized.isEmpty() ? fallback : normalized;
}

QDateTime utcNow()
{
    return QDateTime::currentDateTimeUtc();
}

QString hashCompactJson(const QJsonObject& obj)
{
    const QByteArray compact = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(compact, QCryptographicHash::Sha1).toHex());
}

QString digestSummary(const QString& summary)
{
    const QString normalized = summary.simplified();
    if (normalized.isEmpty())
        return QString();
    return QString::fromLatin1(
        QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString providerStateToText(HealthMonitor::State state)
{
    switch (state) {
    case HealthMonitor::Healthy:
        return QStringLiteral("healthy");
    case HealthMonitor::Degraded:
        return QStringLiteral("degraded");
    case HealthMonitor::Down:
        return QStringLiteral("down");
    case HealthMonitor::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

bool isProviderStateBlocking(const QString& state)
{
    return state.trimmed().compare(QStringLiteral("down"), Qt::CaseInsensitive) == 0;
}

QTimeZone resolveTimezone(const ActiveHours& hours)
{
    QTimeZone zone(hours.timezone.trimmed().toUtf8());
    if (!zone.isValid())
        zone = QTimeZone::systemTimeZone();
    return zone;
}

HeartbeatStateStore makeStateStore(ChatPersistenceService* persistence)
{
    return HeartbeatStateStore(HeartbeatStateStore::Dependencies {
        [persistence](const QString& key) {
            return persistence ? persistence->getAppState(key) : QString();
        },
        [persistence](const QString& key, const QString& value) {
            return persistence ? persistence->setAppState(key, value) : false;
        },
        []() { return DatabaseManager::instance()->isReady(); }
    });
}

HeartbeatTicketPriority maxPriority(HeartbeatTicketPriority lhs, HeartbeatTicketPriority rhs)
{
    return static_cast<int>(lhs) >= static_cast<int>(rhs) ? lhs : rhs;
}

} // namespace

HeartbeatService::HeartbeatService(ApplicationServices& app, QObject* parent)
    : QObject(parent)
    , m_app(app)
    , m_tickTimer(new QTimer(this))
{
    m_snapshotService = new HeartbeatSnapshotService(
        HeartbeatSnapshotService::Dependencies {
            [this](const QString& agentId) {
                Identity* agent =
                    m_app.m_identityManager ? m_app.m_identityManager->findById(agentId) : nullptr;
                return providerStateForAgent(agent);
            },
            [this](const QString& agentId) {
                Identity* agent =
                    m_app.m_identityManager ? m_app.m_identityManager->findById(agentId) : nullptr;
                return providerIdForAgent(agent);
            },
            [](const QString& agentId) {
                return DelegateTaskScheduler::instance()->listJobs(agentId, true, 50);
            },
            [this](const QString& agentId) {
                if (!m_app.m_memoryService)
                    return QString();
                m_app.m_memoryService->ensureAgentPulse(agentId);
                AgentPulse* pulse =
                    m_app.m_memoryService->agentPulseRegistry()
                        ? m_app.m_memoryService->agentPulseRegistry()->find(agentId)
                        : nullptr;
                return pulse ? ChatCoordinatorSupport::pulseStateToString(pulse->currentState())
                             : QString();
            },
            [this](const QString& agentId) {
                int enabledJobs = 0;
                if (!m_app.m_memoryService || !m_app.m_memoryService->schedulerService())
                    return enabledJobs;
                const QList<ScheduledJob> jobs =
                    m_app.m_memoryService->schedulerService()->allJobs();
                for (const ScheduledJob& job : jobs) {
                    if (job.agentId.trimmed() == agentId.trimmed() && job.enabled)
                        ++enabledJobs;
                }
                return enabledJobs;
            },
            [this](const QString& agentId) {
                if (!m_app.m_memoryService || !m_app.m_memoryService->schedulerService())
                    return false;
                const QList<ScheduledJob> jobs =
                    m_app.m_memoryService->schedulerService()->allJobs();
                for (const ScheduledJob& job : jobs) {
                    if (job.agentId.trimmed() != agentId.trimmed() || !job.enabled)
                        continue;
                    if (!job.nextFireAtUtc.isValid())
                        return true;
                }
                return false;
            },
            [this](const QString& agentId) {
                return m_app.m_memoryService
                    ? m_app.m_memoryService->memoryRetainedTurnsByAgent().value(agentId.trimmed(), 0)
                    : 0;
            },
            [this](const QString& agentId) {
                if (!m_app.m_persistence)
                    return static_cast<qint64>(-1);
                const QString path =
                    QDir(QDir(m_app.m_persistence->agentsDirPath()).filePath(agentId.trimmed()))
                        .filePath(QStringLiteral("memory.md"));
                QFileInfo info(path);
                return info.exists() ? info.size() : static_cast<qint64>(-1);
            },
            [](const QString&) { return false; }
        });

    m_decisionEngine = new HeartbeatDecisionEngine();
    m_executionService = new HeartbeatExecutionService(
        HeartbeatExecutionService::Dependencies {
            [this](const QString& agentId, bool createIfMissing, bool isolated, const QString& suffix) {
                return m_app.m_memoryService
                    ? m_app.m_memoryService->resolvePrimarySessionForAgent(
                          agentId, createIfMissing, isolated, suffix)
                    : QString();
            },
            [this](const QString& sessionId,
                   const QString& agentId,
                   const TurnTask& turn,
                   bool force,
                   const QString& reason) {
                if (m_app.m_memoryService) {
                    m_app.m_memoryService->maybeReflectMemoryAndEmit(
                        sessionId, agentId, turn, force, reason);
                }
            },
            [this](const QString& sessionId,
                   const QString& agentId,
                   const TurnTask* turn,
                   const QString& reason,
                   const QString& path,
                   const QJsonObject& metadata) {
                if (m_app.m_memoryService) {
                    m_app.m_memoryService->refreshMemoryIndexAndEmit(
                        sessionId, agentId, turn, reason, path, metadata);
                }
            },
            [this](const QString& agentId,
                   const HeartbeatTicket& ticket,
                   const HeartbeatSnapshot& snapshot,
                   const QStringList& actionableSignals) {
                const HeartbeatPromptBuilder builder(
                    HeartbeatPromptBuilder::Dependencies {
                        [this](const QString& aid) {
                            return m_app.m_memoryService
                                ? m_app.m_memoryService->heartbeatInstructionPathForAgent(aid)
                                : QString();
                        },
                        [this](const QString& path, bool* ok) {
                            return m_app.m_memoryService
                                ? m_app.m_memoryService->readPossiblyMojibakeUtf8File(path, ok)
                                : QString();
                        }
                    });
                return builder.buildEscalationPrompt(
                    HeartbeatPromptBuilder::Context { agentId, ticket, snapshot, actionableSignals });
            }
        });

    m_tickTimer->setInterval(1000);
    connect(m_tickTimer, &QTimer::timeout, this, &HeartbeatService::onTick);
    m_tickTimer->start();
}

HeartbeatService::~HeartbeatService()
{
    stopAll();
    delete m_snapshotService;
    delete m_decisionEngine;
    delete m_executionService;
}

void HeartbeatService::startAgentHeartbeat(const QString& agentId)
{
    AgentState* agent = ensureAgent(agentId);
    if (!agent)
        return;
    if (!m_healthMonitorConnected && m_app.m_memoryService && m_app.m_memoryService->healthMonitor()) {
        connect(m_app.m_memoryService->healthMonitor(),
                &HealthMonitor::providerDown,
                this,
                &HeartbeatService::onProviderDown);
        connect(m_app.m_memoryService->healthMonitor(),
                &HealthMonitor::providerRecovered,
                this,
                &HeartbeatService::onProviderRecovered);
        m_healthMonitorConnected = true;
    }
    if (!agent->policy.enabled)
        return;
    if (!agent->runtimeState.nextDueAtUtc.isValid())
        scheduleNextDue(agent, utcNow());
    makeStateStore(m_app.m_persistence.get()).save(normalizeAgentId(agentId), &agent->runtimeState);
}

void HeartbeatService::stopAgentHeartbeat(const QString& agentId)
{
    removeAgent(agentId);
}

void HeartbeatService::stopAll()
{
    const auto keys = m_agents.keys();
    for (const QString& key : keys)
        removeAgent(key);
}

void HeartbeatService::requestManualHeartbeat(const QString& agentId, const QString& reason)
{
    HeartbeatTicket ticket;
    ticket.kind = HeartbeatTicketKind::Manual;
    ticket.priority = HeartbeatTicketPriority::Critical;
    ticket.reason = normalizeReason(reason, QStringLiteral("manual"));
    ticket.dedupeKey = QStringLiteral("manual");
    ticket.requestedAtUtc = utcNow();
    requestTicket(agentId, ticket);
}

void HeartbeatService::requestEventDrivenHeartbeat(const QString& agentId,
                                                   const QString& reason,
                                                   HeartbeatTicketPriority priority,
                                                   const QJsonObject& payload)
{
    HeartbeatTicket ticket;
    ticket.kind = HeartbeatTicketKind::EventDriven;
    ticket.priority = priority;
    ticket.reason = normalizeReason(reason, QStringLiteral("event"));
    ticket.dedupeKey = ticket.reason;
    ticket.requestedAtUtc = utcNow();
    ticket.payload = payload;
    requestTicket(agentId, ticket);
}

void HeartbeatService::suppressAgentHeartbeat(const QString& agentId, const QString& reason)
{
    AgentState* agent = ensureAgent(agentId);
    if (!agent)
        return;
    agent->suppressed = true;
    agent->suppressReason = normalizeReason(reason, QStringLiteral("suppressed"));
    if (agent->runtimeState.hasPendingTicket) {
        agent->runtimeState.laneState = HeartbeatLaneState::Deferred;
        agent->runtimeState.lastDeferredReason = agent->suppressReason;
        makeStateStore(m_app.m_persistence.get()).save(normalizeAgentId(agentId), &agent->runtimeState);
    }
}

void HeartbeatService::unsuppressAgentHeartbeat(const QString& agentId)
{
    AgentState* agent = ensureAgent(agentId);
    if (!agent)
        return;
    agent->suppressed = false;
    agent->suppressReason.clear();
    if (agent->runtimeState.hasPendingTicket) {
        agent->runtimeState.laneState = HeartbeatLaneState::Deferred;
        makeStateStore(m_app.m_persistence.get()).save(normalizeAgentId(agentId), &agent->runtimeState);
    }
}

void HeartbeatService::updatePolicy(const QString& agentId, const HeartbeatPolicy& policy)
{
    AgentState* agent = ensureAgent(agentId);
    if (!agent)
        return;

    agent->policy = policy;
    if (agent->policy.instructionPath.trimmed().isEmpty())
        agent->policy.instructionPath = instructionPathForAgent(agentId);
    savePolicy(agentId, agent->policy);
    if (agent->policy.enabled)
        scheduleNextDue(agent, utcNow());
    else
        agent->runtimeState.nextDueAtUtc = QDateTime();
    makeStateStore(m_app.m_persistence.get()).save(normalizeAgentId(agentId), &agent->runtimeState);
}

HeartbeatPolicy HeartbeatService::policyForAgent(const QString& agentId) const
{
    const QString key = normalizeAgentId(agentId);
    if (key.isEmpty())
        return HeartbeatPolicy();
    if (m_agents.contains(key))
        return m_agents.value(key)->policy;
    return loadPolicy(key);
}

QString HeartbeatService::instructionPathForAgent(const QString& agentId) const
{
    HeartbeatPolicy policy = policyForAgent(agentId);
    if (!policy.instructionPath.trimmed().isEmpty())
        return policy.instructionPath.trimmed();
    return m_app.m_persistence ? m_app.m_persistence->agentHeartbeatInstructionPath(agentId)
                               : QString();
}

void HeartbeatService::onTick()
{
    if (m_agents.isEmpty())
        return;

    const QDateTime nowUtc = utcNow();
    for (auto it = m_agents.begin(); it != m_agents.end(); ++it) {
        const QString agentId = it.key();
        AgentState* agent = it.value();
        if (!agent)
            continue;
        if (!agent->policy.enabled && !agent->runtimeState.hasPendingTicket)
            continue;
        if (agent->cycleRunning)
            continue;
        if (agent->startupReadyAtUtc.isValid() && nowUtc < agent->startupReadyAtUtc)
            continue;

        if (agent->runtimeState.hasPendingTicket) {
            const HeartbeatTicket ticket = agent->runtimeState.pendingTicket;
            if (agent->suppressed) {
                deferTicket(agentId, agent, ticket, agent->suppressReason, nowUtc);
                continue;
            }
            if (!isWithinActiveHours(agent->policy, nowUtc)) {
                agent->runtimeState.nextDueAtUtc = nextActiveTime(agent->policy, nowUtc);
                deferTicket(agentId, agent, ticket, QStringLiteral("outside_active_hours"), nowUtc);
                continue;
            }

            Identity* agentIdentity =
                m_app.m_identityManager ? m_app.m_identityManager->findById(agentId) : nullptr;
            const QString providerState = providerStateForAgent(agentIdentity);
            if (ticket.kind != HeartbeatTicketKind::Manual && isProviderStateBlocking(providerState)) {
                deferTicket(agentId, agent, ticket, QStringLiteral("provider_down"), nowUtc);
                continue;
            }
            beginCycle(agentId, agent, ticket);
            continue;
        }

        if (!agent->policy.enabled)
            continue;
        if (!agent->runtimeState.nextDueAtUtc.isValid()) {
            scheduleNextDue(agent, nowUtc);
            makeStateStore(m_app.m_persistence.get()).save(agentId, &agent->runtimeState);
            continue;
        }
        if (agent->runtimeState.nextDueAtUtc > nowUtc)
            continue;

        HeartbeatTicket ticket;
        ticket.kind = HeartbeatTicketKind::Auto;
        ticket.priority = HeartbeatTicketPriority::Normal;
        ticket.reason = QStringLiteral("cadence");
        ticket.dedupeKey = QStringLiteral("auto");
        ticket.requestedAtUtc = nowUtc;
        requestTicket(agentId, ticket);
    }
}

void HeartbeatService::onProviderDown(const QString& providerId, const QString& reason)
{
    for (auto it = m_agents.begin(); it != m_agents.end(); ++it) {
        const QString agentId = it.key();
        Identity* agent =
            m_app.m_identityManager ? m_app.m_identityManager->findById(agentId) : nullptr;
        if (providerIdForAgent(agent) != providerId.trimmed())
            continue;
        emitHeartbeatEvent(QStringLiteral("heartbeat.deferred"),
                           agentId,
                           QJsonObject {
                               { QStringLiteral("reason"), QStringLiteral("provider_down") },
                               { QStringLiteral("provider_id"), providerId.trimmed() },
                               { QStringLiteral("provider_reason"), reason.trimmed() }
                           });
    }
}

void HeartbeatService::onProviderRecovered(const QString& providerId)
{
    for (auto it = m_agents.begin(); it != m_agents.end(); ++it) {
        const QString agentId = it.key();
        Identity* agent =
            m_app.m_identityManager ? m_app.m_identityManager->findById(agentId) : nullptr;
        if (providerIdForAgent(agent) != providerId.trimmed())
            continue;
        requestEventDrivenHeartbeat(agentId,
                                    QStringLiteral("provider_recovered"),
                                    HeartbeatTicketPriority::High,
                                    QJsonObject { { QStringLiteral("provider_id"), providerId.trimmed() } });
    }
}

HeartbeatService::AgentState* HeartbeatService::ensureAgent(const QString& agentId)
{
    const QString key = normalizeAgentId(agentId);
    if (key.isEmpty())
        return nullptr;
    AgentState* existing = m_agents.value(key, nullptr);
    if (existing)
        return existing;

    auto* agent = new AgentState();
    agent->policy = loadPolicy(key);
    agent->runtimeState.stateStorageKey = HeartbeatStateStore::storageKeyForAgent(key);
    agent->runtimeState.stateLocation = HeartbeatStateStore::locationForAgent(key);
    restoreRuntimeState(key, agent, utcNow());
    m_agents.insert(key, agent);
    return agent;
}

void HeartbeatService::removeAgent(const QString& agentId)
{
    const QString key = normalizeAgentId(agentId);
    AgentState* agent = m_agents.take(key);
    if (!agent)
        return;
    delete agent;
}

HeartbeatPolicy HeartbeatService::defaultPolicyForAgent(const QString& agentId) const
{
    HeartbeatPolicy policy;
    policy.activeHours.start = QTime(8, 0);
    policy.activeHours.end = QTime(23, 0);
    policy.activeHours.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    policy.instructionPath =
        m_app.m_persistence ? m_app.m_persistence->agentHeartbeatInstructionPath(agentId) : QString();
    return policy;
}

HeartbeatPolicy HeartbeatService::loadPolicy(const QString& agentId) const
{
    HeartbeatPolicy policy = defaultPolicyForAgent(agentId);
    if (!m_app.m_persistence)
        return policy;

    bool ok = false;
    const QJsonObject obj = m_app.m_persistence->readJsonObject(
        m_app.m_persistence->agentHeartbeatPolicyPath(agentId), &ok);
    if (!ok || obj.isEmpty())
        return policy;

    HeartbeatPolicy loaded = heartbeatPolicyFromJson(obj);
    if (loaded.instructionPath.trimmed().isEmpty())
        loaded.instructionPath = policy.instructionPath;
    return loaded;
}

void HeartbeatService::savePolicy(const QString& agentId, const HeartbeatPolicy& policy) const
{
    if (!m_app.m_persistence)
        return;
    m_app.m_persistence->writeJsonObject(m_app.m_persistence->agentHeartbeatPolicyPath(agentId),
                                         heartbeatPolicyToJson(policy));
}

bool HeartbeatService::isWithinActiveHours(const HeartbeatPolicy& policy, const QDateTime& nowUtc) const
{
    if (!policy.activeHours.start.isValid() || !policy.activeHours.end.isValid())
        return true;
    const QDateTime localNow = nowUtc.toTimeZone(resolveTimezone(policy.activeHours));
    const QTime now = localNow.time();
    const QTime start = policy.activeHours.start;
    const QTime end = policy.activeHours.end;
    if (start <= end)
        return now >= start && now <= end;
    return now >= start || now <= end;
}

QDateTime HeartbeatService::nextActiveTime(const HeartbeatPolicy& policy, const QDateTime& nowUtc) const
{
    if (!policy.activeHours.start.isValid() || !policy.activeHours.end.isValid())
        return nowUtc;

    const QTimeZone zone = resolveTimezone(policy.activeHours);
    QDateTime localNow = nowUtc.toTimeZone(zone);
    const QTime start = policy.activeHours.start;
    const QTime end = policy.activeHours.end;
    if (start <= end) {
        if (localNow.time() < start) {
            localNow.setTime(start);
            return localNow.toUTC();
        }
        localNow = localNow.addDays(1);
        localNow.setTime(start);
        return localNow.toUTC();
    }

    if (localNow.time() <= end || localNow.time() >= start)
        return nowUtc;
    localNow.setTime(start);
    return localNow.toUTC();
}

void HeartbeatService::scheduleNextDue(AgentState* agent, const QDateTime& referenceUtc)
{
    if (!agent)
        return;
    if (!agent->policy.enabled) {
        agent->runtimeState.nextDueAtUtc = QDateTime();
        return;
    }
    agent->runtimeState.lastScheduledAtUtc = referenceUtc;
    agent->runtimeState.nextDueAtUtc =
        referenceUtc.addMSecs(qMax(1000, agent->policy.cadenceMs));
}

void HeartbeatService::restoreRuntimeState(const QString& agentId,
                                          AgentState* agent,
                                          const QDateTime& nowUtc)
{
    if (!agent)
        return;

    HeartbeatStateStore store = makeStateStore(m_app.m_persistence.get());
    store.load(agentId, &agent->runtimeState);
    agent->runtimeState.stateStorageKey = HeartbeatStateStore::storageKeyForAgent(agentId);
    agent->runtimeState.stateLocation = HeartbeatStateStore::locationForAgent(agentId);
    agent->startupReadyAtUtc = nowUtc.addMSecs(qMax(0, agent->policy.startupGraceMs));

    if (!agent->runtimeState.nextDueAtUtc.isValid()) {
        const QDateTime reference = agent->runtimeState.lastCompletedAtUtc.isValid()
            ? agent->runtimeState.lastCompletedAtUtc
            : nowUtc;
        scheduleNextDue(agent, reference);
    }

    if (agent->runtimeState.hasPendingTicket) {
        agent->runtimeState.laneState = HeartbeatLaneState::Deferred;
        store.save(agentId, &agent->runtimeState);
        return;
    }

    if (agent->runtimeState.interruptedRun
        || (agent->runtimeState.lastStartedAtUtc.isValid()
            && (!agent->runtimeState.lastCompletedAtUtc.isValid()
                || agent->runtimeState.lastCompletedAtUtc < agent->runtimeState.lastStartedAtUtc))) {
        HeartbeatTicket recovery;
        recovery.kind = HeartbeatTicketKind::Recovery;
        recovery.priority = HeartbeatTicketPriority::High;
        recovery.reason = QStringLiteral("interrupted_run");
        recovery.dedupeKey = QStringLiteral("recovery");
        recovery.requestedAtUtc = nowUtc;
        requestTicket(agentId, recovery);
        return;
    }

    if (agent->runtimeState.nextDueAtUtc.isValid() && agent->runtimeState.nextDueAtUtc <= nowUtc) {
        HeartbeatTicket recovery;
        recovery.kind = HeartbeatTicketKind::Recovery;
        recovery.priority = HeartbeatTicketPriority::High;
        recovery.reason = QStringLiteral("restart_missed_window");
        recovery.dedupeKey = QStringLiteral("restart_missed_window");
        recovery.requestedAtUtc = nowUtc;
        requestTicket(agentId, recovery);
    }
}

void HeartbeatService::requestTicket(const QString& agentId, const HeartbeatTicket& ticket)
{
    AgentState* agent = ensureAgent(agentId);
    if (!agent)
        return;

    HeartbeatTicket merged = ticket;
    if (agent->runtimeState.hasPendingTicket) {
        const HeartbeatTicket current = agent->runtimeState.pendingTicket;
        if (current.dedupeKey == ticket.dedupeKey && !current.dedupeKey.isEmpty()) {
            merged = current;
            merged.priority = maxPriority(current.priority, ticket.priority);
            merged.reason = normalizeReason(ticket.reason, current.reason);
            merged.payload = ticket.payload.isEmpty() ? current.payload : ticket.payload;
        } else if (static_cast<int>(ticket.priority) > static_cast<int>(current.priority)) {
            merged = ticket;
        } else {
            merged = current;
        }
    }

    agent->runtimeState.hasPendingTicket = true;
    agent->runtimeState.pendingTicket = merged;
    agent->runtimeState.laneState = HeartbeatLaneState::Deferred;
    agent->runtimeState.lastDeferredReason.clear();
    makeStateStore(m_app.m_persistence.get()).save(normalizeAgentId(agentId), &agent->runtimeState);

    emitHeartbeatEvent(QStringLiteral("heartbeat.ticket_created"),
                       agentId,
                       QJsonObject {
                           { QStringLiteral("reason"), merged.reason },
                           { QStringLiteral("ticket_kind"), heartbeatTicketKindToString(merged.kind) },
                           { QStringLiteral("priority"), heartbeatTicketPriorityToString(merged.priority) }
                       });
}

void HeartbeatService::deferTicket(const QString& agentId,
                                   AgentState* agent,
                                   const HeartbeatTicket& ticket,
                                   const QString& reason,
                                   const QDateTime& nowUtc)
{
    Q_UNUSED(nowUtc);
    if (!agent)
        return;
    agent->runtimeState.hasPendingTicket = true;
    agent->runtimeState.pendingTicket = ticket;
    agent->runtimeState.laneState = HeartbeatLaneState::Deferred;
    agent->runtimeState.lastDeferredReason = normalizeReason(reason, QStringLiteral("deferred"));
    makeStateStore(m_app.m_persistence.get()).save(agentId, &agent->runtimeState);
    emitHeartbeatEvent(QStringLiteral("heartbeat.deferred"),
                       agentId,
                       QJsonObject {
                           { QStringLiteral("reason"), agent->runtimeState.lastDeferredReason },
                           { QStringLiteral("ticket_kind"), heartbeatTicketKindToString(ticket.kind) }
                       });
}

void HeartbeatService::beginCycle(const QString& agentId,
                                  AgentState* agent,
                                  const HeartbeatTicket& ticket)
{
    if (!agent)
        return;

    const QDateTime startedAtUtc = utcNow();
    agent->cycleRunning = true;
    agent->runtimeState.hasPendingTicket = false;
    agent->runtimeState.pendingTicket = HeartbeatTicket();
    agent->runtimeState.laneState = HeartbeatLaneState::Running;
    agent->runtimeState.lastStartedAtUtc = startedAtUtc;
    agent->runtimeState.lastDeferredReason.clear();
    agent->runtimeState.interruptedRun = false;
    makeStateStore(m_app.m_persistence.get()).save(agentId, &agent->runtimeState);

    emitHeartbeatEvent(QStringLiteral("heartbeat.cycle_started"),
                       agentId,
                       QJsonObject {
                           { QStringLiteral("reason"), ticket.reason },
                           { QStringLiteral("ticket_kind"), heartbeatTicketKindToString(ticket.kind) }
                       });

    const HeartbeatSnapshot snapshot = m_snapshotService->collect(agentId);
    HeartbeatCycleResult cycleResult =
        m_decisionEngine->evaluate(agent->policy, ticket, snapshot, agent->runtimeState);

    const QString sessionId = m_executionService->ensureSessionForMaintenance(agentId);
    m_executionService->runMaintenance(sessionId, agentId, agent->policy, ticket);
    agent->runtimeState.lastMaintenanceAtUtc = utcNow();
    emitHeartbeatEvent(QStringLiteral("heartbeat.maintenance_completed"),
                       agentId,
                       QJsonObject {
                           { QStringLiteral("reason"), ticket.reason },
                           { QStringLiteral("ticket_kind"), heartbeatTicketKindToString(ticket.kind) }
                       },
                       sessionId);

    if (cycleResult.decision == HeartbeatDecision::Escalate
        && isProviderStateBlocking(snapshot.providerState)
        && ticket.kind != HeartbeatTicketKind::Manual) {
        agent->cycleRunning = false;
        deferTicket(agentId, agent, ticket, QStringLiteral("provider_down"), utcNow());
        return;
    }

    if (cycleResult.decision != HeartbeatDecision::Escalate
        || !agent->policy.llmEscalation.enabled
        || isProviderStateBlocking(snapshot.providerState)) {
        if (cycleResult.deliverToUser && cycleResult.summary.trimmed().isEmpty()) {
            cycleResult.summary = m_executionService->buildFallbackSummary(
                ticket, snapshot, cycleResult.actionableSignals);
        }
        handleCycleCompleted(
            agentId, agent, ticket, cycleResult, snapshot, startedAtUtc, digestSummary(cycleResult.summary));
        return;
    }

    Identity* agentIdentity =
        m_app.m_identityManager ? m_app.m_identityManager->findById(agentId) : nullptr;
    AgentRuntime* runtime =
        m_app.m_conversationService && agentIdentity
        ? m_app.m_conversationService->ensureRuntimeForAgent(agentIdentity)
        : nullptr;
    const QString prompt = m_executionService->buildEscalationPrompt(
        agentId, ticket, snapshot, cycleResult.actionableSignals);
    if (!runtime || prompt.trimmed().isEmpty()) {
        cycleResult.summary = m_executionService->buildFallbackSummary(
            ticket, snapshot, cycleResult.actionableSignals);
        emitHeartbeatEvent(QStringLiteral("heartbeat.failed"),
                           agentId,
                           QJsonObject {
                               { QStringLiteral("reason"), QStringLiteral("background_runtime_unavailable") }
                           },
                           sessionId,
                           QStringLiteral("background runtime unavailable"));
        handleCycleCompleted(
            agentId, agent, ticket, cycleResult, snapshot, startedAtUtc, digestSummary(cycleResult.summary));
        return;
    }

    agent->runtimeState.laneState = HeartbeatLaneState::Escalating;
    agent->runtimeState.lastEscalationAtUtc = utcNow();
    makeStateStore(m_app.m_persistence.get()).save(agentId, &agent->runtimeState);
    emitHeartbeatEvent(QStringLiteral("heartbeat.escalation_started"),
                       agentId,
                       QJsonObject {
                           { QStringLiteral("reason"), ticket.reason },
                           { QStringLiteral("ticket_kind"), heartbeatTicketKindToString(ticket.kind) }
                       },
                       sessionId);

    const QString taskId = runtime->runBackgroundTask(prompt);
    if (taskId.trimmed().isEmpty()) {
        cycleResult.summary = m_executionService->buildFallbackSummary(
            ticket, snapshot, cycleResult.actionableSignals);
        emitHeartbeatEvent(QStringLiteral("heartbeat.failed"),
                           agentId,
                           QJsonObject {
                               { QStringLiteral("reason"), QStringLiteral("background_run_rejected") }
                           },
                           sessionId,
                           QStringLiteral("background run rejected"));
        handleCycleCompleted(
            agentId, agent, ticket, cycleResult, snapshot, startedAtUtc, digestSummary(cycleResult.summary));
        return;
    }

    auto* finishConn = new QMetaObject::Connection;
    auto* errorConn = new QMetaObject::Connection;
    *finishConn = connect(
        runtime,
        &AgentRuntime::backgroundTaskFinished,
        this,
        [this,
         agentId,
         agent,
         ticket,
         snapshot,
         startedAtUtc,
         taskId,
         finishConn,
         errorConn](const QString& finishedTaskId, const QString& fullContent) {
            if (finishedTaskId != taskId)
                return;
            disconnect(*finishConn);
            disconnect(*errorConn);
            delete finishConn;
            delete errorConn;

            HeartbeatCycleResult cycleResult;
            cycleResult.decision = HeartbeatDecision::Escalate;
            cycleResult.deliverToUser = true;
            cycleResult.usedLlm = true;
            cycleResult.summary = fullContent.trimmed().isEmpty()
                ? m_executionService->buildFallbackSummary(ticket, snapshot, QStringList())
                : fullContent.trimmed();
            emitHeartbeatEvent(QStringLiteral("heartbeat.escalation_completed"),
                               agentId,
                               QJsonObject { { QStringLiteral("task_id"), taskId } });
            handleCycleCompleted(agentId,
                                 agent,
                                 ticket,
                                 cycleResult,
                                 snapshot,
                                 startedAtUtc,
                                 digestSummary(cycleResult.summary));
        });
    *errorConn = connect(
        runtime,
        &AgentRuntime::backgroundTaskError,
        this,
        [this,
         agentId,
         agent,
         ticket,
         snapshot,
         startedAtUtc,
         taskId,
         finishConn,
         errorConn](const QString& failedTaskId, const QString& errorMsg) {
            if (failedTaskId != taskId)
                return;
            disconnect(*finishConn);
            disconnect(*errorConn);
            delete finishConn;
            delete errorConn;

            HeartbeatCycleResult cycleResult;
            cycleResult.decision = HeartbeatDecision::Escalate;
            cycleResult.deliverToUser = true;
            cycleResult.summary =
                m_executionService->buildFallbackSummary(ticket, snapshot, QStringList());
            emitHeartbeatEvent(QStringLiteral("heartbeat.failed"),
                               agentId,
                               QJsonObject { { QStringLiteral("task_id"), taskId } },
                               QString(),
                               errorMsg);
            handleCycleCompleted(agentId,
                                 agent,
                                 ticket,
                                 cycleResult,
                                 snapshot,
                                 startedAtUtc,
                                 digestSummary(cycleResult.summary));
        });
}

void HeartbeatService::handleCycleCompleted(const QString& agentId,
                                            AgentState* agent,
                                            const HeartbeatTicket& ticket,
                                            const HeartbeatCycleResult& cycleResult,
                                            const HeartbeatSnapshot& snapshot,
                                            const QDateTime& startedAtUtc,
                                            const QString& summaryDigest)
{
    if (!agent)
        return;

    const QDateTime completedAtUtc = utcNow();
    agent->cycleRunning = false;
    agent->runtimeState.lastSnapshot = snapshot;
    agent->runtimeState.lastSnapshotDigest = hashCompactJson(heartbeatSnapshotToJson(snapshot));
    agent->runtimeState.lastCompletedAtUtc = completedAtUtc;
    agent->runtimeState.lastDecision = cycleResult.decision;
    agent->runtimeState.providerState = snapshot.providerState;
    agent->runtimeState.pulseState = snapshot.pulseState;
    agent->runtimeState.interruptedRun = false;

    if (ticket.kind != HeartbeatTicketKind::Manual || !agent->runtimeState.nextDueAtUtc.isValid()
        || agent->runtimeState.nextDueAtUtc <= completedAtUtc) {
        scheduleNextDue(agent, completedAtUtc);
    }

    if (cycleResult.deliverToUser && !cycleResult.summary.trimmed().isEmpty()) {
        const bool duplicateSummary =
            !summaryDigest.trimmed().isEmpty()
            && summaryDigest == agent->runtimeState.lastSummaryDigest
            && agent->runtimeState.lastDeliveredAtUtc.isValid();
        if (!duplicateSummary && m_app.m_conversationService) {
            m_app.m_conversationService->deliverHeartbeatSummary(
                agentId, cycleResult.summary.trimmed(), cycleResult.metadata);
            agent->runtimeState.lastSummaryDigest = summaryDigest;
            agent->runtimeState.lastDeliveredAtUtc = completedAtUtc;
        }
    }

    agent->runtimeState.laneState = agent->runtimeState.hasPendingTicket
        ? HeartbeatLaneState::Deferred
        : HeartbeatLaneState::Idle;
    makeStateStore(m_app.m_persistence.get()).save(agentId, &agent->runtimeState);

    QJsonObject extra = cycleResult.metadata;
    extra.insert(QStringLiteral("decision"), heartbeatDecisionToString(cycleResult.decision));
    extra.insert(QStringLiteral("deliver_to_user"), cycleResult.deliverToUser);
    extra.insert(QStringLiteral("used_llm"), cycleResult.usedLlm);
    extra.insert(QStringLiteral("duration_ms"),
                 static_cast<double>(startedAtUtc.msecsTo(completedAtUtc)));
    if (!summaryDigest.trimmed().isEmpty())
        extra.insert(QStringLiteral("summary_digest"), summaryDigest);
    emitHeartbeatEvent(QStringLiteral("heartbeat.cycle_completed"), agentId, extra);
}

QString HeartbeatService::providerStateForAgent(Identity* agent) const
{
    const QString providerId = providerIdForAgent(agent);
    if (providerId.isEmpty() || !m_app.m_memoryService || !m_app.m_memoryService->healthMonitor())
        return QStringLiteral("unknown");
    return providerStateToText(
        m_app.m_memoryService->healthMonitor()->providerState(providerId));
}

QString HeartbeatService::providerIdForAgent(Identity* agent) const
{
    if (!agent || !m_app.m_conversationService)
        return QString();
    const LLMConfig cfg = m_app.m_conversationService->composeConfigForIdentity(agent);
    return ModelFactory::resolveInstanceId(cfg).trimmed();
}

void HeartbeatService::emitHeartbeatEvent(const QString& type,
                                          const QString& agentId,
                                          const QJsonObject& extra,
                                          const QString& sessionId,
                                          const QString& error,
                                          bool persistToDisk)
{
    if (!m_app.m_conversationService)
        return;
    QJsonObject merged = extra;
    merged.insert(QStringLiteral("agent_id"), agentId);
    if (!sessionId.trimmed().isEmpty())
        merged.insert(QStringLiteral("session_id"), sessionId.trimmed());
    m_app.m_conversationService->emitPipelineEvent(
        type, sessionId, nullptr, QString(), error, merged, persistToDisk);
}
