#include "HeartbeatDispatchCoordinator.h"

#include "AgentPulse.h"
#include <QUuid>

HeartbeatDispatchCoordinator::HeartbeatDispatchCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void HeartbeatDispatchCoordinator::dispatch(const QString& agentId,
                                            const QString& sessionId,
                                            const QString& reasonLabel,
                                            bool forceInteractive,
                                            bool hasChange,
                                            bool watchDelegate,
                                            bool watchProvider,
                                            bool watchPulse,
                                            bool providerDown,
                                            const QString& providerId,
                                            const QList<DelegateTaskScheduler::JobInfo>& activeJobs,
                                            const QJsonObject& triggeredExtra)
{
    if (!m_dependencies.queueDepthForSession
        || !m_dependencies.emitPipelineEventSimple
        || !m_dependencies.userIdentityId
        || !m_dependencies.buildHeartbeatPrompt
        || !m_dependencies.pulseForAgent
        || !m_dependencies.pulseStateText
        || !m_dependencies.enqueueUserMessageAs
        || !m_dependencies.buildHeartbeatClientMessageId
        || !m_dependencies.markHeartbeatNotified
        || !m_dependencies.persistStateIfNeeded) {
        return;
    }

    if (!forceInteractive) {
        const int pipelineDepth = m_dependencies.queueDepthForSession(sessionId);
        if (pipelineDepth > 0) {
            m_dependencies.persistStateIfNeeded(false);
            QJsonObject extra = triggeredExtra;
            extra.insert(QStringLiteral("session_id"), sessionId);
            extra.insert(QStringLiteral("queue_depth"), pipelineDepth);
            extra.insert(QStringLiteral("reason"), QStringLiteral("pipeline_busy"));
            m_dependencies.emitPipelineEventSimple(
                sessionId,
                QStringLiteral("heartbeat.skipped"),
                QStringLiteral("pipeline_busy"),
                QString(),
                extra,
                true);
            return;
        }
    }

    QString prompt = m_dependencies.buildHeartbeatPrompt(agentId, reasonLabel);
    if (hasChange) {
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
        if (watchProvider && !providerId.trimmed().isEmpty())
            delta << QStringLiteral("Provider 状态: %1").arg(providerDown ? QStringLiteral("down") : QStringLiteral("up"));
        if (watchPulse) {
            AgentPulse* pulse = m_dependencies.pulseForAgent(agentId);
            if (pulse)
                delta << QStringLiteral("主代理状态: %1").arg(m_dependencies.pulseStateText(pulse));
        }
        if (delta.isEmpty())
            delta << QStringLiteral("状态发生变化。");
        prompt += QStringLiteral("\n\n[Heartbeat Delta]\n") + delta.join(QStringLiteral("\n"));
    } else if (forceInteractive) {
        prompt += QStringLiteral("\n\n[Heartbeat Delta]\n当前无关键变化。");
    }

    const QString clientMessageId =
        m_dependencies.buildHeartbeatClientMessageId(
            forceInteractive ? QStringLiteral("heartbeat-manual") : QStringLiteral("heartbeat-bg"),
            QUuid::createUuid().toString(QUuid::WithoutBraces));

    const QString turnId = m_dependencies.enqueueUserMessageAs(
        m_dependencies.userIdentityId(),
        sessionId,
        prompt,
        clientMessageId);
    if (turnId.isEmpty()) {
        m_dependencies.persistStateIfNeeded(false);
        QJsonObject extra = triggeredExtra;
        extra.insert(QStringLiteral("session_id"), sessionId);
        extra.insert(QStringLiteral("reason"), QStringLiteral("enqueue_failed"));
        m_dependencies.emitPipelineEventSimple(
            sessionId,
            QStringLiteral("heartbeat.failed"),
            QStringLiteral("enqueue_failed"),
            QString(),
            extra,
            true);
        return;
    }

    m_dependencies.markHeartbeatNotified(agentId, turnId);
    m_dependencies.persistStateIfNeeded(true);

    QJsonObject completeExtra = triggeredExtra;
    completeExtra.insert(QStringLiteral("session_id"), sessionId);
    completeExtra.insert(QStringLiteral("turn_id"), turnId);
    m_dependencies.emitPipelineEventSimple(
        sessionId,
        QStringLiteral("heartbeat.completed"),
        QString(),
        QString(),
        completeExtra,
        true);
}

