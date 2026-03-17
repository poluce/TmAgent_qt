#include "DelegateSettlementCoordinator.h"

DelegateSettlementCoordinator::DelegateSettlementCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void DelegateSettlementCoordinator::onDelegateJobSettled(const QString& jobId,
                                                         const QString& ownerAgentId,
                                                         bool success,
                                                         const QString& result)
{
    if (!m_dependencies.resolvePrimarySessionForAgent
        || !m_dependencies.postMessage
        || !m_dependencies.updateTaskStateForSession
        || !m_dependencies.taskStateTextPreview
        || !m_dependencies.triggerHeartbeat
        || !m_dependencies.emitPipelineEventSimple) {
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

    const QString sessionId = m_dependencies.resolvePrimarySessionForAgent(ownerAgentId, false, false, QString());
    if (!sessionId.isEmpty()) {
        const Message notifyMsg = Message::createSystem(sessionId, notification);
        m_dependencies.postMessage(sessionId, notifyMsg);

        QJsonObject extra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("delegate_job_settled"));
        extra.insert(QStringLiteral("source_event"), QStringLiteral("delegate.job_settled"));
        extra.insert(QStringLiteral("summary"),
                     m_dependencies.taskStateTextPreview(result.isEmpty() ? notification : result, 220));
        extra.insert(QStringLiteral("current_step"),
                     success ? QStringLiteral("后台子代理任务已完成")
                             : QStringLiteral("后台子代理任务失败"));
        extra.insert(QStringLiteral("next_step"), QJsonValue::Null);
        extra.insert(QStringLiteral("waiting_job_id"), QJsonValue::Null);
        extra.insert(QStringLiteral("last_error"),
                     success
                         ? QJsonValue::Null
                         : QJsonValue(m_dependencies.taskStateTextPreview(
                               result.isEmpty() ? notification : result,
                               160)));
        m_dependencies.updateTaskStateForSession(
            sessionId,
            success ? QStringLiteral("done") : QStringLiteral("failed"),
            nullptr,
            extra);
    }

    m_dependencies.triggerHeartbeat(ownerAgentId, QStringLiteral("delegate_job_settled"));

    QJsonObject eventExtra;
    eventExtra.insert(QStringLiteral("job_id"), jobId);
    eventExtra.insert(QStringLiteral("owner_agent_id"), ownerAgentId);
    eventExtra.insert(QStringLiteral("success"), success);
    m_dependencies.emitPipelineEventSimple(
        sessionId,
        QStringLiteral("delegate.job_settled"),
        QString(),
        QString(),
        eventExtra,
        true);
}
