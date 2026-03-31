#include "HeartbeatSnapshotService.h"

#include <QJsonArray>

HeartbeatSnapshot HeartbeatSnapshotService::collect(const QString& agentId) const
{
    HeartbeatSnapshot snapshot;
    const QString trimmedAgentId = agentId.trimmed();
    snapshot.capturedAtUtc = QDateTime::currentDateTimeUtc();
    if (trimmedAgentId.isEmpty())
        return snapshot;

    if (m_dependencies.providerStateForAgent)
        snapshot.providerState = m_dependencies.providerStateForAgent(trimmedAgentId).trimmed();
    if (m_dependencies.providerIdForAgent)
        snapshot.providerId = m_dependencies.providerIdForAgent(trimmedAgentId).trimmed();

    if (m_dependencies.activeJobsForAgent) {
        const QList<DelegateTaskScheduler::JobInfo> activeJobs =
            m_dependencies.activeJobsForAgent(trimmedAgentId);
        snapshot.activeDelegateJobCount = activeJobs.size();
        QJsonArray jobs;
        for (int index = 0; index < activeJobs.size() && index < 8; ++index) {
            const DelegateTaskScheduler::JobInfo& job = activeJobs.at(index);
            QJsonObject item;
            item.insert(QStringLiteral("job_id"), job.jobId);
            item.insert(QStringLiteral("status"), job.status);
            item.insert(QStringLiteral("summary"), job.summary.left(120));
            jobs.append(item);
        }
        snapshot.delegateJobsSummary.insert(QStringLiteral("jobs"), jobs);
    }

    if (m_dependencies.pulseStateForAgent)
        snapshot.pulseState = m_dependencies.pulseStateForAgent(trimmedAgentId).trimmed();
    if (m_dependencies.schedulerEnabledJobsForAgent)
        snapshot.schedulerEnabledJobs = m_dependencies.schedulerEnabledJobsForAgent(trimmedAgentId);
    if (m_dependencies.schedulerIssueForAgent)
        snapshot.schedulerIssue = m_dependencies.schedulerIssueForAgent(trimmedAgentId);
    if (m_dependencies.memoryRetainedTurnsForAgent)
        snapshot.memoryRetainedTurns = m_dependencies.memoryRetainedTurnsForAgent(trimmedAgentId);
    if (m_dependencies.memoryDocSizeBytesForAgent)
        snapshot.memoryDocSizeBytes = m_dependencies.memoryDocSizeBytesForAgent(trimmedAgentId);
    if (m_dependencies.memoryIssueForAgent)
        snapshot.memoryIssue = m_dependencies.memoryIssueForAgent(trimmedAgentId);
    return snapshot;
}
