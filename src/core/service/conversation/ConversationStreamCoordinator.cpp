#include "ConversationStreamCoordinator.h"

#include "core/model/Session.h"
#include <QDateTime>

ConversationStreamCoordinator::ConversationStreamCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void ConversationStreamCoordinator::onRuntimeStreamData(const QString& sessionId, const QString& data)
{
    if (!m_dependencies.findPipeline
        || !m_dependencies.activeTurn
        || !m_dependencies.agentIdentityIdForSession
        || !m_dependencies.reportPulseProgress
        || !m_dependencies.isBackgroundClientMessage
        || !m_dependencies.findSession
        || !m_dependencies.emitStreamData
        || !m_dependencies.emitPipelineEvent
        || !m_dependencies.flushPendingDeltaLog) {
        return;
    }

    SessionPipeline* pipeline = m_dependencies.findPipeline(sessionId);
    TurnTask* activeTurn = m_dependencies.activeTurn(sessionId);
    if (!pipeline || !activeTurn)
        return;

    activeTurn->assistantContent.append(data);
    const bool backgroundHeartbeat = m_dependencies.isBackgroundClientMessage(activeTurn->clientMessageId);
    m_dependencies.reportPulseProgress(m_dependencies.agentIdentityIdForSession(sessionId), QStringLiteral("stream"));

    if (backgroundHeartbeat)
        return;

    Session* session = m_dependencies.findSession(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.append(data);
        state.isStreaming = true;
    }

    m_dependencies.emitStreamData(sessionId, data);
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_delta"),
        activeTurn,
        data,
        QString(),
        QJsonObject(),
        m_dependencies.logVerboseStreamEvents);

    if (!m_dependencies.logVerboseStreamEvents && !data.isEmpty()) {
        if (pipeline->pendingDeltaLog.isEmpty())
            pipeline->pendingDeltaStartedAtMs = QDateTime::currentMSecsSinceEpoch();
        pipeline->pendingDeltaLog.append(data);
        ++pipeline->pendingDeltaChunks;
        m_dependencies.flushPendingDeltaLog(sessionId, pipeline, activeTurn, false);
    }
}
