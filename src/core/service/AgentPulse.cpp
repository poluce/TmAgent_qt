#include "AgentPulse.h"

#include <QTimer>

namespace {
AgentPulse::State stateFromIdleMs(qint64 idleMs, int softTimeoutMs, int hardTimeoutMs, int stallMs)
{
    if (idleMs < 0)
        return AgentPulse::Dead;
    if (idleMs >= hardTimeoutMs)
        return AgentPulse::HardTimeout;
    if (idleMs >= stallMs)
        return AgentPulse::Stalled;
    if (idleMs >= softTimeoutMs)
        return AgentPulse::SoftTimeout;
    return AgentPulse::Healthy;
}

} // namespace

AgentPulse::AgentPulse(const QString& agentId, QObject* parent)
    : QObject(parent)
    , m_agentId(agentId.trimmed())
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &AgentPulse::onTick);
    m_lastProgressTimer.start();
}

void AgentPulse::start(int intervalMs)
{
    m_timer->setInterval(qMax(200, intervalMs));
    if (!m_timer->isActive())
        m_timer->start();
    if (!m_lastProgressTimer.isValid())
        m_lastProgressTimer.start();
}

void AgentPulse::stop()
{
    if (m_timer->isActive())
        m_timer->stop();
}

void AgentPulse::reportProgress(const QString& summary)
{
    Q_UNUSED(summary);
    if (!m_lastProgressTimer.isValid())
        m_lastProgressTimer.start();
    else
        m_lastProgressTimer.restart();
    if (m_state != Healthy) {
        m_state = Healthy;
        emit stateChanged(m_agentId, m_state);
    }
}

AgentPulse::State AgentPulse::currentState() const
{
    return m_state;
}

qint64 AgentPulse::idleMs() const
{
    if (!m_lastProgressTimer.isValid())
        return -1;
    return m_lastProgressTimer.elapsed();
}

void AgentPulse::setThresholds(int softTimeoutMs, int hardTimeoutMs, int stallMs)
{
    const int safeSoft = qMax(1000, softTimeoutMs);
    const int safeStall = qMax(safeSoft + 1000, stallMs);
    const int safeHard = qMax(safeStall + 1000, hardTimeoutMs);
    m_softTimeoutMs = safeSoft;
    m_stallMs = safeStall;
    m_hardTimeoutMs = safeHard;
}

void AgentPulse::onTick()
{
    const qint64 idle = idleMs();
    const State nextState = stateFromIdleMs(idle, m_softTimeoutMs, m_hardTimeoutMs, m_stallMs);
    if (nextState == m_state)
        return;

    m_state = nextState;
    emit stateChanged(m_agentId, m_state);
    if (m_state == HardTimeout)
        emit hardTimeoutReached(m_agentId);
}
