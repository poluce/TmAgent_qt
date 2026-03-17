#include "SchedulerTriggerCoordinator.h"

#include "core/model/Identity.h"
#include <QUuid>

SchedulerTriggerCoordinator::SchedulerTriggerCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void SchedulerTriggerCoordinator::onScheduledJobTriggered(const QString& jobId, const QString& jobName)
{
    if (!m_dependencies.jobById
        || !m_dependencies.findIdentity
        || !m_dependencies.resolvePrimarySessionForAgent
        || !m_dependencies.userIdentityId
        || !m_dependencies.buildClientMessageId
        || !m_dependencies.buildPrompt
        || !m_dependencies.enqueueUserMessageAs
        || !m_dependencies.emitPipelineEventSimple) {
        return;
    }

    ScheduledJob job;
    if (!m_dependencies.jobById(jobId, &job)) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), jobId);
        m_dependencies.emitPipelineEventSimple(
            QString(),
            QStringLiteral("scheduler.failed"),
            QStringLiteral("job_not_found"),
            QString(),
            extra,
            true);
        return;
    }

    const QString agentId = job.agentId.trimmed();
    Identity* agent = m_dependencies.findIdentity(agentId);
    if (!agent || !agent->isAgent()) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), job.jobId);
        extra.insert(QStringLiteral("agent_id"), agentId);
        m_dependencies.emitPipelineEventSimple(
            QString(),
            QStringLiteral("scheduler.failed"),
            QStringLiteral("agent_not_found"),
            QString(),
            extra,
            true);
        return;
    }

    const bool isolated = job.sessionTarget.trimmed().compare(QStringLiteral("isolated"), Qt::CaseInsensitive) == 0;
    const QString sessionId =
        m_dependencies.resolvePrimarySessionForAgent(agentId, true, isolated, QStringLiteral("scheduler"));
    if (sessionId.isEmpty()) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), job.jobId);
        extra.insert(QStringLiteral("agent_id"), agentId);
        m_dependencies.emitPipelineEventSimple(
            QString(),
            QStringLiteral("scheduler.failed"),
            QStringLiteral("session_unavailable"),
            QString(),
            extra,
            true);
        return;
    }

    const QString prompt = m_dependencies.buildPrompt(jobName, job.name, job.prompt, QString());
    const QString clientMessageId = m_dependencies.buildClientMessageId(
        job.jobId,
        QUuid::createUuid().toString(QUuid::WithoutBraces),
        QString(),
        QString());

    QJsonObject fireExtra;
    fireExtra.insert(QStringLiteral("job_id"), job.jobId);
    fireExtra.insert(QStringLiteral("job_name"), job.name);
    fireExtra.insert(QStringLiteral("agent_id"), agentId);
    fireExtra.insert(QStringLiteral("session_id"), sessionId);
    fireExtra.insert(QStringLiteral("session_target"), job.sessionTarget);
    fireExtra.insert(QStringLiteral("cron"), job.cronExpr);
    m_dependencies.emitPipelineEventSimple(
        sessionId,
        QStringLiteral("scheduler.fired"),
        QString(),
        QString(),
        fireExtra,
        true);

    const QString turnId = m_dependencies.enqueueUserMessageAs(
        m_dependencies.userIdentityId(),
        sessionId,
        prompt,
        clientMessageId);
    if (turnId.isEmpty()) {
        m_dependencies.emitPipelineEventSimple(
            sessionId,
            QStringLiteral("scheduler.failed"),
            QStringLiteral("enqueue_failed"),
            QString(),
            fireExtra,
            true);
        return;
    }

    QJsonObject completeExtra = fireExtra;
    completeExtra.insert(QStringLiteral("turn_id"), turnId);
    m_dependencies.emitPipelineEventSimple(
        sessionId,
        QStringLiteral("scheduler.completed"),
        QString(),
        QString(),
        completeExtra,
        true);
}
