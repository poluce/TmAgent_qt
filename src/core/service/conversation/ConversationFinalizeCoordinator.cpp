#include "ConversationFinalizeCoordinator.h"

ConversationFinalizeCoordinator::ConversationFinalizeCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void ConversationFinalizeCoordinator::finalizeTurn(const QString& sessionId, TurnTask* outTurn)
{
    if (!m_dependencies.findPipeline
        || !m_dependencies.activeTurn
        || !m_dependencies.flushPendingDeltaLog
        || !m_dependencies.turnManager
        || !m_dependencies.clearDelegateStartsForSession
        || !m_dependencies.clearToolProgressCacheForSession
        || !m_dependencies.agentIdentityIdForSession
        || !m_dependencies.activeSessionForAgent
        || !m_dependencies.clearActiveSessionForAgent
        || !m_dependencies.resetSessionStreamState
        || !m_dependencies.tryStartNextTurn
        || !m_dependencies.tryStartNextTurnForAgent) {
        return;
    }

    SessionPipeline* pipeline = m_dependencies.findPipeline(sessionId);
    TurnTask* activeTurn = m_dependencies.activeTurn(sessionId);
    if (pipeline && activeTurn)
        m_dependencies.flushPendingDeltaLog(sessionId, pipeline, activeTurn, true);

    m_dependencies.turnManager->clearActiveTurn(sessionId, outTurn);
    m_dependencies.clearDelegateStartsForSession(sessionId);
    m_dependencies.clearToolProgressCacheForSession(sessionId);

    const QString agentId = m_dependencies.agentIdentityIdForSession(sessionId);
    if (!agentId.isEmpty() && m_dependencies.activeSessionForAgent(agentId) == sessionId)
        m_dependencies.clearActiveSessionForAgent(agentId);
    m_dependencies.resetSessionStreamState(sessionId);

    m_dependencies.tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        m_dependencies.tryStartNextTurnForAgent(agentId);
}
