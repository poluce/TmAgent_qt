#include "HeartbeatSnapshotCoordinator.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace {

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

} // namespace

HeartbeatSnapshotCoordinator::Result HeartbeatSnapshotCoordinator::evaluate(const Inputs& inputs)
{
    Result result;
    if (inputs.agentId.trimmed().isEmpty() || !inputs.nowUtc.isValid())
        return result;

    result.valid = true;
    result.reasonLabel = inputs.reason.trimmed().isEmpty()
        ? QStringLiteral("interval")
        : inputs.reason.trimmed();
    result.forceInteractive =
        (result.reasonLabel == QLatin1String("manual_ui")
         || result.reasonLabel == QLatin1String("requested"));
    result.runtimeState = inputs.runtimeState;

    const QSet<QString> enabledSignals(inputs.config.snapshotSignals.begin(), inputs.config.snapshotSignals.end());
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
        if (inputs.schedulerNextFireAtUtc.isValid())
            snapshot.insert(QStringLiteral("scheduler_next_fire_at_utc"),
                            inputs.schedulerNextFireAtUtc.toUTC().toString(Qt::ISODateWithMs));
    }
    if (watchMemory) {
        snapshot.insert(QStringLiteral("memory_retained_turns"), inputs.memoryRetainedTurns);
        if (inputs.memoryDocSizeBytes >= 0)
            snapshot.insert(QStringLiteral("memory_doc_size_bytes"),
                            static_cast<double>(inputs.memoryDocSizeBytes));
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
    result.runtimeState.stateObj.insert(QStringLiteral("active_jobs_count"),
                                        inputs.activeJobs.size());
    result.runtimeState.stateObj.insert(QStringLiteral("provider_down"),
                                        inputs.providerDown);
    if (result.hasChange) {
        result.runtimeState.stateObj.insert(QStringLiteral("last_change_at_utc"),
                                            inputs.nowUtc.toString(Qt::ISODateWithMs));
    }

    const bool persistIntervalElapsed = (!result.runtimeState.lastPersistAtUtc.isValid())
        || (result.runtimeState.lastPersistAtUtc.msecsTo(inputs.nowUtc)
            >= qMax(1000, inputs.config.statePersistIntervalMs));
    result.shouldPersistState =
        result.hasChange
        || inputs.config.persistStateOnNoChange
        || persistIntervalElapsed
        || result.forceInteractive;

    result.triggeredExtra.insert(QStringLiteral("agent_id"), inputs.agentId);
    result.triggeredExtra.insert(QStringLiteral("reason"), result.reasonLabel);
    result.triggeredExtra.insert(QStringLiteral("has_change"), result.hasChange);
    result.triggeredExtra.insert(QStringLiteral("has_actionable_change"), result.hasActionableChange);
    result.triggeredExtra.insert(QStringLiteral("first_snapshot"), !hadPreviousSnapshot);
    result.triggeredExtra.insert(QStringLiteral("active_delegate_jobs"),
                                 inputs.activeJobs.size());
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
    } else if (!result.forceInteractive && inputs.config.notifyOnChangeOnly
               && !result.hasChange) {
        result.shouldNotify = false;
        result.skipReason = QStringLiteral("notify_on_change_only");
    } else if (!result.forceInteractive
               && inputs.config.notifyMinIntervalMs > 0
               && !result.hasChange
               && result.runtimeState.lastNotifyAtUtc.isValid()
               && result.runtimeState.lastNotifyAtUtc.msecsTo(inputs.nowUtc)
                   < inputs.config.notifyMinIntervalMs) {
        result.shouldNotify = false;
        result.skipReason = QStringLiteral("notify_rate_limited");
    }

    return result;
}
