#include "ConversationContextService.h"

#include <QJsonArray>
#include <QUuid>

namespace {

QString completionGoal(const QJsonObject& existingTaskState, const TurnTask& finishedTurn)
{
    const QString stateSummary =
        existingTaskState.value(QStringLiteral("summary")).toString().trimmed();
    return stateSummary.isEmpty() ? finishedTurn.userContent : stateSummary;
}

} // namespace

ConversationContextService::ConversationContextService(const Dependencies& dependencies)
    : ConversationContextService(dependencies, Options())
{
}

ConversationContextService::ConversationContextService(const Dependencies& dependencies,
                                                       const Options& options)
    : m_dependencies(dependencies)
    , m_options(options)
{
}

void ConversationContextService::persistCompletionArtifacts(const QString& sessionId,
                                                            const TurnTask& finishedTurn,
                                                            const QJsonObject& existingTaskState,
                                                            const QDateTime& nowUtc) const
{
    if (!m_dependencies.saveTaskContextSnapshot
        || !m_dependencies.saveContextCompressionCheckpoint
        || !m_dependencies.saveResumePacket
        || !m_dependencies.loadTaskContextSnapshot
        || !m_dependencies.emitPipelineEvent) {
        return;
    }

    bool snapshotLoadOk = false;
    Q_UNUSED(snapshotLoadOk);
    QJsonObject snapshotObj =
        m_dependencies.loadTaskContextSnapshot(sessionId, &snapshotLoadOk).toJson();
    snapshotObj.insert(QStringLiteral("schema_version"), ConversationContext::kSchemaVersion);
    snapshotObj.insert(QStringLiteral("kind"), QStringLiteral("task_snapshot"));
    snapshotObj.insert(QStringLiteral("session_id"), sessionId);
    snapshotObj.insert(
        QStringLiteral("snapshot_id"),
        QStringLiteral("snap_") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    snapshotObj.insert(QStringLiteral("updated_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
    snapshotObj.insert(QStringLiteral("current_phase"), QStringLiteral("completed"));
    snapshotObj.insert(QStringLiteral("goal"), completionGoal(existingTaskState, finishedTurn));
    snapshotObj.insert(QStringLiteral("next_action"),
                       QStringLiteral("等待下一轮用户输入或继续当前任务"));
    snapshotObj.insert(QStringLiteral("recent_decisions"),
                       QJsonArray { finishedTurn.assistantContent.left(m_options.previewChars) });
    snapshotObj.insert(QStringLiteral("source_turn_ids"),
                       QJsonArray { finishedTurn.turnId });

    ConversationContext::TaskContextSnapshot snapshot;
    snapshot.payload = snapshotObj;
    const bool snapshotSaved = m_dependencies.saveTaskContextSnapshot(sessionId, snapshot);
    if (snapshotSaved) {
        QJsonObject snapshotExtra;
        snapshotExtra.insert(QStringLiteral("snapshot_id"),
                             snapshotObj.value(QStringLiteral("snapshot_id")).toString());
        snapshotExtra.insert(QStringLiteral("phase"),
                             snapshotObj.value(QStringLiteral("current_phase")).toString());
        m_dependencies.emitPipelineEvent(sessionId,
                                         QStringLiteral("context.snapshot.updated"),
                                         &finishedTurn,
                                         QString(),
                                         QString(),
                                         snapshotExtra,
                                         true);
    } else {
        QJsonObject errorExtra;
        errorExtra.insert(QStringLiteral("reason"), QStringLiteral("snapshot_save_failed"));
        m_dependencies.emitPipelineEvent(sessionId,
                                         QStringLiteral("context.compact.error"),
                                         &finishedTurn,
                                         QString(),
                                         QStringLiteral("snapshot_save_failed"),
                                         errorExtra,
                                         true);
    }

    const int contentLength =
        finishedTurn.assistantContent.size() + finishedTurn.userContent.size();
    if (contentLength > m_options.compactThresholdChars) {
        QJsonObject checkpointObj;
        checkpointObj.insert(QStringLiteral("schema_version"),
                             ConversationContext::kSchemaVersion);
        checkpointObj.insert(QStringLiteral("kind"), QStringLiteral("context_checkpoint"));
        checkpointObj.insert(QStringLiteral("session_id"), sessionId);
        checkpointObj.insert(
            QStringLiteral("checkpoint_id"),
            QStringLiteral("ckpt_") + QUuid::createUuid().toString(QUuid::WithoutBraces));
        checkpointObj.insert(QStringLiteral("updated_at_utc"),
                             nowUtc.toString(Qt::ISODateWithMs));
        checkpointObj.insert(QStringLiteral("reason"),
                             QStringLiteral("content_length_exceeded"));
        checkpointObj.insert(QStringLiteral("dropped_message_count"), 1);
        checkpointObj.insert(QStringLiteral("summary_text"),
                             finishedTurn.assistantContent.left(m_options.previewChars));

        ConversationContext::ContextCompressionCheckpoint checkpoint;
        checkpoint.payload = checkpointObj;
        if (m_dependencies.saveContextCompressionCheckpoint(sessionId, checkpoint)) {
            QJsonObject compactExtra;
            compactExtra.insert(
                QStringLiteral("checkpoint_id"),
                checkpointObj.value(QStringLiteral("checkpoint_id")).toString());
            compactExtra.insert(QStringLiteral("reason"),
                                checkpointObj.value(QStringLiteral("reason")).toString());
            compactExtra.insert(
                QStringLiteral("dropped_message_count"),
                checkpointObj.value(QStringLiteral("dropped_message_count")).toInt());
            m_dependencies.emitPipelineEvent(sessionId,
                                             QStringLiteral("context.compacted"),
                                             &finishedTurn,
                                             QString(),
                                             QString(),
                                             compactExtra,
                                             true);
        }
    } else {
        QJsonObject skippedExtra;
        skippedExtra.insert(QStringLiteral("reason"),
                            QStringLiteral("content_length_within_budget"));
        skippedExtra.insert(QStringLiteral("content_length"), contentLength);
        m_dependencies.emitPipelineEvent(sessionId,
                                         QStringLiteral("context.compact.skipped"),
                                         &finishedTurn,
                                         QString(),
                                         QString(),
                                         skippedExtra,
                                         true);
    }

    QJsonObject resumeObj;
    resumeObj.insert(QStringLiteral("schema_version"), ConversationContext::kSchemaVersion);
    resumeObj.insert(QStringLiteral("kind"), QStringLiteral("resume_packet"));
    resumeObj.insert(QStringLiteral("session_id"), sessionId);
    resumeObj.insert(QStringLiteral("updated_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
    resumeObj.insert(QStringLiteral("goal"),
                     snapshotObj.value(QStringLiteral("goal")).toString());
    resumeObj.insert(QStringLiteral("current_phase"),
                     snapshotObj.value(QStringLiteral("current_phase")).toString());
    resumeObj.insert(QStringLiteral("pending_items"),
                     QJsonArray { QStringLiteral("等待后续恢复或继续") });
    resumeObj.insert(QStringLiteral("resume_instruction"),
                     QStringLiteral("基于当前 snapshot 与最近消息继续执行。"));

    ConversationContext::ResumePacket packet;
    packet.payload = resumeObj;
    if (m_dependencies.saveResumePacket(sessionId, packet)) {
        QJsonObject resumeExtra;
        resumeExtra.insert(QStringLiteral("phase"),
                           resumeObj.value(QStringLiteral("current_phase")).toString());
        m_dependencies.emitPipelineEvent(sessionId,
                                         QStringLiteral("context.resume_packet.updated"),
                                         &finishedTurn,
                                         QString(),
                                         QString(),
                                         resumeExtra,
                                         true);
    }
}
