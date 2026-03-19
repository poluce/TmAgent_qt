#include "Teammate.h"
#include <QDateTime>

Teammate::Teammate(const QString& id, const Config& config, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_name(config.name)
    , m_role(config.role)
    , m_backend(config.backend)
    , m_status(Status::Idle)
    , m_turnIdleTimeoutMs(config.turnIdleTimeoutMs)
    , m_workingDirectory(config.workingDirectory)
    , m_backendOverrides(config.backendOverrides)
    , m_createdAtMs(QDateTime::currentMSecsSinceEpoch())
    , m_lastActiveAtMs(QDateTime::currentMSecsSinceEpoch())
{
}

void Teammate::setName(const QString& name)
{
    if (m_name == name)
        return;
    m_name = name;
    emit nameChanged(m_name);
}

void Teammate::setRole(const QString& role)
{
    if (m_role == role)
        return;
    m_role = role;
    emit roleChanged(m_role);
}

void Teammate::setStatus(Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(m_status);
}

void Teammate::setThreadId(const QString& threadId)
{
    m_threadId = threadId;
}

void Teammate::setLastError(const QString& error)
{
    m_lastError = error;
}

void Teammate::incrementTurnCount()
{
    ++m_turnCount;
}

void Teammate::touchLastActive()
{
    m_lastActiveAtMs = QDateTime::currentMSecsSinceEpoch();
}

QJsonObject Teammate::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("teammate_id"), m_id);
    obj.insert(QStringLiteral("name"), m_name);
    obj.insert(QStringLiteral("role"), m_role);
    obj.insert(QStringLiteral("backend"), m_backend);
    obj.insert(QStringLiteral("thread_id"), m_threadId);
    obj.insert(QStringLiteral("status"),
               m_status == Status::Idle ? QStringLiteral("idle")
               : m_status == Status::Busy ? QStringLiteral("busy")
               : m_status == Status::Error ? QStringLiteral("error")
               : QStringLiteral("shutdown"));
    obj.insert(QStringLiteral("last_error"), m_lastError);
    obj.insert(QStringLiteral("turn_count"), m_turnCount);
    obj.insert(QStringLiteral("created_at_ms"), m_createdAtMs);
    obj.insert(QStringLiteral("last_active_at_ms"), m_lastActiveAtMs);
    obj.insert(QStringLiteral("working_directory"), m_workingDirectory);
    return obj;
}
