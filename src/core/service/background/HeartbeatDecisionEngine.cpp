#include "HeartbeatDecisionEngine.h"

#include <QJsonDocument>

namespace {

QStringList changedDelegateKeys(const HeartbeatSnapshot& previous, const HeartbeatSnapshot& current)
{
    QStringList changes;
    if (previous.activeDelegateJobCount != current.activeDelegateJobCount)
        changes.append(QStringLiteral("delegate_jobs"));
    if (QJsonDocument(previous.delegateJobsSummary).toJson(QJsonDocument::Compact)
        != QJsonDocument(current.delegateJobsSummary).toJson(QJsonDocument::Compact)) {
        if (!changes.contains(QStringLiteral("delegate_jobs")))
            changes.append(QStringLiteral("delegate_jobs"));
    }
    return changes;
}

bool isPulseRiskState(const QString& state)
{
    const QString normalized = state.trimmed().toLower();
    return normalized == QLatin1String("stalled")
        || normalized == QLatin1String("hard_timeout")
        || normalized == QLatin1String("soft_timeout");
}

QStringList determineActionableSignals(const HeartbeatPolicy& policy,
                                       const HeartbeatTicket& ticket,
                                       const HeartbeatSnapshot& snapshot,
                                       const HeartbeatRuntimeState& runtimeState)
{
    QStringList actionable;
    const HeartbeatSnapshot previous = runtimeState.lastSnapshot;
    const bool hasPreviousSnapshot = previous.capturedAtUtc.isValid()
        || !runtimeState.lastSnapshotDigest.trimmed().isEmpty();

    if (ticket.kind == HeartbeatTicketKind::Recovery)
        actionable.append(QStringLiteral("restart_recovery"));

    if (policy.watchModules.provider && policy.actionableRules.providerStatus) {
        const QString currentState = snapshot.providerState.trimmed().toLower();
        const QString previousState = previous.providerState.trimmed().toLower();
        const bool stateChanged = currentState != previousState;
        if ((!hasPreviousSnapshot && (currentState == QLatin1String("down")
                                      || currentState == QLatin1String("degraded")))
            || (hasPreviousSnapshot && stateChanged)) {
            actionable.append(QStringLiteral("provider_status"));
        }
    }

    if (policy.watchModules.delegateJobs && policy.actionableRules.delegateChanges) {
        const QStringList delegateChanges = changedDelegateKeys(previous, snapshot);
        if (!delegateChanges.isEmpty())
            actionable.append(QStringLiteral("delegate_jobs"));
    }

    if (policy.watchModules.pulse && policy.actionableRules.pulseRisk) {
        const QString currentState = snapshot.pulseState.trimmed().toLower();
        const QString previousState = previous.pulseState.trimmed().toLower();
        const bool currentRisk = isPulseRiskState(currentState);
        const bool previousRisk = isPulseRiskState(previousState);
        if (currentRisk || (previousRisk && currentState == QLatin1String("healthy")))
            actionable.append(QStringLiteral("pulse_state"));
    }

    if (policy.watchModules.scheduler && policy.actionableRules.schedulerIssues && snapshot.schedulerIssue) {
        actionable.append(QStringLiteral("scheduler_issue"));
    }

    if (policy.watchModules.memory && policy.actionableRules.memoryIssues && snapshot.memoryIssue)
        actionable.append(QStringLiteral("memory_issue"));

    actionable.removeDuplicates();
    return actionable;
}

} // namespace

HeartbeatCycleResult HeartbeatDecisionEngine::evaluate(const HeartbeatPolicy& policy,
                                                       const HeartbeatTicket& ticket,
                                                       const HeartbeatSnapshot& snapshot,
                                                       const HeartbeatRuntimeState& runtimeState) const
{
    HeartbeatCycleResult result;
    result.actionableSignals =
        determineActionableSignals(policy, ticket, snapshot, runtimeState);
    result.metadata.insert(QStringLiteral("ticket_kind"),
                           heartbeatTicketKindToString(ticket.kind));
    result.metadata.insert(QStringLiteral("ticket_reason"), ticket.reason);
    result.metadata.insert(QStringLiteral("provider_state"), snapshot.providerState);
    result.metadata.insert(QStringLiteral("pulse_state"), snapshot.pulseState);
    result.metadata.insert(QStringLiteral("delegate_job_count"), snapshot.activeDelegateJobCount);
    result.metadata.insert(QStringLiteral("scheduler_issue"), snapshot.schedulerIssue);
    result.metadata.insert(QStringLiteral("memory_issue"), snapshot.memoryIssue);

    if (ticket.kind == HeartbeatTicketKind::Manual) {
        result.decision = policy.llmEscalation.enabled ? HeartbeatDecision::Escalate
                                                       : HeartbeatDecision::MaintainOnly;
        result.deliverToUser = true;
        result.metadata.insert(QStringLiteral("manual"), true);
        return result;
    }

    if (!result.actionableSignals.isEmpty()) {
        result.decision = policy.llmEscalation.enabled ? HeartbeatDecision::Escalate
                                                       : HeartbeatDecision::MaintainOnly;
        result.deliverToUser = policy.deliveryPolicy.deliverActionableSummary;
        return result;
    }

    result.decision = HeartbeatDecision::MaintainOnly;
    result.deliverToUser = false;
    return result;
}
