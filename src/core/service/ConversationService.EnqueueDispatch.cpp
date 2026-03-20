#include "ConversationService.h"

#include "ApplicationServices.h"
#include "ConversationContextTypes.h"
#include "GovernanceService.h"
#include "MemoryService.h"
#include "WorkspaceService.h"
#include "AgentRuntime.h"
#include "ChatCoordinatorSupport.h"
#include "MessageRouter.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/memory/MemoryManager.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/Message.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QUuid>

namespace {
using ChatCoordinatorSupport::estimateHistoryChars;
using ChatCoordinatorSupport::taskStateTextPreview;

constexpr int kSoftQueueDepth = 10;
constexpr int kHardQueueDepth = 200;
constexpr int kQueueMergeWindowMs = 2500;
constexpr int kQueueMergeMaxMergedMessages = 4;
constexpr int kQueueMergeMaxChars = 12000;
constexpr int kMemoryContextMaxChars = 4500;
constexpr int kMaxTeammateInjections = 20;
} // namespace

QString ConversationService::enqueueUserMessage(const QString& sessionId,
                                                const QString& text,
                                                const QString& clientMessageId)
{
    const QString userId = m_app.m_identityManager ? m_app.m_identityManager->userIdentity()->id()
                                                   : QString();
    return enqueueUserMessageAs(userId, sessionId, text, clientMessageId);
}

QString ConversationService::enqueueUserMessageAs(const QString& actorIdentityId,
                                                  const QString& sessionId,
                                                  const QString& text,
                                                  const QString& clientMessageId)
{
    if (!m_app.m_identityManager || !m_app.m_sessionManager || !m_app.m_workspaceService)
        return QString();

    if (!m_app.m_workspaceService->canIdentitySendMessage(actorIdentityId, sessionId)) {
        qWarning() << "[ConversationService] 拒绝发送消息，actor 无权限:" << actorIdentityId
                   << "session:" << sessionId;
        return QString();
    }

    const QString prompt = text.trimmed();
    if (prompt.isEmpty())
        return QString();

    Session* session = m_app.m_sessionManager->findById(sessionId);
    if (!session)
        return QString();

    Identity* actor = m_app.m_identityManager->findById(actorIdentityId);
    if (!actor)
        return QString();
    const QString actorId = actor->id().trimmed();
    if (actorId.isEmpty())
        return QString();

    m_turnManager.ensurePipeline(sessionId);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    QStringList participantAgentIds;
    QHash<QString, QString> participantAgentNames;
    for (const QString& participantId : session->participantIds()) {
        Identity* participant = m_app.m_identityManager->findById(participantId);
        if (!participant || !participant->isAgent())
            continue;
        participantAgentIds.append(participant->id());
        participantAgentNames.insert(participant->id(), participant->name());
    }

    MessageRouter::RouteInput routeInput;
    routeInput.sessionType = session->type();
    routeInput.senderIdentityId = actorId;
    routeInput.userIdentityId = m_app.m_identityManager->userIdentity()
        ? m_app.m_identityManager->userIdentity()->id()
        : QString();
    routeInput.text = prompt;
    routeInput.participantAgentIds = participantAgentIds;
    routeInput.agentDisplayNames = participantAgentNames;
    const MessageRouter::RouteResult routeResult = MessageRouter::route(routeInput);

    TurnTask* mergeTarget = nullptr;
    if (TurnTask* tail = m_turnManager.queuedTail(sessionId)) {
        const QString tailActorId = tail->actorIdentityId.trimmed();
        const int tailMergedCount = qMax(1, tail->mergedMessageCount);
        const bool sameActor = !tailActorId.isEmpty() && tailActorId == actorId;
        const bool withinWindow = tail->enqueuedAtMs > 0
            && nowMs >= tail->enqueuedAtMs
            && (nowMs - tail->enqueuedAtMs) <= kQueueMergeWindowMs;
        const bool withinMergeCount = tailMergedCount < kQueueMergeMaxMergedMessages;
        const bool withinMergedSize =
            (tail->userContent.size() + prompt.size() + 32) <= kQueueMergeMaxChars;
        if (sameActor && withinWindow && withinMergeCount && withinMergedSize)
            mergeTarget = tail;
    }

    const int queueDepthBeforeEnqueue = m_turnManager.totalDepth(sessionId);
    if (!mergeTarget && queueDepthBeforeEnqueue >= kHardQueueDepth) {
        QJsonObject extra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("queue_overflow"));
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        extra.insert(QStringLiteral("queueHardLimit"), kHardQueueDepth);
        emitPipelineEvent(QStringLiteral("turn_rejected"),
                          sessionId,
                          nullptr,
                          QString(),
                          QStringLiteral("queue overflow"),
                          extra,
                          true);
        return QString();
    }

    if (!mergeTarget && queueDepthBeforeEnqueue >= kSoftQueueDepth) {
        QJsonObject extra;
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        extra.insert(QStringLiteral("queueSoftLimit"), kSoftQueueDepth);
        emitPipelineEvent(QStringLiteral("queue_backpressure"),
                          sessionId,
                          nullptr,
                          QString(),
                          QString(),
                          extra,
                          true);
    }

    QString requestTraceId = mergeTarget ? mergeTarget->requestTraceId : QString();
    if (requestTraceId.trimmed().isEmpty())
        requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString turnId = mergeTarget ? mergeTarget->turnId : QString();
    if (turnId.trimmed().isEmpty())
        turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (mergeTarget) {
        mergeTarget->requestTraceId = requestTraceId;
        mergeTarget->turnId = turnId;
    }

    TurnTask turn;
    turn.requestTraceId = requestTraceId;
    turn.turnId = turnId;
    turn.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    turn.actorIdentityId = actorId;
    turn.enqueuedAtMs = nowMs;
    turn.mergedMessageCount = 1;
    turn.clientMessageId = clientMessageId.trimmed();
    turn.userContent = prompt;

    const bool skipPersistUserMessage =
        ChatCoordinatorSupport::isBackgroundHeartbeatClientMessageId(turn.clientMessageId);
    if (!skipPersistUserMessage) {
        Message userMsg = Message::createText(sessionId, actorId, prompt);
        userMsg.traceId = requestTraceId;
        userMsg.turnId = turnId;
        userMsg.mentions = routeResult.targetAgentIds;
        userMsg.status = Message::Status::Completed;
        m_app.m_sessionManager->postMessage(sessionId, userMsg);
    }

    QJsonObject routeExtra;
    routeExtra.insert(QStringLiteral("target_agent_ids"),
                      QJsonArray::fromStringList(routeResult.targetAgentIds));
    routeExtra.insert(QStringLiteral("mention_tokens"),
                      QJsonArray::fromStringList(routeResult.mentionTokens));
    routeExtra.insert(QStringLiteral("unresolved_mentions"),
                      QJsonArray::fromStringList(routeResult.unresolvedMentions));
    routeExtra.insert(QStringLiteral("is_broadcast"), routeResult.isBroadcast);
    routeExtra.insert(QStringLiteral("used_default_route"), routeResult.usedDefaultRoute);
    emitPipelineEvent(QStringLiteral("message_routed"),
                      sessionId,
                      &turn,
                      QString(),
                      QString(),
                      routeExtra,
                      false);

    if (mergeTarget) {
        mergeTarget->enqueuedAtMs = nowMs;
        mergeTarget->mergedMessageCount = qMax(1, mergeTarget->mergedMessageCount) + 1;
        if (!turn.clientMessageId.isEmpty())
            mergeTarget->clientMessageId = turn.clientMessageId;
        mergeTarget->userContent.append(QStringLiteral("\n\n[补充消息]\n"));
        mergeTarget->userContent.append(prompt);

        QJsonObject extra;
        extra.insert(QStringLiteral("mergedIntoTurnId"), mergeTarget->turnId);
        extra.insert(QStringLiteral("mergedMessageCount"), mergeTarget->mergedMessageCount);
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        emitPipelineEvent(QStringLiteral("turn_merged"),
                          sessionId,
                          mergeTarget,
                          QString(),
                          QString(),
                          extra,
                          true);

        QJsonObject taskExtra;
        taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_merged"));
        taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_merged"));
        taskExtra.insert(QStringLiteral("summary"),
                         taskStateTextPreview(mergeTarget->userContent, 220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("等待调度"));
        taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("合并补充消息后开始执行"));
        updateTaskStateForSession(sessionId,
                                  QStringLiteral("queued"),
                                  mergeTarget,
                                  taskExtra);
        return mergeTarget->turnId;
    }

    m_turnManager.enqueueTurn(sessionId, turn);
    emitPipelineEvent(QStringLiteral("turn_queued"),
                      sessionId,
                      &turn,
                      QString(),
                      QString(),
                      QJsonObject(),
                      true);

    QJsonObject taskExtra;
    taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_queued"));
    taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_queued"));
    taskExtra.insert(QStringLiteral("summary"), taskStateTextPreview(turn.userContent, 220));
    taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("等待调度"));
    taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("准备执行用户请求"));
    updateTaskStateForSession(sessionId, QStringLiteral("queued"), &turn, taskExtra);

    tryStartNextTurn(sessionId);
    return turn.turnId;
}

void ConversationService::sendUserMessage(const QString& sessionId, const QString& text)
{
    enqueueUserMessage(sessionId, text);
}

void ConversationService::sendUserMessageAs(const QString& actorIdentityId,
                                            const QString& sessionId,
                                            const QString& text)
{
    enqueueUserMessageAs(actorIdentityId, sessionId, text);
}

void ConversationService::tryStartNextTurnForAgent(const QString& agentIdentityId)
{
    if (agentIdentityId.trimmed().isEmpty())
        return;
    if (!m_agentActiveSession.value(agentIdentityId).isEmpty())
        return;

    const QStringList sessionIds = m_turnManager.sessionIds();
    for (const QString& sid : sessionIds) {
        if (agentIdentityIdForSession(sid) != agentIdentityId)
            continue;
        tryStartNextTurn(sid);
        if (!m_agentActiveSession.value(agentIdentityId).isEmpty())
            break;
    }
}

void ConversationService::tryStartNextTurn(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline)
        return;
    if (m_turnManager.hasActiveTurn(sessionId) || m_turnManager.queuedTurnCount(sessionId) <= 0)
        return;

    QString agentId;
    Identity* runtimeIdentity = nullptr;
    if (AgentRuntime* runtime = ensureRuntimeForSession(sessionId)) {
        runtimeIdentity = runtime->identity();
        agentId = runtime->identityId().trimmed();
    }
    if (!runtimeIdentity || agentId.isEmpty())
        return;

    const QString activeSessionId = m_agentActiveSession.value(agentId);
    if (!activeSessionId.isEmpty() && activeSessionId != sessionId)
        return;

    TurnTask startedTurn;
    if (!m_turnManager.startNextTurn(sessionId, &startedTurn))
        return;
    if (startedTurn.requestTraceId.isEmpty()) {
        startedTurn.requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (TurnTask* active = m_turnManager.activeTurn(sessionId))
            active->requestTraceId = startedTurn.requestTraceId;
    }
    m_agentActiveSession.insert(agentId, sessionId);

    Session* session = m_app.m_sessionManager ? m_app.m_sessionManager->findById(sessionId) : nullptr;
    QJsonArray runtimeHistory = buildRuntimeHistoryFromMessages(session);

    bool snapshotOk = false;
    const ConversationContext::TaskContextSnapshot snapshot =
        m_app.m_persistence ? m_app.m_persistence->loadTaskContextSnapshot(sessionId, &snapshotOk)
                            : ConversationContext::TaskContextSnapshot();
    if (!snapshot.toJson().isEmpty()) {
        QJsonObject snapshotMsg;
        snapshotMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
        const QString snapshotContent = QStringLiteral("## Current Task Snapshot\n%1")
                                            .arg(QString::fromUtf8(
                                                     QJsonDocument(snapshot.toJson())
                                                         .toJson(QJsonDocument::Indented))
                                                     .trimmed());
        snapshotMsg.insert(QStringLiteral("content"), snapshotContent);
        runtimeHistory.prepend(snapshotMsg);

        QJsonObject snapshotExtra;
        snapshotExtra.insert(QStringLiteral("snapshot_id"),
                             snapshot.toJson().value(QStringLiteral("snapshot_id")).toString());
        snapshotExtra.insert(QStringLiteral("phase"),
                             snapshot.toJson().value(QStringLiteral("current_phase")).toString());
        snapshotExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
        emitPipelineEvent(QStringLiteral("context.snapshot.updated"),
                          sessionId,
                          &startedTurn,
                          QString(),
                          QString(),
                          snapshotExtra,
                          true);
    } else if (!snapshotOk) {
        QJsonObject snapshotErrorExtra;
        snapshotErrorExtra.insert(QStringLiteral("reason"), QStringLiteral("snapshot_load_failed"));
        emitPipelineEvent(QStringLiteral("context.compact.error"),
                          sessionId,
                          &startedTurn,
                          QString(),
                          QStringLiteral("snapshot_load_failed"),
                          snapshotErrorExtra,
                          true);
    }

    bool checkpointOk = false;
    const ConversationContext::ContextCompressionCheckpoint checkpoint =
        m_app.m_persistence
        ? m_app.m_persistence->loadContextCompressionCheckpoint(sessionId, &checkpointOk)
        : ConversationContext::ContextCompressionCheckpoint();
    if (!checkpoint.toJson().isEmpty()) {
        QJsonObject compactExtra;
        compactExtra.insert(QStringLiteral("checkpoint_id"),
                            checkpoint.toJson().value(QStringLiteral("checkpoint_id")).toString());
        compactExtra.insert(QStringLiteral("reason"),
                            checkpoint.toJson().value(QStringLiteral("reason")).toString());
        compactExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
        compactExtra.insert(QStringLiteral("historyChars"),
                            static_cast<double>(estimateHistoryChars(runtimeHistory)));
        emitPipelineEvent(QStringLiteral("context.compacted"),
                          sessionId,
                          &startedTurn,
                          QString(),
                          QString(),
                          compactExtra,
                          true);
    }

    const QStringList injections = m_teammateInjections.take(sessionId);
    for (const QString& injection : injections) {
        QJsonObject item;
        item.insert(QStringLiteral("role"), QStringLiteral("user"));
        item.insert(QStringLiteral("content"), injection);
        runtimeHistory.append(item);
    }

    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (!runtime)
        return;
    runtime->setHistory(runtimeHistory);
    if (!runtimeHistory.isEmpty()) {
        const QJsonObject first = runtimeHistory.first().toObject();
        const QString firstRole = first.value(QStringLiteral("role")).toString();
        const QString firstContent = first.value(QStringLiteral("content")).toString();
        if (firstRole == QLatin1String("system")
            && firstContent.startsWith(QStringLiteral("[Context Compact]"))) {
            QJsonObject compactExtra;
            compactExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
            compactExtra.insert(QStringLiteral("historyChars"),
                                static_cast<double>(estimateHistoryChars(runtimeHistory)));
            emitPipelineEvent(QStringLiteral("context.compacted"),
                              sessionId,
                              &startedTurn,
                              QString(),
                              QString(),
                              compactExtra,
                              true);
        }
    }

    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
        state.isStreaming = true;
    }

    emitPipelineEvent(QStringLiteral("turn_started"),
                      sessionId,
                      &startedTurn,
                      QString(),
                      QString(),
                      QJsonObject(),
                      true);

    QJsonObject taskExtra;
    taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_started"));
    taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_started"));
    taskExtra.insert(QStringLiteral("summary"), taskStateTextPreview(startedTurn.userContent, 220));
    taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("执行中"));
    taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("等待模型输出或工具结果"));
    taskExtra.insert(QStringLiteral("last_error"), QJsonValue::Null);
    taskExtra.insert(QStringLiteral("waiting_job_id"), QJsonValue::Null);
    updateTaskStateForSession(sessionId, QStringLiteral("running"), &startedTurn, taskExtra);
    if (m_app.m_memoryService)
        m_app.m_memoryService->reportPulseProgress(agentId, QStringLiteral("turn_started"));

    if (m_app.m_memoryService)
        m_app.m_memoryService->ensureMemoryInitializedForAgent(runtimeIdentity);
    LLMConfig runtimeConfig = composeConfigForIdentity(runtimeIdentity);

    const QString memoryContext = m_app.m_memoryService && m_app.m_memoryService->memoryManager()
        ? m_app.m_memoryService->memoryManager()->composeMemoryContext(agentId,
                                                                       kMemoryContextMaxChars)
        : QString();
    if (!memoryContext.isEmpty()) {
        if (runtimeConfig.systemPrompt.trimmed().isEmpty()) {
            runtimeConfig.systemPrompt = memoryContext;
        } else {
            runtimeConfig.systemPrompt =
                runtimeConfig.systemPrompt.trimmed() + QStringLiteral("\n\n") + memoryContext;
        }
        QJsonObject extra;
        extra.insert(QStringLiteral("memoryContextChars"), memoryContext.size());
        emitPipelineEvent(QStringLiteral("memory.recalled"),
                          sessionId,
                          &startedTurn,
                          QString(),
                          QString(),
                          extra,
                          true);
    }

    const QString delegateContext =
        DelegateTaskScheduler::instance()->formatActiveJobsContext(agentId);
    if (!delegateContext.isEmpty()) {
        if (runtimeConfig.systemPrompt.trimmed().isEmpty()) {
            runtimeConfig.systemPrompt = delegateContext;
        } else {
            runtimeConfig.systemPrompt =
                runtimeConfig.systemPrompt.trimmed() + QStringLiteral("\n\n") + delegateContext;
        }
    }

    QJsonObject dispatchExtra;
    dispatchExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
    dispatchExtra.insert(QStringLiteral("historyChars"),
                         static_cast<double>(estimateHistoryChars(runtimeHistory)));
    if (!runtimeConfig.selectedModelId.trimmed().isEmpty())
        dispatchExtra.insert(QStringLiteral("model"), runtimeConfig.selectedModelId.trimmed());
    emitPipelineEvent(QStringLiteral("turn_dispatch_prepare"),
                      sessionId,
                      &startedTurn,
                      QString(),
                      QString(),
                      dispatchExtra,
                      true);

    runtime->setConfig(runtimeConfig);
    emitPipelineEvent(QStringLiteral("turn_dispatch_config_applied"),
                      sessionId,
                      &startedTurn,
                      QString(),
                      QString(),
                      QJsonObject(),
                      true);

    QJsonObject ioContext;
    ioContext.insert(QStringLiteral("session_id"), sessionId);
    if (!startedTurn.requestTraceId.trimmed().isEmpty())
        ioContext.insert(QStringLiteral("trace_id"), startedTurn.requestTraceId.trimmed());
    if (!startedTurn.turnId.trimmed().isEmpty())
        ioContext.insert(QStringLiteral("turn_id"), startedTurn.turnId.trimmed());
    if (!startedTurn.runId.trimmed().isEmpty())
        ioContext.insert(QStringLiteral("run_id"), startedTurn.runId.trimmed());
    if (!agentId.trimmed().isEmpty())
        ioContext.insert(QStringLiteral("agent_id"), agentId.trimmed());
    if (!startedTurn.clientMessageId.trimmed().isEmpty())
        ioContext.insert(QStringLiteral("client_message_id"), startedTurn.clientMessageId.trimmed());
    runtime->setIoContext(ioContext);
    runtime->sendMessage(sessionId, startedTurn.userContent);
    emitPipelineEvent(QStringLiteral("turn_dispatch_sent"),
                      sessionId,
                      &startedTurn,
                      QString(),
                      QString(),
                      QJsonObject(),
                      true);
}

void ConversationService::enqueueInternalTurn(const QString& sessionId,
                                              const QString& content,
                                              const QString& clientMessageId)
{
    if (sessionId.isEmpty() || content.isEmpty())
        return;

    QStringList& injections = m_teammateInjections[sessionId];
    injections.append(content);
    if (injections.size() > kMaxTeammateInjections)
        injections = injections.mid(injections.size() - kMaxTeammateInjections);

    TurnTask turn;
    turn.userContent = content;
    turn.clientMessageId = clientMessageId.isEmpty()
        ? QStringLiteral("internal-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
        : clientMessageId;
    m_turnManager.enqueueTurn(sessionId, turn);
    tryStartNextTurn(sessionId);
}
