#include "MemoryService.h"

#include "ApplicationServices.h"
#include "ConversationService.h"
#include "AgentPulse.h"
#include "AgentPulseRegistry.h"
#include "HealthMonitor.h"
#include "HeartbeatPromptBuilder.h"
#include "HeartbeatService.h"
#include "HeartbeatStateStore.h"
#include "SchedulerService.h"
#include "ChatCoordinatorSupport.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/manager/IdentityManager.h"
#include "core/model/Identity.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "llm/ModelFactory.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace {
using ChatCoordinatorSupport::isBackgroundHeartbeatClientMessageId;
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

QStringList changedTopLevelKeysLocal(const QJsonObject& previous, const QJsonObject& current)
{
    QStringList changed;
    QSet<QString> keys;
    for (auto it = previous.constBegin(); it != previous.constEnd(); ++it)
        keys.insert(it.key());
    for (auto it = current.constBegin(); it != current.constEnd(); ++it)
        keys.insert(it.key());

    for (const QString& key : keys) {
        if (previous.value(key) != current.value(key))
            changed.append(key);
    }
    std::sort(changed.begin(), changed.end());
    return changed;
}

struct HeartbeatSnapshotState {
    bool hasSnapshot = false;
    QJsonObject stateObj;
    QJsonObject lastSnapshotObj;
    QString lastSnapshotDigest;
    QDateTime lastNotifyAtUtc;
    QDateTime lastPersistAtUtc;
};

struct HeartbeatSnapshotInputs {
    QString agentId;
    QString reason;
    HeartbeatConfig config;
    QString providerId;
    bool providerDown = false;
    QList<DelegateTaskScheduler::JobInfo> activeJobs;
    QString pulseState;
    int schedulerEnabledJobs = 0;
    QDateTime schedulerNextFireAtUtc;
    int memoryRetainedTurns = 0;
    qint64 memoryDocSizeBytes = -1;
    HeartbeatSnapshotState runtimeState;
    QDateTime nowUtc;
};

struct HeartbeatSnapshotResult {
    bool valid = false;
    QString reasonLabel;
    bool forceInteractive = false;
    bool hasChange = false;
    bool hasActionableChange = false;
    bool shouldNotify = true;
    QString skipReason;
    bool shouldPersistState = false;
    HeartbeatSnapshotState runtimeState;
    QJsonObject triggeredExtra;
};

HeartbeatSnapshotResult evaluateHeartbeatSnapshot(const HeartbeatSnapshotInputs& inputs)
{
    HeartbeatSnapshotResult result;
    if (inputs.agentId.trimmed().isEmpty() || !inputs.nowUtc.isValid())
        return result;

    result.valid = true;
    result.reasonLabel = inputs.reason.trimmed().isEmpty()
        ? QStringLiteral("interval")
        : inputs.reason.trimmed();
    result.forceInteractive = (result.reasonLabel == QLatin1String("manual_ui")
                               || result.reasonLabel == QLatin1String("requested"));
    result.runtimeState = inputs.runtimeState;

    const QSet<QString> enabledSignals(inputs.config.snapshotSignals.begin(),
                                       inputs.config.snapshotSignals.end());
    const bool watchProvider = enabledSignals.contains(QStringLiteral("provider_status"));
    const bool watchDelegate = enabledSignals.contains(QStringLiteral("delegate_jobs"));
    const bool watchPulse = enabledSignals.contains(QStringLiteral("pulse_state"));
    const bool watchScheduler = enabledSignals.contains(QStringLiteral("scheduler_jobs"));
    const bool watchMemory = enabledSignals.contains(QStringLiteral("memory_progress"));

    QJsonObject snapshot;
    QJsonArray signalArr;
    for (const QString& s : inputs.config.snapshotSignals)
        signalArr.append(s);
    snapshot.insert(QStringLiteral("watch_signals"), signalArr);
    if (watchProvider) {
        snapshot.insert(QStringLiteral("provider_down"), inputs.providerDown);
        snapshot.insert(QStringLiteral("provider_id"), inputs.providerId);
    }
    if (watchDelegate) {
        snapshot.insert(QStringLiteral("active_jobs_count"), inputs.activeJobs.size());
        QJsonArray jobsArr;
        for (const DelegateTaskScheduler::JobInfo& job : inputs.activeJobs) {
            QJsonObject item;
            item.insert(QStringLiteral("job_id"), job.jobId.trimmed());
            item.insert(QStringLiteral("status"), job.status.trimmed());
            item.insert(QStringLiteral("summary"), job.summary.left(120));
            jobsArr.append(item);
            if (jobsArr.size() >= 10)
                break;
        }
        snapshot.insert(QStringLiteral("active_jobs"), jobsArr);
    }
    if (watchPulse && !inputs.pulseState.trimmed().isEmpty())
        snapshot.insert(QStringLiteral("pulse_state"), inputs.pulseState.trimmed());
    if (watchScheduler) {
        snapshot.insert(QStringLiteral("scheduler_enabled_jobs"), inputs.schedulerEnabledJobs);
        if (inputs.schedulerNextFireAtUtc.isValid()) {
            snapshot.insert(QStringLiteral("scheduler_next_fire_at_utc"),
                            inputs.schedulerNextFireAtUtc.toUTC().toString(Qt::ISODateWithMs));
        }
    }
    if (watchMemory) {
        snapshot.insert(QStringLiteral("memory_retained_turns"), inputs.memoryRetainedTurns);
        if (inputs.memoryDocSizeBytes >= 0) {
            snapshot.insert(QStringLiteral("memory_doc_size_bytes"),
                            static_cast<double>(inputs.memoryDocSizeBytes));
        }
    }

    const QByteArray snapshotBytes = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
    const QString snapshotDigest = QString::fromLatin1(
        QCryptographicHash::hash(snapshotBytes, QCryptographicHash::Sha1).toHex());

    const bool hadPreviousSnapshot = result.runtimeState.hasSnapshot;
    const QStringList changedKeys = hadPreviousSnapshot
        ? changedTopLevelKeysLocal(result.runtimeState.lastSnapshotObj, snapshot)
        : QStringList();
    result.hasChange = hadPreviousSnapshot && !changedKeys.isEmpty();

    const bool changedProvider = changedKeys.contains(QStringLiteral("provider_down"))
        || changedKeys.contains(QStringLiteral("provider_id"));
    const bool changedDelegate = changedKeys.contains(QStringLiteral("active_jobs_count"))
        || changedKeys.contains(QStringLiteral("active_jobs"));
    const bool changedScheduler = changedKeys.contains(QStringLiteral("scheduler_enabled_jobs"))
        || changedKeys.contains(QStringLiteral("scheduler_next_fire_at_utc"));
    result.hasActionableChange = changedProvider || changedDelegate || changedScheduler;

    result.runtimeState.hasSnapshot = true;
    result.runtimeState.lastSnapshotObj = snapshot;
    result.runtimeState.lastSnapshotDigest = snapshotDigest;
    result.runtimeState.stateObj.insert(QStringLiteral("last_snapshot"), snapshot);
    result.runtimeState.stateObj.insert(QStringLiteral("last_snapshot_digest"), snapshotDigest);
    result.runtimeState.stateObj.insert(QStringLiteral("last_snapshot_at_utc"),
                                        inputs.nowUtc.toString(Qt::ISODateWithMs));
    result.runtimeState.stateObj.insert(QStringLiteral("last_reason"), result.reasonLabel);
    result.runtimeState.stateObj.insert(QStringLiteral("watch_signals"), signalArr);
    result.runtimeState.stateObj.insert(QStringLiteral("active_jobs_count"), inputs.activeJobs.size());
    result.runtimeState.stateObj.insert(QStringLiteral("provider_down"), inputs.providerDown);
    if (result.hasChange) {
        result.runtimeState.stateObj.insert(QStringLiteral("last_change_at_utc"),
                                            inputs.nowUtc.toString(Qt::ISODateWithMs));
    }

    const bool persistIntervalElapsed = (!result.runtimeState.lastPersistAtUtc.isValid())
        || (result.runtimeState.lastPersistAtUtc.msecsTo(inputs.nowUtc)
            >= qMax(1000, inputs.config.statePersistIntervalMs));
    result.shouldPersistState =
        result.hasChange || inputs.config.persistStateOnNoChange || persistIntervalElapsed
        || result.forceInteractive;

    result.triggeredExtra.insert(QStringLiteral("agent_id"), inputs.agentId);
    result.triggeredExtra.insert(QStringLiteral("reason"), result.reasonLabel);
    result.triggeredExtra.insert(QStringLiteral("has_change"), result.hasChange);
    result.triggeredExtra.insert(QStringLiteral("has_actionable_change"), result.hasActionableChange);
    result.triggeredExtra.insert(QStringLiteral("first_snapshot"), !hadPreviousSnapshot);
    result.triggeredExtra.insert(QStringLiteral("active_delegate_jobs"), inputs.activeJobs.size());
    if (!inputs.providerId.isEmpty())
        result.triggeredExtra.insert(QStringLiteral("provider_id"), inputs.providerId);
    if (!changedKeys.isEmpty()) {
        QJsonArray changedArr;
        for (const QString& key : changedKeys)
            changedArr.append(key);
        result.triggeredExtra.insert(QStringLiteral("changed_keys"), changedArr);
    }

    if (inputs.providerDown) {
        result.shouldNotify = false;
        result.skipReason = QStringLiteral("provider_down");
        return result;
    }

    result.shouldNotify = true;
    if (!result.forceInteractive && result.hasChange && !result.hasActionableChange) {
        result.shouldNotify = false;
        result.skipReason = QStringLiteral("non_actionable_change");
    } else if (!result.forceInteractive && !result.hasChange
               && inputs.config.silentWhenNoChange) {
        result.shouldNotify = false;
        result.skipReason = QStringLiteral("silent_no_change");
    } else if (!result.forceInteractive && inputs.config.notifyOnChangeOnly && !result.hasChange) {
        result.shouldNotify = false;
        result.skipReason = QStringLiteral("notify_on_change_only");
    } else if (!result.forceInteractive && inputs.config.notifyMinIntervalMs > 0
               && !result.hasChange && result.runtimeState.lastNotifyAtUtc.isValid()
               && result.runtimeState.lastNotifyAtUtc.msecsTo(inputs.nowUtc)
                   < inputs.config.notifyMinIntervalMs) {
        result.shouldNotify = false;
        result.skipReason = QStringLiteral("notify_rate_limited");
    }

    return result;
}
} // namespace

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

    const QString reasonLabel = reason.trimmed().isEmpty() ? QStringLiteral("interval")
                                                           : reason.trimmed();
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
    if (watchProvider && m_healthMonitor) {
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

    HeartbeatSnapshotInputs snapshotInputs;
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

    const HeartbeatSnapshotResult snapshotResult = evaluateHeartbeatSnapshot(snapshotInputs);
    if (!snapshotResult.valid)
        return;

    runtimeState.hasSnapshot = snapshotResult.runtimeState.hasSnapshot;
    runtimeState.stateObj = snapshotResult.runtimeState.stateObj;
    runtimeState.lastSnapshotObj = snapshotResult.runtimeState.lastSnapshotObj;
    runtimeState.lastSnapshotDigest = snapshotResult.runtimeState.lastSnapshotDigest;
    runtimeState.lastNotifyAtUtc = snapshotResult.runtimeState.lastNotifyAtUtc;
    runtimeState.lastPersistAtUtc = snapshotResult.runtimeState.lastPersistAtUtc;

    bool shouldPersistState = snapshotResult.shouldPersistState;
    auto persistStateIfNeeded = [&](bool forcePersist) {
        const bool doPersist = forcePersist || shouldPersistState;
        stateStore.persist(trimmedAgentId, &runtimeState, nowUtc, doPersist);
    };
    const QJsonObject triggeredExtra = snapshotResult.triggeredExtra;
    m_app.m_conversationService->emitPipelineEvent(QStringLiteral("heartbeat.triggered"),
                                                   QString(),
                                                   nullptr,
                                                   QString(),
                                                   QString(),
                                                   triggeredExtra);

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
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("heartbeat.completed"),
                                                       QString(),
                                                       nullptr,
                                                       QString(),
                                                       QString(),
                                                       completeExtra);
        return;
    }

    const QString sessionId =
        resolvePrimarySessionForAgent(trimmedAgentId, true, false, QStringLiteral("heartbeat"));
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

    if (!snapshotResult.forceInteractive) {
        const int pipelineDepth = m_app.m_conversationService->turnManager().totalDepth(sessionId);
        if (pipelineDepth > 0) {
            persistStateIfNeeded(false);
            QJsonObject extra = triggeredExtra;
            extra.insert(QStringLiteral("session_id"), sessionId);
            extra.insert(QStringLiteral("queue_depth"), pipelineDepth);
            extra.insert(QStringLiteral("reason"), QStringLiteral("pipeline_busy"));
            m_app.m_conversationService->emitPipelineEvent(QStringLiteral("heartbeat.skipped"),
                                                           sessionId,
                                                           nullptr,
                                                           QString(),
                                                           QStringLiteral("pipeline_busy"),
                                                           extra,
                                                           true);
            return;
        }
    }

    const HeartbeatPromptBuilder promptBuilder(
        HeartbeatPromptBuilder::Dependencies {
            [this](const QString& aid) { return heartbeatPathForAgent(aid); }
        });
    QString prompt = promptBuilder.build(trimmedAgentId, reasonLabel);
    if (snapshotResult.hasChange) {
        QStringList delta;
        if (watchDelegate) {
            if (activeJobs.isEmpty()) {
                delta << QStringLiteral("活跃子代理任务: 0");
            } else {
                delta << QStringLiteral("活跃子代理任务: %1").arg(activeJobs.size());
                const DelegateTaskScheduler::JobInfo& first = activeJobs.first();
                if (!first.jobId.trimmed().isEmpty()) {
                    delta << QStringLiteral("首个任务：job_id=%1 status=%2")
                                 .arg(first.jobId.trimmed(),
                                      first.status.trimmed().isEmpty()
                                          ? QStringLiteral("running")
                                          : first.status.trimmed());
                }
            }
        }
        if (watchProvider && !providerId.trimmed().isEmpty()) {
            delta << QStringLiteral("Provider 状态: %1")
                        .arg(providerDown ? QStringLiteral("down") : QStringLiteral("up"));
        }
        if (watchPulse) {
            AgentPulse* pulse = m_agentPulseRegistry ? m_agentPulseRegistry->find(trimmedAgentId)
                                                     : nullptr;
            if (pulse)
                delta << QStringLiteral("主代理状态: %1").arg(pulseStateToString(pulse->currentState()));
        }
        if (delta.isEmpty())
            delta << QStringLiteral("状态发生变化。");
        prompt += QStringLiteral("\n\n[Heartbeat Delta]\n") + delta.join(QStringLiteral("\n"));
    } else if (snapshotResult.forceInteractive) {
        prompt += QStringLiteral("\n\n[Heartbeat Delta]\n当前无关键变化。");
    }

    const QString clientMessageId = QStringLiteral("%1-%2")
                                        .arg(snapshotResult.forceInteractive
                                                 ? QStringLiteral("heartbeat-manual")
                                                 : QStringLiteral("heartbeat-bg"),
                                             QUuid::createUuid().toString(QUuid::WithoutBraces));

    const QString actorId =
        m_app.m_identityManager && m_app.m_identityManager->userIdentity()
        ? m_app.m_identityManager->userIdentity()->id()
        : QString();
    const QString turnId = m_app.m_conversationService->enqueueUserMessageAs(
        actorId, sessionId, prompt, clientMessageId);
    if (turnId.isEmpty()) {
        persistStateIfNeeded(false);
        QJsonObject extra = triggeredExtra;
        extra.insert(QStringLiteral("session_id"), sessionId);
        extra.insert(QStringLiteral("reason"), QStringLiteral("enqueue_failed"));
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("heartbeat.failed"),
                                                       sessionId,
                                                       nullptr,
                                                       QString(),
                                                       QStringLiteral("enqueue_failed"),
                                                       extra,
                                                       true);
        return;
    }

    runtimeState.lastNotifyAtUtc = nowUtc;
    runtimeState.stateObj.insert(QStringLiteral("last_notify_at_utc"),
                                 nowUtc.toString(Qt::ISODateWithMs));
    shouldPersistState = true;
    persistStateIfNeeded(true);

    QJsonObject completeExtra = triggeredExtra;
    completeExtra.insert(QStringLiteral("session_id"), sessionId);
    completeExtra.insert(QStringLiteral("turn_id"), turnId);
    m_app.m_conversationService->emitPipelineEvent(QStringLiteral("heartbeat.completed"),
                                                   sessionId,
                                                   nullptr,
                                                   QString(),
                                                   QString(),
                                                   completeExtra,
                                                   true);
}

void MemoryService::onHeartbeatSkipped(const QString& agentId, const QString& reason)
{
    if (!m_app.m_conversationService)
        return;
    QJsonObject extra;
    extra.insert(QStringLiteral("agent_id"), agentId);
    extra.insert(QStringLiteral("reason"), reason);
    m_app.m_conversationService->emitPipelineEvent(QStringLiteral("heartbeat.skipped"),
                                                   QString(),
                                                   nullptr,
                                                   QString(),
                                                   reason,
                                                   extra);
}
