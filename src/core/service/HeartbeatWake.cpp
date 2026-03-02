#include "HeartbeatWake.h"

#include <QTimer>

HeartbeatWake::HeartbeatWake(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &HeartbeatWake::onCoalesceTimeout);
}

void HeartbeatWake::setHandler(const std::function<void(const QString&)>& handler)
{
    m_handler = handler;
}

void HeartbeatWake::setCoalesceMs(int ms)
{
    m_coalesceMs = qMax(10, ms);
}

int HeartbeatWake::coalesceMs() const
{
    return m_coalesceMs;
}

void HeartbeatWake::request(const QString& reason)
{
    const QString normalized = reason.trimmed().isEmpty()
        ? QStringLiteral("requested")
        : reason.trimmed();
    m_pendingReason = normalized;

    if (m_running) {
        emit skipped(QStringLiteral("running"));
        return;
    }

    if (m_scheduled)
        return;

    m_scheduled = true;
    m_timer->start(m_coalesceMs);
}

void HeartbeatWake::cancel()
{
    m_scheduled = false;
    m_pendingReason.clear();
    if (m_timer->isActive())
        m_timer->stop();
}

void HeartbeatWake::onCoalesceTimeout()
{
    m_scheduled = false;
    if (m_running) {
        emit skipped(QStringLiteral("running"));
        return;
    }

    const QString reason = m_pendingReason.trimmed().isEmpty()
        ? QStringLiteral("requested")
        : m_pendingReason.trimmed();
    m_pendingReason.clear();

    if (!m_handler) {
        emit skipped(QStringLiteral("missing_handler"));
        return;
    }

    m_running = true;
    m_handler(reason);
    m_running = false;
    emit executed(reason);
}
