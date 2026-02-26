#include "HeartbeatService.h"

#include "HeartbeatWake.h"
#include "core/persistence/ChatPersistenceService.h"
#include <QJsonObject>
#include <QTimeZone>
#include <QTimer>

namespace {
QString normalizeAgentId(const QString& agentId)
{
    return agentId.trimmed();
}

QString normalizeReason(const QString& reason, const QString& fallback)
{
    const QString normalized = reason.trimmed();
    return normalized.isEmpty() ? fallback : normalized;
}

QDateTime makeUtcNow()
{
    return QDateTime::currentDateTimeUtc();
}

QTime parseTimeOrDefault(const QString& text, const QTime& fallback)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return fallback;
    QTime time = QTime::fromString(trimmed, QStringLiteral("HH:mm"));
    if (!time.isValid())
        time = QTime::fromString(trimmed, Qt::ISODate);
    return time.isValid() ? time : fallback;
}

} // namespace

HeartbeatService::HeartbeatService(QObject* parent)
    : QObject(parent)
    , m_tickTimer(new QTimer(this))
{
    m_tickTimer->setInterval(1000);
    connect(m_tickTimer, &QTimer::timeout, this, &HeartbeatService::onTick);
    m_tickTimer->start();
}

HeartbeatService::~HeartbeatService()
{
    stopAll();
}

void HeartbeatService::setPersistence(ChatPersistenceService* persistence)
{
    m_persistence = persistence;
}

HeartbeatService::AgentHeartbeat* HeartbeatService::ensureAgent(const QString& agentId)
{
    const QString key = normalizeAgentId(agentId);
    if (key.isEmpty())
        return nullptr;

    AgentHeartbeat* agent = m_agents.value(key, nullptr);
    if (agent)
        return agent;

    agent = new AgentHeartbeat();
    agent->config = loadConfig(key);
    agent->wake = new HeartbeatWake(this);
    agent->wake->setCoalesceMs(agent->config.coalesceMs);
    connect(agent->wake, &HeartbeatWake::skipped, this, [this, key](const QString& reason) {
        emit heartbeatSkipped(key, normalizeReason(reason, QStringLiteral("wake_skipped")));
    });
    agent->wake->setHandler([this, key](const QString& reason) {
        AgentHeartbeat* current = m_agents.value(key, nullptr);
        if (!current)
            return;
        const QDateTime nowUtc = makeUtcNow();
        current->lastRunAtUtc = nowUtc;
        current->nextRunAtUtc = nowUtc.addMSecs(qMax(1000, current->config.intervalMs));
        emit heartbeatTriggered(key, normalizeReason(reason, QStringLiteral("triggered")));
    });

    const QDateTime nowUtc = makeUtcNow();
    scheduleNextRun(agent, nowUtc);
    m_agents.insert(key, agent);
    return agent;
}

void HeartbeatService::removeAgent(const QString& agentId)
{
    const QString key = normalizeAgentId(agentId);
    AgentHeartbeat* agent = m_agents.take(key);
    if (!agent)
        return;
    if (agent->wake) {
        agent->wake->cancel();
        delete agent->wake;
        agent->wake = nullptr;
    }
    delete agent;
}

void HeartbeatService::startHeartbeat(const QString& agentId)
{
    AgentHeartbeat* agent = ensureAgent(agentId);
    if (!agent)
        return;

    if (!agent->config.enabled)
        return;

    const QDateTime nowUtc = makeUtcNow();
    if (!agent->nextRunAtUtc.isValid())
        scheduleNextRun(agent, nowUtc);
}

void HeartbeatService::stopHeartbeat(const QString& agentId)
{
    removeAgent(agentId);
}

void HeartbeatService::stopAll()
{
    const auto keys = m_agents.keys();
    for (const QString& key : keys)
        removeAgent(key);
}

void HeartbeatService::triggerHeartbeat(const QString& agentId, const QString& reason)
{
    AgentHeartbeat* agent = ensureAgent(agentId);
    if (!agent || !agent->wake) {
        emit heartbeatSkipped(normalizeAgentId(agentId), QStringLiteral("agent_missing"));
        return;
    }

    if (!agent->config.enabled) {
        emit heartbeatSkipped(normalizeAgentId(agentId), QStringLiteral("disabled"));
        return;
    }

    if (agent->suppressed) {
        emit heartbeatSkipped(normalizeAgentId(agentId), QStringLiteral("suppressed"));
        return;
    }

    if (!isWithinActiveHours(agent->config, makeUtcNow())) {
        emit heartbeatSkipped(normalizeAgentId(agentId), QStringLiteral("outside_active_hours"));
        return;
    }

    agent->wake->request(normalizeReason(reason, QStringLiteral("requested")));
}

void HeartbeatService::suppressHeartbeat(const QString& agentId, const QString& reason)
{
    AgentHeartbeat* agent = ensureAgent(agentId);
    if (!agent)
        return;
    agent->suppressed = true;
    agent->suppressReason = normalizeReason(reason, QStringLiteral("suppressed"));
}

void HeartbeatService::unsuppressHeartbeat(const QString& agentId)
{
    AgentHeartbeat* agent = ensureAgent(agentId);
    if (!agent)
        return;
    const bool wasSuppressed = agent->suppressed;
    agent->suppressed = false;
    agent->suppressReason.clear();
    if (!wasSuppressed || !agent->config.enabled)
        return;

    const QDateTime nowUtc = makeUtcNow();
    if (!agent->nextRunAtUtc.isValid() || agent->nextRunAtUtc <= nowUtc) {
        triggerHeartbeat(agentId, QStringLiteral("resume_after_suppress"));
    }
}

void HeartbeatService::updateConfig(const QString& agentId, const HeartbeatConfig& config)
{
    AgentHeartbeat* agent = ensureAgent(agentId);
    if (!agent)
        return;

    agent->config = config;
    if (agent->wake)
        agent->wake->setCoalesceMs(agent->config.coalesceMs);
    saveConfig(agentId, config);
    scheduleNextRun(agent, makeUtcNow());
}

HeartbeatConfig HeartbeatService::configForAgent(const QString& agentId) const
{
    const QString key = normalizeAgentId(agentId);
    if (key.isEmpty())
        return HeartbeatConfig();
    if (m_agents.contains(key))
        return m_agents.value(key)->config;
    return loadConfig(key);
}

QString HeartbeatService::heartbeatPathForAgent(const QString& agentId) const
{
    return configForAgent(agentId).heartbeatPath;
}

HeartbeatConfig HeartbeatService::loadConfig(const QString& agentId) const
{
    HeartbeatConfig cfg;
    if (!m_persistence)
        return cfg;

    const QString key = normalizeAgentId(agentId);
    if (key.isEmpty())
        return cfg;

    cfg.heartbeatPath = m_persistence->agentHeartbeatInstructionPath(key);
    bool ok = false;
    const QJsonObject obj = m_persistence->readJsonObject(m_persistence->agentHeartbeatConfigPath(key), &ok);
    if (!ok || obj.isEmpty())
        return cfg;

    cfg.enabled = obj.value(QStringLiteral("enabled")).toBool(cfg.enabled);
    cfg.intervalMs = qMax(1000, obj.value(QStringLiteral("intervalMs")).toInt(cfg.intervalMs));
    cfg.coalesceMs = qMax(10, obj.value(QStringLiteral("coalesceMs")).toInt(cfg.coalesceMs));
    cfg.duplicateWindowMs = qMax(1000, obj.value(QStringLiteral("duplicateWindowMs")).toInt(cfg.duplicateWindowMs));
    cfg.silentWhenNoChange = obj.value(QStringLiteral("silentWhenNoChange")).toBool(cfg.silentWhenNoChange);
    cfg.notifyOnChangeOnly = obj.value(QStringLiteral("notifyOnChangeOnly")).toBool(cfg.notifyOnChangeOnly);
    cfg.notifyMinIntervalMs = qMax(1000, obj.value(QStringLiteral("notifyMinIntervalMs")).toInt(cfg.notifyMinIntervalMs));

    const QJsonObject activeHoursObj = obj.value(QStringLiteral("activeHours")).toObject();
    cfg.activeHours.start = parseTimeOrDefault(activeHoursObj.value(QStringLiteral("start")).toString(), QTime(0, 0));
    cfg.activeHours.end = parseTimeOrDefault(activeHoursObj.value(QStringLiteral("end")).toString(), QTime(23, 59));
    cfg.activeHours.timezone = activeHoursObj.value(QStringLiteral("timezone")).toString().trimmed();

    const QString heartbeatPath = obj.value(QStringLiteral("heartbeatPath")).toString().trimmed();
    if (!heartbeatPath.isEmpty())
        cfg.heartbeatPath = heartbeatPath;
    return cfg;
}

void HeartbeatService::saveConfig(const QString& agentId, const HeartbeatConfig& config) const
{
    if (!m_persistence)
        return;

    const QString key = normalizeAgentId(agentId);
    if (key.isEmpty())
        return;

    QJsonObject obj;
    obj.insert(QStringLiteral("enabled"), config.enabled);
    obj.insert(QStringLiteral("intervalMs"), config.intervalMs);
    obj.insert(QStringLiteral("coalesceMs"), config.coalesceMs);
    obj.insert(QStringLiteral("duplicateWindowMs"), config.duplicateWindowMs);
    obj.insert(QStringLiteral("silentWhenNoChange"), config.silentWhenNoChange);
    obj.insert(QStringLiteral("notifyOnChangeOnly"), config.notifyOnChangeOnly);
    obj.insert(QStringLiteral("notifyMinIntervalMs"), config.notifyMinIntervalMs);
    obj.insert(QStringLiteral("heartbeatPath"), config.heartbeatPath);

    QJsonObject activeHoursObj;
    activeHoursObj.insert(QStringLiteral("start"), config.activeHours.start.toString(QStringLiteral("HH:mm")));
    activeHoursObj.insert(QStringLiteral("end"), config.activeHours.end.toString(QStringLiteral("HH:mm")));
    if (!config.activeHours.timezone.trimmed().isEmpty())
        activeHoursObj.insert(QStringLiteral("timezone"), config.activeHours.timezone.trimmed());
    obj.insert(QStringLiteral("activeHours"), activeHoursObj);

    m_persistence->writeJsonObject(m_persistence->agentHeartbeatConfigPath(key), obj);
}

bool HeartbeatService::isWithinActiveHours(const HeartbeatConfig& config, const QDateTime& nowUtc) const
{
    if (!config.activeHours.start.isValid() || !config.activeHours.end.isValid())
        return true;

    QTimeZone zone(config.activeHours.timezone.trimmed().toUtf8());
    if (!zone.isValid())
        zone = QTimeZone::systemTimeZone();
    const QDateTime localNow = nowUtc.toTimeZone(zone);
    const QTime nowTime = localNow.time();
    const QTime start = config.activeHours.start;
    const QTime end = config.activeHours.end;
    if (start <= end)
        return nowTime >= start && nowTime <= end;
    return nowTime >= start || nowTime <= end;
}

void HeartbeatService::scheduleNextRun(AgentHeartbeat* agent, const QDateTime& nowUtc)
{
    if (!agent)
        return;
    agent->nextRunAtUtc = nowUtc.addMSecs(qMax(1000, agent->config.intervalMs));
}

void HeartbeatService::onTick()
{
    if (m_agents.isEmpty())
        return;

    const QDateTime nowUtc = makeUtcNow();
    for (auto it = m_agents.begin(); it != m_agents.end(); ++it) {
        const QString agentId = it.key();
        AgentHeartbeat* agent = it.value();
        if (!agent || !agent->config.enabled)
            continue;
        if (agent->suppressed) {
            continue;
        }
        if (!isWithinActiveHours(agent->config, nowUtc)) {
            continue;
        }
        if (!agent->nextRunAtUtc.isValid())
            scheduleNextRun(agent, nowUtc);
        if (agent->nextRunAtUtc <= nowUtc)
            triggerHeartbeat(agentId, QStringLiteral("interval"));
    }
}
