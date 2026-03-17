#include "ConversationEnqueueCoordinator.h"

#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/Message.h"
#include "core/model/Session.h"
#include "MessageRouter.h"
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QUuid>

ConversationEnqueueCoordinator::ConversationEnqueueCoordinator(const Dependencies& dependencies,
                                                               const Limits& limits)
    : m_dependencies(dependencies)
    , m_limits(limits)
{
}

QString ConversationEnqueueCoordinator::enqueueUserMessageAs(const QString& actorIdentityId,
                                                             const QString& sessionId,
                                                             const QString& text,
                                                             const QString& clientMessageId)
{
    if (!m_dependencies.identityManager
        || !m_dependencies.sessionManager
        || !m_dependencies.turnManager
        || !m_dependencies.canIdentitySendMessage
        || !m_dependencies.emitPipelineEvent
        || !m_dependencies.updateTaskStateForSession
        || !m_dependencies.tryStartNextTurn
        || !m_dependencies.isBackgroundClientMessage
        || !m_dependencies.taskStateTextPreview) {
        return QString();
    }

    if (!m_dependencies.canIdentitySendMessage(actorIdentityId, sessionId)) {
        qWarning() << "[ConversationEnqueueCoordinator] 拒绝发送消息，actor 无权限:"
                   << actorIdentityId << "session:" << sessionId;
        return QString();
    }

    const QString prompt = text.trimmed();
    if (prompt.isEmpty())
        return QString();

    Session* session = m_dependencies.sessionManager->findById(sessionId);
    if (!session)
        return QString();

    Identity* actor = m_dependencies.identityManager->findById(actorIdentityId);
    if (!actor)
        return QString();
    const QString actorId = actor->id().trimmed();
    if (actorId.isEmpty())
        return QString();

    m_dependencies.turnManager->ensurePipeline(sessionId);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    QStringList participantAgentIds;
    QHash<QString, QString> participantAgentNames;
    for (const QString& participantId : session->participantIds()) {
        Identity* participant = m_dependencies.identityManager->findById(participantId);
        if (!participant || !participant->isAgent())
            continue;
        participantAgentIds.append(participant->id());
        participantAgentNames.insert(participant->id(), participant->name());
    }

    MessageRouter::RouteInput routeInput;
    routeInput.sessionType = session->type();
    routeInput.senderIdentityId = actorId;
    routeInput.userIdentityId = m_dependencies.identityManager->userIdentity()
        ? m_dependencies.identityManager->userIdentity()->id()
        : QString();
    routeInput.text = prompt;
    routeInput.participantAgentIds = participantAgentIds;
    routeInput.agentDisplayNames = participantAgentNames;
    const MessageRouter::RouteResult routeResult = MessageRouter::route(routeInput);

    TurnTask* mergeTarget = nullptr;
    if (TurnTask* tail = m_dependencies.turnManager->queuedTail(sessionId)) {
        const QString tailActorId = tail->actorIdentityId.trimmed();
        const int tailMergedCount = qMax(1, tail->mergedMessageCount);
        const bool sameActor = !tailActorId.isEmpty() && tailActorId == actorId;
        const bool withinWindow = tail->enqueuedAtMs > 0
            && nowMs >= tail->enqueuedAtMs
            && (nowMs - tail->enqueuedAtMs) <= m_limits.queueMergeWindowMs;
        const bool withinMergeCount = tailMergedCount < m_limits.queueMergeMaxMergedMessages;
        const bool withinMergedSize = (tail->userContent.size() + prompt.size() + 32)
            <= m_limits.queueMergeMaxChars;
        if (sameActor && withinWindow && withinMergeCount && withinMergedSize)
            mergeTarget = tail;
    }

    const int queueDepthBeforeEnqueue = m_dependencies.turnManager->totalDepth(sessionId);
    if (!mergeTarget && queueDepthBeforeEnqueue >= m_limits.hardQueueDepth) {
        QJsonObject extra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("queue_overflow"));
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        extra.insert(QStringLiteral("queueHardLimit"), m_limits.hardQueueDepth);
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("turn_rejected"),
            nullptr,
            QString(),
            QStringLiteral("queue overflow"),
            extra,
            true);
        return QString();
    }

    if (!mergeTarget && queueDepthBeforeEnqueue >= m_limits.softQueueDepth) {
        QJsonObject extra;
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        extra.insert(QStringLiteral("queueSoftLimit"), m_limits.softQueueDepth);
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("queue_backpressure"),
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

    const bool skipPersistUserMessage = m_dependencies.isBackgroundClientMessage(turn.clientMessageId);
    if (!skipPersistUserMessage) {
        Message userMsg = Message::createText(sessionId, actorId, prompt);
        userMsg.traceId = requestTraceId;
        userMsg.turnId = turnId;
        userMsg.mentions = routeResult.targetAgentIds;
        userMsg.status = Message::Status::Completed;
        m_dependencies.sessionManager->postMessage(sessionId, userMsg);
    }

    QJsonObject routeExtra;
    routeExtra.insert(QStringLiteral("target_agent_ids"), QJsonArray::fromStringList(routeResult.targetAgentIds));
    routeExtra.insert(QStringLiteral("mention_tokens"), QJsonArray::fromStringList(routeResult.mentionTokens));
    routeExtra.insert(QStringLiteral("unresolved_mentions"), QJsonArray::fromStringList(routeResult.unresolvedMentions));
    routeExtra.insert(QStringLiteral("is_broadcast"), routeResult.isBroadcast);
    routeExtra.insert(QStringLiteral("used_default_route"), routeResult.usedDefaultRoute);
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("message_routed"),
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
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("turn_merged"),
            mergeTarget,
            QString(),
            QString(),
            extra,
            true);

        QJsonObject taskExtra;
        taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_merged"));
        taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_merged"));
        taskExtra.insert(QStringLiteral("summary"), m_dependencies.taskStateTextPreview(mergeTarget->userContent, 220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("等待调度"));
        taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("合并补充消息后开始执行"));
        m_dependencies.updateTaskStateForSession(
            sessionId,
            QStringLiteral("queued"),
            mergeTarget,
            taskExtra);
        return mergeTarget->turnId;
    }

    m_dependencies.turnManager->enqueueTurn(sessionId, turn);
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_queued"),
        &turn,
        QString(),
        QString(),
        QJsonObject(),
        true);

    QJsonObject taskExtra;
    taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_queued"));
    taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_queued"));
    taskExtra.insert(QStringLiteral("summary"), m_dependencies.taskStateTextPreview(turn.userContent, 220));
    taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("等待调度"));
    taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("准备执行用户请求"));
    m_dependencies.updateTaskStateForSession(
        sessionId,
        QStringLiteral("queued"),
        &turn,
        taskExtra);

    m_dependencies.tryStartNextTurn(sessionId);
    return turn.turnId;
}

