#ifndef HEARTBEATTYPES_H
#define HEARTBEATTYPES_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QTime>

struct ActiveHours {
    QTime start;
    QTime end;
    QString timezone;
};

struct HeartbeatWatchModules {
    bool provider = true;
    bool delegateJobs = true;
    bool pulse = true;
    bool scheduler = true;
    bool memory = true;
};

struct HeartbeatActionableRules {
    bool delegateChanges = true;
    bool providerStatus = true;
    bool pulseRisk = true;
    bool memoryIssues = true;
    bool schedulerIssues = true;
};

struct HeartbeatDeliveryPolicy {
    bool deliverActionableSummary = true;
};

struct HeartbeatLlmEscalation {
    bool enabled = true;
};

struct HeartbeatMaintenancePolicy {
    bool reflectMemory = true;
    bool rebuildMemoryIndex = true;
};

struct HeartbeatPolicy {
    bool enabled = true;
    int cadenceMs = 30 * 60 * 1000;
    int startupGraceMs = 5000;
    int coalesceMs = 250;
    ActiveHours activeHours;
    HeartbeatWatchModules watchModules;
    HeartbeatActionableRules actionableRules;
    HeartbeatDeliveryPolicy deliveryPolicy;
    HeartbeatLlmEscalation llmEscalation;
    HeartbeatMaintenancePolicy maintenancePolicy;
    QString instructionPath;
};

enum class HeartbeatTicketKind {
    Auto,
    Manual,
    Recovery,
    EventDriven
};

enum class HeartbeatTicketPriority {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

struct HeartbeatTicket {
    HeartbeatTicketKind kind = HeartbeatTicketKind::Auto;
    HeartbeatTicketPriority priority = HeartbeatTicketPriority::Normal;
    QString reason;
    QString dedupeKey;
    QDateTime requestedAtUtc;
    QJsonObject payload;
};

enum class HeartbeatDecision {
    Noop,
    MaintainOnly,
    Escalate
};

enum class HeartbeatLaneState {
    Idle,
    Deferred,
    Running,
    Escalating
};

struct HeartbeatSnapshot {
    QDateTime capturedAtUtc;
    QString providerState;
    QString providerId;
    int activeDelegateJobCount = 0;
    QJsonObject delegateJobsSummary;
    QString pulseState;
    int schedulerEnabledJobs = 0;
    bool schedulerIssue = false;
    int memoryRetainedTurns = 0;
    qint64 memoryDocSizeBytes = -1;
    bool memoryIssue = false;
};

struct HeartbeatCycleResult {
    HeartbeatDecision decision = HeartbeatDecision::Noop;
    bool deliverToUser = false;
    bool usedLlm = false;
    QString summary;
    QStringList actionableSignals;
    QJsonObject metadata;
};

QString heartbeatTicketKindToString(HeartbeatTicketKind kind);
HeartbeatTicketKind heartbeatTicketKindFromString(const QString& value);

QString heartbeatTicketPriorityToString(HeartbeatTicketPriority priority);
HeartbeatTicketPriority heartbeatTicketPriorityFromString(const QString& value);

QString heartbeatDecisionToString(HeartbeatDecision decision);
HeartbeatDecision heartbeatDecisionFromString(const QString& value);

QString heartbeatLaneStateToString(HeartbeatLaneState state);
HeartbeatLaneState heartbeatLaneStateFromString(const QString& value);

QJsonObject heartbeatTicketToJson(const HeartbeatTicket& ticket);
HeartbeatTicket heartbeatTicketFromJson(const QJsonObject& obj);

QJsonObject heartbeatPolicyToJson(const HeartbeatPolicy& policy);
HeartbeatPolicy heartbeatPolicyFromJson(const QJsonObject& obj);

QJsonObject heartbeatSnapshotToJson(const HeartbeatSnapshot& snapshot);
HeartbeatSnapshot heartbeatSnapshotFromJson(const QJsonObject& obj);

#endif // HEARTBEATTYPES_H
