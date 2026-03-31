#include "HeartbeatRuntimeState.h"

#include <QJsonDocument>
#include <QTimeZone>

namespace {

QTime parseTimeOrDefault(const QString& text, const QTime& fallback)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return fallback;

    QTime time = QTime::fromString(trimmed, QStringLiteral("HH:mm"));
    if (!time.isValid())
        time = QTime::fromString(trimmed, Qt::ISODate);
    return time.isValid() ? time : fallback;
}

QString defaultTimezone()
{
    return QString::fromUtf8(QTimeZone::systemTimeZoneId());
}

} // namespace

QString heartbeatTicketKindToString(HeartbeatTicketKind kind)
{
    switch (kind) {
    case HeartbeatTicketKind::Auto:
        return QStringLiteral("auto");
    case HeartbeatTicketKind::Manual:
        return QStringLiteral("manual");
    case HeartbeatTicketKind::Recovery:
        return QStringLiteral("recovery");
    case HeartbeatTicketKind::EventDriven:
        return QStringLiteral("event_driven");
    }
    return QStringLiteral("auto");
}

HeartbeatTicketKind heartbeatTicketKindFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("manual"))
        return HeartbeatTicketKind::Manual;
    if (normalized == QLatin1String("recovery"))
        return HeartbeatTicketKind::Recovery;
    if (normalized == QLatin1String("event_driven"))
        return HeartbeatTicketKind::EventDriven;
    return HeartbeatTicketKind::Auto;
}

QString heartbeatTicketPriorityToString(HeartbeatTicketPriority priority)
{
    switch (priority) {
    case HeartbeatTicketPriority::Low:
        return QStringLiteral("low");
    case HeartbeatTicketPriority::Normal:
        return QStringLiteral("normal");
    case HeartbeatTicketPriority::High:
        return QStringLiteral("high");
    case HeartbeatTicketPriority::Critical:
        return QStringLiteral("critical");
    }
    return QStringLiteral("normal");
}

HeartbeatTicketPriority heartbeatTicketPriorityFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("low"))
        return HeartbeatTicketPriority::Low;
    if (normalized == QLatin1String("high"))
        return HeartbeatTicketPriority::High;
    if (normalized == QLatin1String("critical"))
        return HeartbeatTicketPriority::Critical;
    return HeartbeatTicketPriority::Normal;
}

QString heartbeatDecisionToString(HeartbeatDecision decision)
{
    switch (decision) {
    case HeartbeatDecision::Noop:
        return QStringLiteral("noop");
    case HeartbeatDecision::MaintainOnly:
        return QStringLiteral("maintain_only");
    case HeartbeatDecision::Escalate:
        return QStringLiteral("escalate");
    }
    return QStringLiteral("noop");
}

HeartbeatDecision heartbeatDecisionFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("maintain_only"))
        return HeartbeatDecision::MaintainOnly;
    if (normalized == QLatin1String("escalate"))
        return HeartbeatDecision::Escalate;
    return HeartbeatDecision::Noop;
}

QString heartbeatLaneStateToString(HeartbeatLaneState state)
{
    switch (state) {
    case HeartbeatLaneState::Idle:
        return QStringLiteral("idle");
    case HeartbeatLaneState::Deferred:
        return QStringLiteral("deferred");
    case HeartbeatLaneState::Running:
        return QStringLiteral("running");
    case HeartbeatLaneState::Escalating:
        return QStringLiteral("escalating");
    }
    return QStringLiteral("idle");
}

HeartbeatLaneState heartbeatLaneStateFromString(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("deferred"))
        return HeartbeatLaneState::Deferred;
    if (normalized == QLatin1String("running"))
        return HeartbeatLaneState::Running;
    if (normalized == QLatin1String("escalating"))
        return HeartbeatLaneState::Escalating;
    return HeartbeatLaneState::Idle;
}

QJsonObject heartbeatTicketToJson(const HeartbeatTicket& ticket)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("kind"), heartbeatTicketKindToString(ticket.kind));
    obj.insert(QStringLiteral("priority"), heartbeatTicketPriorityToString(ticket.priority));
    obj.insert(QStringLiteral("reason"), ticket.reason);
    obj.insert(QStringLiteral("dedupe_key"), ticket.dedupeKey);
    if (ticket.requestedAtUtc.isValid())
        obj.insert(QStringLiteral("requested_at_utc"), ticket.requestedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    if (!ticket.payload.isEmpty())
        obj.insert(QStringLiteral("payload"), ticket.payload);
    return obj;
}

HeartbeatTicket heartbeatTicketFromJson(const QJsonObject& obj)
{
    HeartbeatTicket ticket;
    ticket.kind = heartbeatTicketKindFromString(obj.value(QStringLiteral("kind")).toString());
    ticket.priority =
        heartbeatTicketPriorityFromString(obj.value(QStringLiteral("priority")).toString());
    ticket.reason = obj.value(QStringLiteral("reason")).toString().trimmed();
    ticket.dedupeKey = obj.value(QStringLiteral("dedupe_key")).toString().trimmed();
    ticket.requestedAtUtc = QDateTime::fromString(
        obj.value(QStringLiteral("requested_at_utc")).toString().trimmed(),
        Qt::ISODateWithMs);
    if (!ticket.requestedAtUtc.isValid()) {
        ticket.requestedAtUtc = QDateTime::fromString(
            obj.value(QStringLiteral("requested_at_utc")).toString().trimmed(),
            Qt::ISODate);
    }
    if (ticket.requestedAtUtc.isValid())
        ticket.requestedAtUtc = ticket.requestedAtUtc.toUTC();
    ticket.payload = obj.value(QStringLiteral("payload")).toObject();
    return ticket;
}

QJsonObject heartbeatPolicyToJson(const HeartbeatPolicy& policy)
{
    QJsonObject watchModules;
    watchModules.insert(QStringLiteral("provider"), policy.watchModules.provider);
    watchModules.insert(QStringLiteral("delegate_jobs"), policy.watchModules.delegateJobs);
    watchModules.insert(QStringLiteral("pulse"), policy.watchModules.pulse);
    watchModules.insert(QStringLiteral("scheduler"), policy.watchModules.scheduler);
    watchModules.insert(QStringLiteral("memory"), policy.watchModules.memory);

    QJsonObject actionableRules;
    actionableRules.insert(QStringLiteral("delegate_changes"), policy.actionableRules.delegateChanges);
    actionableRules.insert(QStringLiteral("provider_status"), policy.actionableRules.providerStatus);
    actionableRules.insert(QStringLiteral("pulse_risk"), policy.actionableRules.pulseRisk);
    actionableRules.insert(QStringLiteral("memory_issues"), policy.actionableRules.memoryIssues);
    actionableRules.insert(QStringLiteral("scheduler_issues"), policy.actionableRules.schedulerIssues);

    QJsonObject deliveryPolicy;
    deliveryPolicy.insert(QStringLiteral("deliver_actionable_summary"),
                          policy.deliveryPolicy.deliverActionableSummary);

    QJsonObject llmEscalation;
    llmEscalation.insert(QStringLiteral("enabled"), policy.llmEscalation.enabled);

    QJsonObject maintenancePolicy;
    maintenancePolicy.insert(QStringLiteral("reflect_memory"),
                             policy.maintenancePolicy.reflectMemory);
    maintenancePolicy.insert(QStringLiteral("rebuild_memory_index"),
                             policy.maintenancePolicy.rebuildMemoryIndex);

    QJsonObject activeHours;
    activeHours.insert(QStringLiteral("start"),
                       policy.activeHours.start.isValid()
                           ? policy.activeHours.start.toString(QStringLiteral("HH:mm"))
                           : QStringLiteral("08:00"));
    activeHours.insert(QStringLiteral("end"),
                       policy.activeHours.end.isValid()
                           ? policy.activeHours.end.toString(QStringLiteral("HH:mm"))
                           : QStringLiteral("23:00"));
    activeHours.insert(QStringLiteral("timezone"),
                       policy.activeHours.timezone.trimmed().isEmpty()
                           ? defaultTimezone()
                           : policy.activeHours.timezone.trimmed());

    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), 1);
    obj.insert(QStringLiteral("enabled"), policy.enabled);
    obj.insert(QStringLiteral("cadence_ms"), policy.cadenceMs);
    obj.insert(QStringLiteral("startup_grace_ms"), policy.startupGraceMs);
    obj.insert(QStringLiteral("coalesce_ms"), policy.coalesceMs);
    obj.insert(QStringLiteral("active_hours"), activeHours);
    obj.insert(QStringLiteral("watch_modules"), watchModules);
    obj.insert(QStringLiteral("actionable_rules"), actionableRules);
    obj.insert(QStringLiteral("delivery_policy"), deliveryPolicy);
    obj.insert(QStringLiteral("llm_escalation"), llmEscalation);
    obj.insert(QStringLiteral("maintenance_policy"), maintenancePolicy);
    obj.insert(QStringLiteral("instruction_path"), policy.instructionPath);
    return obj;
}

HeartbeatPolicy heartbeatPolicyFromJson(const QJsonObject& obj)
{
    HeartbeatPolicy policy;
    policy.enabled = obj.value(QStringLiteral("enabled")).toBool(policy.enabled);
    policy.cadenceMs = qMax(1000, obj.value(QStringLiteral("cadence_ms")).toInt(policy.cadenceMs));
    policy.startupGraceMs =
        qMax(0, obj.value(QStringLiteral("startup_grace_ms")).toInt(policy.startupGraceMs));
    policy.coalesceMs = qMax(10, obj.value(QStringLiteral("coalesce_ms")).toInt(policy.coalesceMs));

    const QJsonObject activeHours = obj.value(QStringLiteral("active_hours")).toObject();
    policy.activeHours.start =
        parseTimeOrDefault(activeHours.value(QStringLiteral("start")).toString(), QTime(8, 0));
    policy.activeHours.end =
        parseTimeOrDefault(activeHours.value(QStringLiteral("end")).toString(), QTime(23, 0));
    policy.activeHours.timezone =
        activeHours.value(QStringLiteral("timezone")).toString().trimmed();
    if (policy.activeHours.timezone.isEmpty())
        policy.activeHours.timezone = defaultTimezone();

    const QJsonObject watchModules = obj.value(QStringLiteral("watch_modules")).toObject();
    policy.watchModules.provider =
        watchModules.value(QStringLiteral("provider")).toBool(policy.watchModules.provider);
    policy.watchModules.delegateJobs = watchModules.value(QStringLiteral("delegate_jobs"))
                                           .toBool(policy.watchModules.delegateJobs);
    policy.watchModules.pulse =
        watchModules.value(QStringLiteral("pulse")).toBool(policy.watchModules.pulse);
    policy.watchModules.scheduler =
        watchModules.value(QStringLiteral("scheduler")).toBool(policy.watchModules.scheduler);
    policy.watchModules.memory =
        watchModules.value(QStringLiteral("memory")).toBool(policy.watchModules.memory);

    const QJsonObject actionableRules = obj.value(QStringLiteral("actionable_rules")).toObject();
    policy.actionableRules.delegateChanges = actionableRules
                                                 .value(QStringLiteral("delegate_changes"))
                                                 .toBool(policy.actionableRules.delegateChanges);
    policy.actionableRules.providerStatus = actionableRules
                                                .value(QStringLiteral("provider_status"))
                                                .toBool(policy.actionableRules.providerStatus);
    policy.actionableRules.pulseRisk = actionableRules.value(QStringLiteral("pulse_risk"))
                                           .toBool(policy.actionableRules.pulseRisk);
    policy.actionableRules.memoryIssues = actionableRules.value(QStringLiteral("memory_issues"))
                                              .toBool(policy.actionableRules.memoryIssues);
    policy.actionableRules.schedulerIssues = actionableRules
                                                 .value(QStringLiteral("scheduler_issues"))
                                                 .toBool(policy.actionableRules.schedulerIssues);

    const QJsonObject deliveryPolicy = obj.value(QStringLiteral("delivery_policy")).toObject();
    policy.deliveryPolicy.deliverActionableSummary =
        deliveryPolicy.value(QStringLiteral("deliver_actionable_summary"))
            .toBool(policy.deliveryPolicy.deliverActionableSummary);

    const QJsonObject llmEscalation = obj.value(QStringLiteral("llm_escalation")).toObject();
    policy.llmEscalation.enabled =
        llmEscalation.value(QStringLiteral("enabled")).toBool(policy.llmEscalation.enabled);

    const QJsonObject maintenancePolicy = obj.value(QStringLiteral("maintenance_policy")).toObject();
    policy.maintenancePolicy.reflectMemory = maintenancePolicy
                                                 .value(QStringLiteral("reflect_memory"))
                                                 .toBool(policy.maintenancePolicy.reflectMemory);
    policy.maintenancePolicy.rebuildMemoryIndex =
        maintenancePolicy.value(QStringLiteral("rebuild_memory_index"))
            .toBool(policy.maintenancePolicy.rebuildMemoryIndex);

    policy.instructionPath = obj.value(QStringLiteral("instruction_path")).toString().trimmed();
    return policy;
}

QJsonObject heartbeatSnapshotToJson(const HeartbeatSnapshot& snapshot)
{
    QJsonObject obj;
    if (snapshot.capturedAtUtc.isValid())
        obj.insert(QStringLiteral("captured_at_utc"),
                   snapshot.capturedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    obj.insert(QStringLiteral("provider_state"), snapshot.providerState);
    obj.insert(QStringLiteral("provider_id"), snapshot.providerId);
    obj.insert(QStringLiteral("active_delegate_job_count"), snapshot.activeDelegateJobCount);
    obj.insert(QStringLiteral("delegate_jobs_summary"), snapshot.delegateJobsSummary);
    obj.insert(QStringLiteral("pulse_state"), snapshot.pulseState);
    obj.insert(QStringLiteral("scheduler_enabled_jobs"), snapshot.schedulerEnabledJobs);
    obj.insert(QStringLiteral("scheduler_issue"), snapshot.schedulerIssue);
    obj.insert(QStringLiteral("memory_retained_turns"), snapshot.memoryRetainedTurns);
    obj.insert(QStringLiteral("memory_doc_size_bytes"),
               static_cast<double>(snapshot.memoryDocSizeBytes));
    obj.insert(QStringLiteral("memory_issue"), snapshot.memoryIssue);
    return obj;
}

HeartbeatSnapshot heartbeatSnapshotFromJson(const QJsonObject& obj)
{
    HeartbeatSnapshot snapshot;
    snapshot.capturedAtUtc = QDateTime::fromString(
        obj.value(QStringLiteral("captured_at_utc")).toString().trimmed(),
        Qt::ISODateWithMs);
    if (!snapshot.capturedAtUtc.isValid()) {
        snapshot.capturedAtUtc = QDateTime::fromString(
            obj.value(QStringLiteral("captured_at_utc")).toString().trimmed(),
            Qt::ISODate);
    }
    if (snapshot.capturedAtUtc.isValid())
        snapshot.capturedAtUtc = snapshot.capturedAtUtc.toUTC();
    snapshot.providerState = obj.value(QStringLiteral("provider_state")).toString().trimmed();
    snapshot.providerId = obj.value(QStringLiteral("provider_id")).toString().trimmed();
    snapshot.activeDelegateJobCount =
        obj.value(QStringLiteral("active_delegate_job_count")).toInt(0);
    snapshot.delegateJobsSummary = obj.value(QStringLiteral("delegate_jobs_summary")).toObject();
    snapshot.pulseState = obj.value(QStringLiteral("pulse_state")).toString().trimmed();
    snapshot.schedulerEnabledJobs = obj.value(QStringLiteral("scheduler_enabled_jobs")).toInt(0);
    snapshot.schedulerIssue = obj.value(QStringLiteral("scheduler_issue")).toBool(false);
    snapshot.memoryRetainedTurns = obj.value(QStringLiteral("memory_retained_turns")).toInt(0);
    snapshot.memoryDocSizeBytes = static_cast<qint64>(
        obj.value(QStringLiteral("memory_doc_size_bytes")).toDouble(-1));
    snapshot.memoryIssue = obj.value(QStringLiteral("memory_issue")).toBool(false);
    return snapshot;
}

QJsonObject heartbeatRuntimeStateToJson(const HeartbeatRuntimeState& state)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("lane_state"), heartbeatLaneStateToString(state.laneState));
    obj.insert(QStringLiteral("last_snapshot"), heartbeatSnapshotToJson(state.lastSnapshot));
    obj.insert(QStringLiteral("last_snapshot_digest"), state.lastSnapshotDigest);
    obj.insert(QStringLiteral("last_decision"), heartbeatDecisionToString(state.lastDecision));
    obj.insert(QStringLiteral("last_summary_digest"), state.lastSummaryDigest);
    obj.insert(QStringLiteral("last_deferred_reason"), state.lastDeferredReason);
    obj.insert(QStringLiteral("provider_state"), state.providerState);
    obj.insert(QStringLiteral("pulse_state"), state.pulseState);
    obj.insert(QStringLiteral("interrupted_run"), state.interruptedRun);
    obj.insert(QStringLiteral("has_pending_ticket"), state.hasPendingTicket);
    if (state.hasPendingTicket)
        obj.insert(QStringLiteral("pending_ticket"), heartbeatTicketToJson(state.pendingTicket));

    auto insertUtc = [&obj](const QString& key, const QDateTime& dt) {
        if (dt.isValid())
            obj.insert(key, dt.toUTC().toString(Qt::ISODateWithMs));
    };
    insertUtc(QStringLiteral("last_scheduled_at_utc"), state.lastScheduledAtUtc);
    insertUtc(QStringLiteral("last_started_at_utc"), state.lastStartedAtUtc);
    insertUtc(QStringLiteral("last_completed_at_utc"), state.lastCompletedAtUtc);
    insertUtc(QStringLiteral("next_due_at_utc"), state.nextDueAtUtc);
    insertUtc(QStringLiteral("last_escalation_at_utc"), state.lastEscalationAtUtc);
    insertUtc(QStringLiteral("last_maintenance_at_utc"), state.lastMaintenanceAtUtc);
    insertUtc(QStringLiteral("last_delivered_at_utc"), state.lastDeliveredAtUtc);
    return obj;
}

HeartbeatRuntimeState heartbeatRuntimeStateFromJson(const QJsonObject& obj)
{
    HeartbeatRuntimeState state;
    state.laneState = heartbeatLaneStateFromString(obj.value(QStringLiteral("lane_state")).toString());
    state.lastSnapshot = heartbeatSnapshotFromJson(obj.value(QStringLiteral("last_snapshot")).toObject());
    state.lastSnapshotDigest = obj.value(QStringLiteral("last_snapshot_digest")).toString().trimmed();
    state.lastDecision =
        heartbeatDecisionFromString(obj.value(QStringLiteral("last_decision")).toString());
    state.lastSummaryDigest = obj.value(QStringLiteral("last_summary_digest")).toString().trimmed();
    state.lastDeferredReason =
        obj.value(QStringLiteral("last_deferred_reason")).toString().trimmed();
    state.providerState = obj.value(QStringLiteral("provider_state")).toString().trimmed();
    state.pulseState = obj.value(QStringLiteral("pulse_state")).toString().trimmed();
    state.interruptedRun = obj.value(QStringLiteral("interrupted_run")).toBool(false);
    state.hasPendingTicket = obj.value(QStringLiteral("has_pending_ticket")).toBool(false);
    if (state.hasPendingTicket)
        state.pendingTicket = heartbeatTicketFromJson(obj.value(QStringLiteral("pending_ticket")).toObject());

    auto parseUtc = [&obj](const QString& key) {
        QDateTime dt = QDateTime::fromString(obj.value(key).toString().trimmed(), Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(obj.value(key).toString().trimmed(), Qt::ISODate);
        return dt.isValid() ? dt.toUTC() : QDateTime();
    };
    state.lastScheduledAtUtc = parseUtc(QStringLiteral("last_scheduled_at_utc"));
    state.lastStartedAtUtc = parseUtc(QStringLiteral("last_started_at_utc"));
    state.lastCompletedAtUtc = parseUtc(QStringLiteral("last_completed_at_utc"));
    state.nextDueAtUtc = parseUtc(QStringLiteral("next_due_at_utc"));
    state.lastEscalationAtUtc = parseUtc(QStringLiteral("last_escalation_at_utc"));
    state.lastMaintenanceAtUtc = parseUtc(QStringLiteral("last_maintenance_at_utc"));
    state.lastDeliveredAtUtc = parseUtc(QStringLiteral("last_delivered_at_utc"));
    state.loaded = true;
    return state;
}
