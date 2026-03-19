#include "BackgroundTaskCoordinator.h"

#include "core/model/Identity.h"
#include <QUuid>

BackgroundTaskCoordinator::BackgroundTaskCoordinator(const Dependencies& dependencies)
    : m_deps(dependencies)
{
}

void BackgroundTaskCoordinator::onScheduledJobTriggered(const QString& jobId, const QString& jobName)
{
    if (!m_deps.jobById
        || !m_deps.findIdentity
        || !m_deps.resolvePrimarySessionForAgent
        || !m_deps.userIdentityId
        || !m_deps.buildClientMessageId
        || !m_deps.buildPrompt
        || !m_deps.enqueueUserMessageAs
        || !m_deps.emitPipelineEventSimple) {
        return;
    }

    ScheduledJob job;
    if (!m_deps.jobById(jobId, &job)) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), jobId);
        m_deps.emitPipelineEventSimple(
            QString(),
            QStringLiteral("scheduler.failed"),
            QStringLiteral("job_not_found"),
            QString(),
            extra,
            true);
        return;
    }

    const QString agentId = job.agentId.trimmed();
    Identity* agent = m_deps.findIdentity(agentId);
    if (!agent || !agent->isAgent()) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), job.jobId);
        extra.insert(QStringLiteral("agent_id"), agentId);
        m_deps.emitPipelineEventSimple(
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
        m_deps.resolvePrimarySessionForAgent(agentId, true, isolated, QStringLiteral("scheduler"));
    if (sessionId.isEmpty()) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), job.jobId);
        extra.insert(QStringLiteral("agent_id"), agentId);
        m_deps.emitPipelineEventSimple(
            QString(),
            QStringLiteral("scheduler.failed"),
            QStringLiteral("session_unavailable"),
            QString(),
            extra,
            true);
        return;
    }

    const QString prompt = m_deps.buildPrompt(jobName, job.name, job.prompt, QString());
    const QString clientMessageId = m_deps.buildClientMessageId(
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
    m_deps.emitPipelineEventSimple(
        sessionId,
        QStringLiteral("scheduler.fired"),
        QString(),
        QString(),
        fireExtra,
        true);

    const QString turnId = m_deps.enqueueUserMessageAs(
        m_deps.userIdentityId(),
        sessionId,
        prompt,
        clientMessageId);
    if (turnId.isEmpty()) {
        m_deps.emitPipelineEventSimple(
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
    m_deps.emitPipelineEventSimple(
        sessionId,
        QStringLiteral("scheduler.completed"),
        QString(),
        QString(),
        completeExtra,
        true);
}

void BackgroundTaskCoordinator::onDelegateJobSettled(const QString& jobId,
                                                      const QString& ownerAgentId,
                                                      bool success,
                                                      const QString& result)
{
    if (!m_deps.resolvePrimarySessionForAgent
        || !m_deps.ctx.postMessage
        || !m_deps.updateTaskStateForSession
        || !m_deps.ctx.taskStateTextPreview
        || !m_deps.triggerHeartbeat
        || !m_deps.emitPipelineEventSimple) {
        return;
    }

    if (ownerAgentId.trimmed().isEmpty())
        return;

    QString notification = QStringLiteral(
                               "[子代理任务完成通知]\n"
                               "job_id: %1\n"
                               "状态: %2\n")
                               .arg(jobId, success ? QStringLiteral("成功") : QStringLiteral("失败"));
    if (!result.trimmed().isEmpty())
        notification += QStringLiteral("结果摘要: %1\n").arg(result.left(500));

    const QString sessionId = m_deps.resolvePrimarySessionForAgent(ownerAgentId, false, false, QString());
    if (!sessionId.isEmpty()) {
        const Message notifyMsg = Message::createSystem(sessionId, notification);
        m_deps.ctx.postMessage(sessionId, notifyMsg);

        QJsonObject extra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("delegate_job_settled"));
        extra.insert(QStringLiteral("source_event"), QStringLiteral("delegate.job_settled"));
        extra.insert(QStringLiteral("summary"),
                     m_deps.ctx.taskStateTextPreview(result.isEmpty() ? notification : result, 220));
        extra.insert(QStringLiteral("current_step"),
                     success ? QStringLiteral("后台子代理任务已完成")
                             : QStringLiteral("后台子代理任务失败"));
        extra.insert(QStringLiteral("next_step"), QJsonValue::Null);
        extra.insert(QStringLiteral("waiting_job_id"), QJsonValue::Null);
        extra.insert(QStringLiteral("last_error"),
                     success
                         ? QJsonValue::Null
                         : QJsonValue(m_deps.ctx.taskStateTextPreview(
                               result.isEmpty() ? notification : result,
                               160)));
        m_deps.updateTaskStateForSession(
            sessionId,
            success ? QStringLiteral("done") : QStringLiteral("failed"),
            nullptr,
            extra);
    }

    m_deps.triggerHeartbeat(ownerAgentId, QStringLiteral("delegate_job_settled"));

    QJsonObject eventExtra;
    eventExtra.insert(QStringLiteral("job_id"), jobId);
    eventExtra.insert(QStringLiteral("owner_agent_id"), ownerAgentId);
    eventExtra.insert(QStringLiteral("success"), success);
    m_deps.emitPipelineEventSimple(
        sessionId,
        QStringLiteral("delegate.job_settled"),
        QString(),
        QString(),
        eventExtra,
        true);
}