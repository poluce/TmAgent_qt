#include "MemoryService.h"

#include "ApplicationServices.h"
#include "ConversationService.h"
#include "PrimarySessionResolver.h"
#include "WorkspaceService.h"
#include "ChatCoordinatorSupport.h"
#include "HeartbeatService.h"
#include "core/manager/IdentityManager.h"
#include "core/model/Identity.h"
#include "core/model/Message.h"
#include "core/memory/MemoryManager.h"
#include <QUuid>

namespace {
using ChatCoordinatorSupport::taskStateTextPreview;

QString buildSchedulerClientMessageId(const QString& jobId)
{
    return QStringLiteral("scheduler-%1-%2")
        .arg(jobId, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString buildScheduledJobPrompt(const QString& jobNameArg,
                                const QString& storedJobName,
                                const QString& prompt)
{
    const QString displayName = jobNameArg.trimmed().isEmpty()
        ? (storedJobName.trimmed().isEmpty() ? QStringLiteral("scheduled-job")
                                             : storedJobName.trimmed())
        : jobNameArg.trimmed();
    return QStringLiteral("【定时任务:%1】\n%2").arg(displayName, prompt);
}
} // namespace

void MemoryService::refreshMemoryIndexAndEmit(const QString& sessionId,
                                              const QString& agentId,
                                              const TurnTask* turn,
                                              const QString& reason,
                                              const QString& sourcePath,
                                              const QJsonObject& sourceMetadata) const
{
    if (!m_memoryManager || !m_app.m_conversationService)
        return;

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return;

    QJsonObject indexMetadata;
    QString indexError;
    const bool ok = m_memoryManager->rebuildSearchIndex(trimmedAgentId, &indexMetadata, &indexError);

    QJsonObject extra;
    extra.insert(QStringLiteral("agent_id"), trimmedAgentId);
    extra.insert(QStringLiteral("reason"),
                 reason.trimmed().isEmpty() ? QStringLiteral("unknown") : reason.trimmed());
    if (!sourcePath.trimmed().isEmpty())
        extra.insert(QStringLiteral("source_path"), sourcePath);
    const QString longMemoryPath =
        sourceMetadata.value(QStringLiteral("longMemoryPath")).toString().trimmed();
    if (!longMemoryPath.isEmpty())
        extra.insert(QStringLiteral("longMemoryPath"), longMemoryPath);

    if (ok) {
        for (auto it = indexMetadata.constBegin(); it != indexMetadata.constEnd(); ++it)
            extra.insert(it.key(), it.value());
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("memory.index.updated"),
                                                       sessionId,
                                                       turn,
                                                       QString(),
                                                       QString(),
                                                       extra,
                                                       true);
    } else {
        m_app.m_conversationService->emitPipelineEvent(
            QStringLiteral("memory.index.error"),
            sessionId,
            turn,
            QString(),
            indexError.trimmed().isEmpty() ? QStringLiteral("memory index rebuild failed")
                                           : indexError.trimmed(),
            extra,
            true);
    }
}

void MemoryService::maybeReflectMemoryAndEmit(const QString& sessionId,
                                              const QString& agentId,
                                              const TurnTask& turn,
                                              bool forceReflection,
                                              const QString& triggerReason)
{
    if (!m_memoryManager || !m_app.m_conversationService)
        return;

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return;
    if (!m_memoryManager->reflectionEnabled())
        return;

    const int interval = m_memoryManager->reflectionIntervalTurns();
    if (interval <= 0 && !forceReflection)
        return;

    int retainedTurns = m_memoryRetainedTurnsByAgent.value(trimmedAgentId, 0);
    if (!forceReflection) {
        retainedTurns += 1;
        memoryRetainedTurnsByAgent().insert(trimmedAgentId, retainedTurns);
        if ((retainedTurns % interval) != 0)
            return;
    }

    QString summary;
    QString writtenPath;
    QJsonObject reflectMetadata;
    QString reflectError;
    const bool reflected = m_memoryManager->reflectAndScore(trimmedAgentId,
                                                            sessionId,
                                                            turn.turnId,
                                                            turn.requestTraceId,
                                                            &summary,
                                                            &writtenPath,
                                                            &reflectMetadata,
                                                            &reflectError);
    const QString reflectionTrigger = forceReflection
        ? (triggerReason.trimmed().isEmpty() ? QStringLiteral("forced")
                                            : triggerReason.trimmed())
        : QStringLiteral("retain_interval");

    if (!reflected) {
        QJsonObject extra;
        extra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        extra.insert(QStringLiteral("path"), writtenPath);
        extra.insert(QStringLiteral("reflection"), true);
        extra.insert(QStringLiteral("reflection_trigger"), reflectionTrigger);
        extra.insert(QStringLiteral("reflection_interval_turns"), interval);
        extra.insert(QStringLiteral("retained_turn_count"), retainedTurns);
        m_app.m_conversationService->emitPipelineEvent(
            QStringLiteral("memory.error"),
            sessionId,
            &turn,
            QString(),
            reflectError.isEmpty() ? QStringLiteral("memory reflection failed") : reflectError,
            extra,
            true);
        return;
    }

    QJsonObject extra = reflectMetadata;
    extra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
    extra.insert(QStringLiteral("summary"), summary);
    extra.insert(QStringLiteral("path"), writtenPath);
    extra.insert(QStringLiteral("reflection"), true);
    extra.insert(QStringLiteral("reflection_trigger"), reflectionTrigger);
    extra.insert(QStringLiteral("reflection_interval_turns"), interval);
    extra.insert(QStringLiteral("retained_turn_count"), retainedTurns);
    m_app.m_conversationService->emitPipelineEvent(QStringLiteral("memory.reflected"),
                                                   sessionId,
                                                   &turn,
                                                   QString(),
                                                   QString(),
                                                   extra,
                                                   true);

    QJsonObject qualityExtra = extra;
    qualityExtra.insert(QStringLiteral("quality_score"),
                        reflectMetadata.value(QStringLiteral("quality_score")).toInt());
    qualityExtra.insert(QStringLiteral("quality_level"),
                        reflectMetadata.value(QStringLiteral("quality_level")).toString());
    m_app.m_conversationService->emitPipelineEvent(QStringLiteral("memory.quality"),
                                                   sessionId,
                                                   &turn,
                                                   QString(),
                                                   QString(),
                                                   qualityExtra,
                                                   true);

    if (reflectMetadata.value(QStringLiteral("longMemoryAdded")).toInt() > 0) {
        refreshMemoryIndexAndEmit(sessionId,
                                  trimmedAgentId,
                                  &turn,
                                  QStringLiteral("reflect_turn"),
                                  writtenPath,
                                  reflectMetadata);
    }
}

QString MemoryService::resolvePrimarySessionForAgent(const QString& agentId,
                                                     bool createIfMissing,
                                                     bool isolated,
                                                     const QString& titleSuffix) const
{
    const PrimarySessionResolver resolver(
        PrimarySessionResolver::Dependencies {
            m_app.m_identityManager,
            m_app.m_sessionManager,
            [this]() {
                return m_app.m_identityManager && m_app.m_identityManager->userIdentity()
                    ? m_app.m_identityManager->userIdentity()->id()
                    : QString();
            },
            [this](const QString& actorIdentityId,
                   const QString& identityId,
                   const QString& title) -> Session* {
                return m_app.m_workspaceService
                    ? m_app.m_workspaceService->createSessionForIdentityAs(actorIdentityId,
                                                                          identityId,
                                                                          title)
                    : nullptr;
            }
        });
    return resolver.resolveForAgent(agentId, createIfMissing, isolated, titleSuffix);
}

void MemoryService::onScheduledJobTriggered(const QString& jobId, const QString& jobName)
{
    if (!m_schedulerService || !m_app.m_identityManager || !m_app.m_conversationService)
        return;

    ScheduledJob job;
    if (!m_schedulerService->jobById(jobId, &job)) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), jobId);
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("scheduler.failed"),
                                                       QString(),
                                                       nullptr,
                                                       QString(),
                                                       QStringLiteral("job_not_found"),
                                                       extra,
                                                       true);
        return;
    }

    const QString agentId = job.agentId.trimmed();
    Identity* agent = m_app.m_identityManager->findById(agentId);
    if (!agent || !agent->isAgent()) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), job.jobId);
        extra.insert(QStringLiteral("agent_id"), agentId);
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("scheduler.failed"),
                                                       QString(),
                                                       nullptr,
                                                       QString(),
                                                       QStringLiteral("agent_not_found"),
                                                       extra,
                                                       true);
        return;
    }

    const bool isolated =
        job.sessionTarget.trimmed().compare(QStringLiteral("isolated"), Qt::CaseInsensitive) == 0;
    const QString sessionId =
        resolvePrimarySessionForAgent(agentId, true, isolated, QStringLiteral("scheduler"));
    if (sessionId.isEmpty()) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), job.jobId);
        extra.insert(QStringLiteral("agent_id"), agentId);
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("scheduler.failed"),
                                                       QString(),
                                                       nullptr,
                                                       QString(),
                                                       QStringLiteral("session_unavailable"),
                                                       extra,
                                                       true);
        return;
    }

    const QString prompt = buildScheduledJobPrompt(jobName, job.name, job.prompt);
    const QString clientMessageId = buildSchedulerClientMessageId(job.jobId);

    QJsonObject fireExtra;
    fireExtra.insert(QStringLiteral("job_id"), job.jobId);
    fireExtra.insert(QStringLiteral("job_name"), job.name);
    fireExtra.insert(QStringLiteral("agent_id"), agentId);
    fireExtra.insert(QStringLiteral("session_id"), sessionId);
    fireExtra.insert(QStringLiteral("session_target"), job.sessionTarget);
    fireExtra.insert(QStringLiteral("cron"), job.cronExpr);
    m_app.m_conversationService->emitPipelineEvent(QStringLiteral("scheduler.fired"),
                                                   sessionId,
                                                   nullptr,
                                                   QString(),
                                                   QString(),
                                                   fireExtra,
                                                   true);

    const QString actorId =
        m_app.m_identityManager && m_app.m_identityManager->userIdentity()
        ? m_app.m_identityManager->userIdentity()->id()
        : QString();
    const QString turnId =
        m_app.m_conversationService->enqueueUserMessageAs(actorId, sessionId, prompt, clientMessageId);
    if (turnId.isEmpty()) {
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("scheduler.failed"),
                                                       sessionId,
                                                       nullptr,
                                                       QString(),
                                                       QStringLiteral("enqueue_failed"),
                                                       fireExtra,
                                                       true);
        return;
    }

    QJsonObject completeExtra = fireExtra;
    completeExtra.insert(QStringLiteral("turn_id"), turnId);
    m_app.m_conversationService->emitPipelineEvent(QStringLiteral("scheduler.completed"),
                                                   sessionId,
                                                   nullptr,
                                                   QString(),
                                                   QString(),
                                                   completeExtra,
                                                   true);
}

void MemoryService::onDelegateJobSettled(const QString& jobId,
                                         const QString& ownerAgentId,
                                         bool success,
                                         const QString& result)
{
    if (!m_app.m_conversationService)
        return;
    if (ownerAgentId.trimmed().isEmpty())
        return;

    QString notification = QStringLiteral(
                               "[子代理任务完成通知]\n"
                               "job_id: %1\n"
                               "状态: %2\n")
                               .arg(jobId, success ? QStringLiteral("成功") : QStringLiteral("失败"));
    if (!result.trimmed().isEmpty())
        notification += QStringLiteral("结果摘要: %1\n").arg(result.left(500));

    const QString sessionId = resolvePrimarySessionForAgent(ownerAgentId, false, false, QString());
    if (!sessionId.isEmpty() && m_app.m_sessionManager) {
        const Message notifyMsg = Message::createSystem(sessionId, notification);
        m_app.m_sessionManager->postMessage(sessionId, notifyMsg);

        QJsonObject extra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("delegate_job_settled"));
        extra.insert(QStringLiteral("source_event"), QStringLiteral("delegate.job_settled"));
        extra.insert(QStringLiteral("summary"),
                     taskStateTextPreview(result.isEmpty() ? notification : result, 220));
        extra.insert(QStringLiteral("current_step"),
                     success ? QStringLiteral("后台子代理任务已完成")
                             : QStringLiteral("后台子代理任务失败"));
        extra.insert(QStringLiteral("next_step"), QJsonValue::Null);
        extra.insert(QStringLiteral("waiting_job_id"), QJsonValue::Null);
        extra.insert(QStringLiteral("last_error"),
                     success ? QJsonValue::Null
                             : QJsonValue(taskStateTextPreview(
                                   result.isEmpty() ? notification : result,
                                   160)));
        m_app.m_conversationService->updateTaskStateForSession(
            sessionId,
            success ? QStringLiteral("done") : QStringLiteral("failed"),
            nullptr,
            extra);
    }

    if (m_heartbeatService) {
        m_heartbeatService->requestEventDrivenHeartbeat(
            ownerAgentId,
            QStringLiteral("delegate_job_settled"),
            success ? HeartbeatTicketPriority::High : HeartbeatTicketPriority::Critical,
            QJsonObject {
                { QStringLiteral("job_id"), jobId },
                { QStringLiteral("success"), success }
            });
    }

    QJsonObject eventExtra;
    eventExtra.insert(QStringLiteral("job_id"), jobId);
    eventExtra.insert(QStringLiteral("owner_agent_id"), ownerAgentId);
    eventExtra.insert(QStringLiteral("success"), success);
    m_app.m_conversationService->emitPipelineEvent(QStringLiteral("delegate.job_settled"),
                                                   sessionId,
                                                   nullptr,
                                                   QString(),
                                                   QString(),
                                                   eventExtra,
                                                   true);
}

ToolResult MemoryService::executeMemoryWriteTool(const QJsonObject& args)
{
    if (!m_memoryManager || !m_app.m_conversationService) {
        return ToolResult(QStringLiteral("错误: memory manager unavailable"),
                          QStringLiteral("记忆写入失败"),
                          false);
    }

    const QString agentId = args.value(QStringLiteral("_agent_id")).toString().trimmed();
    if (agentId.isEmpty()) {
        return ToolResult(QStringLiteral("错误: 缺少 _agent_id，上下文无法确定当前助手"),
                          QStringLiteral("记忆写入失败：缺少助手上下文"),
                          false);
    }

    QString sessionId = m_app.m_conversationService->activeSessionByAgent().value(agentId).trimmed();
    if (sessionId.isEmpty())
        sessionId = resolvePrimarySessionForAgent(agentId, false, false, QStringLiteral("memory_write"))
                        .trimmed();

    const QString memoryText = args.value(QStringLiteral("memory")).toString().trimmed();
    const QString reason = args.value(QStringLiteral("reason")).toString().trimmed();
    const QString toolCallId = args.value(QStringLiteral("_tool_call_id")).toString().trimmed();

    TurnTask* activeTurn =
        sessionId.isEmpty() ? nullptr : m_app.m_conversationService->turnManager().activeTurn(sessionId);
    TurnTask syntheticTurn;
    const TurnTask* eventTurn = activeTurn;
    if (!eventTurn) {
        syntheticTurn.turnId = QStringLiteral("memory_write");
        syntheticTurn.requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        syntheticTurn.runId = QStringLiteral("memory_write");
        syntheticTurn.actorIdentityId = agentId;
        eventTurn = &syntheticTurn;
    }

    QString memorySummary;
    QString memoryPath;
    QJsonObject memoryMetadata;
    QString memoryError;
    const bool ok = m_memoryManager->rememberToolRequested(agentId,
                                                           sessionId,
                                                           eventTurn ? eventTurn->turnId : QString(),
                                                           eventTurn ? eventTurn->requestTraceId : QString(),
                                                           memoryText,
                                                           reason,
                                                           &memorySummary,
                                                           &memoryPath,
                                                           &memoryMetadata,
                                                           &memoryError);

    if (!ok) {
        QJsonObject extra;
        extra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        extra.insert(QStringLiteral("path"), memoryPath);
        extra.insert(QStringLiteral("toolRequested"), true);
        if (!toolCallId.isEmpty())
            extra.insert(QStringLiteral("tool_call_id"), toolCallId);
        if (!reason.isEmpty())
            extra.insert(QStringLiteral("reason"), reason);
        m_app.m_conversationService->emitPipelineEvent(
            QStringLiteral("memory.error"),
            sessionId,
            eventTurn,
            QString(),
            memoryError.isEmpty() ? QStringLiteral("tool memory write failed") : memoryError,
            extra,
            true);
        return ToolResult(memoryError.isEmpty() ? QStringLiteral("错误: memory_write 执行失败")
                                                : memoryError,
                          QStringLiteral("记忆写入失败"),
                          false,
                          extra);
    }

    QJsonObject updateExtra;
    updateExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
    updateExtra.insert(QStringLiteral("summary"), memorySummary);
    updateExtra.insert(QStringLiteral("path"), memoryPath);
    updateExtra.insert(QStringLiteral("toolRequested"), true);
    if (!toolCallId.isEmpty())
        updateExtra.insert(QStringLiteral("tool_call_id"), toolCallId);
    if (!reason.isEmpty())
        updateExtra.insert(QStringLiteral("reason"), reason);
    for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
        updateExtra.insert(it.key(), it.value());
    m_app.m_conversationService->emitPipelineEvent(QStringLiteral("memory.updated"),
                                                   sessionId,
                                                   eventTurn,
                                                   QString(),
                                                   QString(),
                                                   updateExtra,
                                                   true);

    const int compactedCount = memoryMetadata.value(QStringLiteral("compacted_count")).toInt();
    if (compactedCount > 0) {
        QJsonObject compactExtra = updateExtra;
        compactExtra.insert(QStringLiteral("compacted_count"), compactedCount);
        m_app.m_conversationService->emitPipelineEvent(QStringLiteral("memory.compacted"),
                                                       sessionId,
                                                       eventTurn,
                                                       QString(),
                                                       QString(),
                                                       compactExtra,
                                                       true);
    }

    if (memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt() > 0) {
        refreshMemoryIndexAndEmit(sessionId,
                                  agentId,
                                  eventTurn,
                                  QStringLiteral("tool_memory_write"),
                                  memoryPath,
                                  memoryMetadata);
    }

    QJsonObject resultData = updateExtra;
    resultData.insert(QStringLiteral("agent_id"), agentId);
    resultData.insert(QStringLiteral("session_id"), sessionId);

    const bool duplicateOnly =
        memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt() == 0
        && memoryMetadata.value(QStringLiteral("longMemoryDuplicate")).toInt() > 0;
    const QString raw = duplicateOnly
        ? QStringLiteral(
              "memory_write: 已存在相同长期记忆，无需重复写入\nagent_id: %1\npath: %2\nmemory: %3")
              .arg(agentId, memoryPath, memorySummary)
        : QStringLiteral("memory_write: 已写入长期记忆\nagent_id: %1\npath: %2\nmemory: %3")
              .arg(agentId, memoryPath, memorySummary);
    const QString summary = duplicateOnly ? QStringLiteral("记忆已存在，无需重复写入")
                                          : QStringLiteral("已写入长期记忆");
    return ToolResult(raw, summary, true, resultData);
}
