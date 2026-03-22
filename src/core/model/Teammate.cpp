#include "Teammate.h"
#include <QDateTime>

Teammate::Teammate(const QString& id, const Config& config, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_name(config.name)
    , m_role(config.role)
    , m_backend(config.backend)
    , m_ownerAgentId(config.ownerAgentId)
    , m_persistence(config.persistence.trimmed().compare(QStringLiteral("temporary"), Qt::CaseInsensitive) == 0
                        ? QStringLiteral("temporary")
                        : QStringLiteral("persistent"))
    , m_autoCleanup(config.autoCleanup || m_persistence == QLatin1String("temporary"))
    , m_ephemeralOwnerTurnId(config.ephemeralOwnerTurnId)
    , m_status(Status::Idle)
    , m_turnIdleTimeoutMs(config.turnIdleTimeoutMs)
    , m_createdAtMs(QDateTime::currentMSecsSinceEpoch())
    , m_lastActiveAtMs(QDateTime::currentMSecsSinceEpoch())
    , m_workingDirectory(config.workingDirectory)
    , m_backendOverrides(config.backendOverrides)
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

void Teammate::setActiveTurnId(const QString& turnId)
{
    m_activeTurnId = turnId;
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

QString Teammate::statusToString(Status status)
{
    switch (status) {
    case Status::Idle:     return QStringLiteral("idle");
    case Status::Busy:     return QStringLiteral("busy");
    case Status::Error:    return QStringLiteral("error");
    case Status::Shutdown: return QStringLiteral("shutdown");
    }
    return QStringLiteral("unknown");
}

QJsonObject Teammate::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("teammate_id"), m_id);
    obj.insert(QStringLiteral("name"), m_name);
    obj.insert(QStringLiteral("role"), m_role);
    obj.insert(QStringLiteral("backend"), m_backend);
    obj.insert(QStringLiteral("thread_id"), m_threadId);
    obj.insert(QStringLiteral("owner_agent_id"), m_ownerAgentId);
    obj.insert(QStringLiteral("persistence"), m_persistence);
    obj.insert(QStringLiteral("auto_cleanup"), m_autoCleanup);
    obj.insert(QStringLiteral("ephemeral_owner_turn_id"), m_ephemeralOwnerTurnId);
    obj.insert(QStringLiteral("status"), statusToString(m_status));
    obj.insert(QStringLiteral("last_error"), m_lastError);
    obj.insert(QStringLiteral("turn_count"), m_turnCount);
    obj.insert(QStringLiteral("created_at_ms"), m_createdAtMs);
    obj.insert(QStringLiteral("last_active_at_ms"), m_lastActiveAtMs);
    obj.insert(QStringLiteral("working_directory"), m_workingDirectory);
    obj.insert(QStringLiteral("active_turn_id"), m_activeTurnId);
    return obj;
}
