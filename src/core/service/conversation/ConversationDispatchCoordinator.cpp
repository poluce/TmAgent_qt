#include "ConversationDispatchCoordinator.h"

#include <QJsonDocument>
#include "core/model/Identity.h"
#include "core/model/Session.h"
#include <QJsonObject>
#include <QUuid>

ConversationDispatchCoordinator::ConversationDispatchCoordinator(const Dependencies& dependencies,
                                                                 const Limits& limits)
    : m_dependencies(dependencies)
    , m_limits(limits)
{
}

bool ConversationDispatchCoordinator::tryStartNextTurn(const QString& sessionId)
{
    if (!m_dependencies.turnManager
        || !m_dependencies.findPipeline
        || !m_dependencies.ensureRuntimeIdentityForSession
        || !m_dependencies.findSession
        || !m_dependencies.activeSessionForAgent
        || !m_dependencies.setActiveSessionForAgent
        || !m_dependencies.buildRuntimeHistoryFromMessages
        || !m_dependencies.estimateHistoryChars
        || !m_dependencies.setRuntimeHistory
        || !m_dependencies.setRuntimeConfig
        || !m_dependencies.setRuntimeIoContext
        || !m_dependencies.sendRuntimeMessage
        || !m_dependencies.emitPipelineEvent
        || !m_dependencies.updateTaskStateForSession
        || !m_dependencies.taskStateTextPreview
        || !m_dependencies.reportPulseProgress
        || !m_dependencies.ensureMemoryInitializedForAgent
        || !m_dependencies.composeConfigForIdentity
        || !m_dependencies.composeMemoryContext
        || !m_dependencies.delegateContextForAgent
        || !m_dependencies.loadTaskContextSnapshot
        || !m_dependencies.loadContextCompressionCheckpoint) {
        return false;
    }

    SessionPipeline* pipeline = m_dependencies.findPipeline(sessionId);
    if (!pipeline)
        return false;
    if (m_dependencies.turnManager->hasActiveTurn(sessionId)
        || m_dependencies.turnManager->queuedTurnCount(sessionId) <= 0) {
        return false;
    }

    QString agentId;
    Identity* runtimeIdentity = m_dependencies.ensureRuntimeIdentityForSession(sessionId, &agentId);
    if (!runtimeIdentity)
        return false;
    if (agentId.isEmpty())
        return false;

    const QString activeSessionId = m_dependencies.activeSessionForAgent(agentId);
    if (!activeSessionId.isEmpty() && activeSessionId != sessionId)
        return false;

    TurnTask startedTurn;
    if (!m_dependencies.turnManager->startNextTurn(sessionId, &startedTurn))
        return false;
    if (startedTurn.requestTraceId.isEmpty()) {
        startedTurn.requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (TurnTask* active = m_dependencies.turnManager->activeTurn(sessionId))
            active->requestTraceId = startedTurn.requestTraceId;
    }
    m_dependencies.setActiveSessionForAgent(agentId, sessionId);

    Session* session = m_dependencies.findSession(sessionId);
    QJsonArray runtimeHistory = m_dependencies.buildRuntimeHistoryFromMessages(session);
    bool snapshotOk = false;
    const ConversationContext::TaskContextSnapshot snapshot = m_dependencies.loadTaskContextSnapshot(sessionId, &snapshotOk);
    if (!snapshot.toJson().isEmpty()) {
        QJsonObject snapshotMsg;
        snapshotMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
        const QString snapshotContent = QStringLiteral("## Current Task Snapshot\n%1")
                                            .arg(QString::fromUtf8(QJsonDocument(snapshot.toJson()).toJson(QJsonDocument::Indented)).trimmed());
        snapshotMsg.insert(QStringLiteral("content"), snapshotContent);
        runtimeHistory.prepend(snapshotMsg);

        QJsonObject snapshotExtra;
        snapshotExtra.insert(QStringLiteral("snapshot_id"), snapshot.toJson().value(QStringLiteral("snapshot_id")).toString());
        snapshotExtra.insert(QStringLiteral("phase"), snapshot.toJson().value(QStringLiteral("current_phase")).toString());
        snapshotExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("context.snapshot.updated"),
            &startedTurn,
            QString(),
            QString(),
            snapshotExtra,
            true);
    } else if (!snapshotOk) {
        QJsonObject snapshotErrorExtra;
        snapshotErrorExtra.insert(QStringLiteral("reason"), QStringLiteral("snapshot_load_failed"));
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("context.compact.error"),
            &startedTurn,
            QString(),
            QStringLiteral("snapshot_load_failed"),
            snapshotErrorExtra,
            true);
    }

    bool checkpointOk = false;
    const ConversationContext::ContextCompressionCheckpoint checkpoint =
        m_dependencies.loadContextCompressionCheckpoint(sessionId, &checkpointOk);
    if (!checkpoint.toJson().isEmpty()) {
        QJsonObject compactExtra;
        compactExtra.insert(QStringLiteral("checkpoint_id"), checkpoint.toJson().value(QStringLiteral("checkpoint_id")).toString());
        compactExtra.insert(QStringLiteral("reason"), checkpoint.toJson().value(QStringLiteral("reason")).toString());
        compactExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
        compactExtra.insert(QStringLiteral("historyChars"), static_cast<double>(m_dependencies.estimateHistoryChars(runtimeHistory)));
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("context.compacted"),
            &startedTurn,
            QString(),
            QString(),
            compactExtra,
            true);
    }
    m_dependencies.setRuntimeHistory(sessionId, runtimeHistory);
    if (!runtimeHistory.isEmpty()) {
        const QJsonObject first = runtimeHistory.first().toObject();
        const QString firstRole = first.value(QStringLiteral("role")).toString();
        const QString firstContent = first.value(QStringLiteral("content")).toString();
        if (firstRole == QLatin1String("system")
            && firstContent.startsWith(QStringLiteral("[Context Compact]"))) {
            QJsonObject compactExtra;
            compactExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
            compactExtra.insert(QStringLiteral("historyChars"), static_cast<double>(m_dependencies.estimateHistoryChars(runtimeHistory)));
            m_dependencies.emitPipelineEvent(
                sessionId,
                QStringLiteral("context.compacted"),
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

    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_started"),
        &startedTurn,
        QString(),
        QString(),
        QJsonObject(),
        true);

    QJsonObject taskExtra;
    taskExtra.insert(QStringLiteral("reason"), QStringLiteral("turn_started"));
    taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("turn_started"));
    taskExtra.insert(QStringLiteral("summary"), m_dependencies.taskStateTextPreview(startedTurn.userContent, 220));
    taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("执行中"));
    taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("等待模型输出或工具结果"));
    taskExtra.insert(QStringLiteral("last_error"), QJsonValue::Null);
    taskExtra.insert(QStringLiteral("waiting_job_id"), QJsonValue::Null);
    m_dependencies.updateTaskStateForSession(
        sessionId,
        QStringLiteral("running"),
        &startedTurn,
        taskExtra);
    m_dependencies.reportPulseProgress(agentId, QStringLiteral("turn_started"));

    m_dependencies.ensureMemoryInitializedForAgent(runtimeIdentity);
    LLMConfig runtimeConfig = m_dependencies.composeConfigForIdentity(runtimeIdentity);

    const QString memoryContext = m_dependencies.composeMemoryContext(agentId, m_limits.memoryContextMaxChars);
    if (!memoryContext.isEmpty()) {
        if (runtimeConfig.systemPrompt.trimmed().isEmpty()) {
            runtimeConfig.systemPrompt = memoryContext;
        } else {
            runtimeConfig.systemPrompt = runtimeConfig.systemPrompt.trimmed()
                + QStringLiteral("\n\n")
                + memoryContext;
        }
        QJsonObject extra;
        extra.insert(QStringLiteral("memoryContextChars"), memoryContext.size());
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("memory.recalled"),
            &startedTurn,
            QString(),
            QString(),
            extra,
            true);
    }

    const QString delegateContext = m_dependencies.delegateContextForAgent(agentId);
    if (!delegateContext.isEmpty()) {
        if (runtimeConfig.systemPrompt.trimmed().isEmpty()) {
            runtimeConfig.systemPrompt = delegateContext;
        } else {
            runtimeConfig.systemPrompt = runtimeConfig.systemPrompt.trimmed()
                + QStringLiteral("\n\n")
                + delegateContext;
        }
    }

    QJsonObject dispatchExtra;
    dispatchExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
    dispatchExtra.insert(QStringLiteral("historyChars"), static_cast<double>(m_dependencies.estimateHistoryChars(runtimeHistory)));
    if (!runtimeConfig.selectedModelId.trimmed().isEmpty())
        dispatchExtra.insert(QStringLiteral("model"), runtimeConfig.selectedModelId.trimmed());
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_dispatch_prepare"),
        &startedTurn,
        QString(),
        QString(),
        dispatchExtra,
        true);

    m_dependencies.setRuntimeConfig(sessionId, runtimeConfig);
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_dispatch_config_applied"),
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
    m_dependencies.setRuntimeIoContext(sessionId, ioContext);
    m_dependencies.sendRuntimeMessage(sessionId, startedTurn.userContent);
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_dispatch_sent"),
        &startedTurn,
        QString(),
        QString(),
        QJsonObject(),
        true);
    return true;
}
