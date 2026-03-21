#include "AlertManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

AlertManager* AlertManager::instance()
{
    static AlertManager s_instance;
    return &s_instance;
}

AlertManager::AlertManager(QObject* parent)
    : QObject(parent)
{
    // 每 60 秒清理一次过期记录
    m_cleanupTimer = new QTimer(this);
    connect(m_cleanupTimer, &QTimer::timeout, this, &AlertManager::cleanExpiredRecords);
    m_cleanupTimer->start(60000);
}

// ---------------------------------------------------------------------------
// Rule management
// ---------------------------------------------------------------------------

void AlertManager::addRule(const AlertRule& rule)
{
    // 如果同名规则已存在，先移除
    removeRule(rule.name);
    m_rules.append(rule);
}

void AlertManager::removeRule(const QString& name)
{
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules[i].name == name) {
            m_rules.removeAt(i);
            m_failureWindows.remove(name);
            return;
        }
    }
}

QVector<AlertManager::AlertRule> AlertManager::rules() const
{
    return m_rules;
}

// ---------------------------------------------------------------------------
// Event processing
// ---------------------------------------------------------------------------

void AlertManager::processEvent(const QJsonObject& event)
{
    // 只关注失败事件
    const bool hasSuccess = event.contains(QStringLiteral("success"));
    if (!hasSuccess)
        return;
    const bool success = event.value(QStringLiteral("success")).toBool(true);
    if (success)
        return;

    const QString evtToolName = event.value(QStringLiteral("tool_name")).toString();
    const QString evtEventType = event.value(QStringLiteral("event_type")).toString();
    const QDateTime now = QDateTime::currentDateTimeUtc();

    for (const AlertRule& rule : m_rules) {
        if (!rule.enabled)
            continue;

        // 匹配 toolName（空表示全部）
        if (!rule.toolName.isEmpty() && rule.toolName != evtToolName)
            continue;

        // 匹配 eventType（空表示全部）
        if (!rule.eventType.isEmpty() && rule.eventType != evtEventType)
            continue;

        // 记录失败
        FailureRecord rec;
        rec.timestamp = now;
        rec.toolName = evtToolName;
        m_failureWindows[rule.name].append(rec);

        // 清理窗口外的记录
        auto& records = m_failureWindows[rule.name];
        const QDateTime windowStart = now.addSecs(-rule.windowSeconds);
        while (!records.isEmpty() && records.first().timestamp < windowStart)
            records.removeFirst();

        // 检查是否达到阈值
        if (records.size() >= rule.failureThreshold) {
            Alert alert;
            alert.ruleName = rule.name;
            alert.toolName = evtToolName.isEmpty() ? QStringLiteral("(all)") : evtToolName;
            alert.failureCount = records.size();
            alert.triggeredAt = now;
            alert.message = QStringLiteral("Rule '%1' triggered: %2 failures in %3s window (threshold=%4)")
                                .arg(rule.name)
                                .arg(records.size())
                                .arg(rule.windowSeconds)
                                .arg(rule.failureThreshold);

            m_recentAlerts.append(alert);
            if (m_recentAlerts.size() > kMaxRecentAlerts)
                m_recentAlerts.removeFirst();

            emit alertTriggered(alert);

            // 触发后清空窗口，避免重复告警
            records.clear();
        }
    }
}

// ---------------------------------------------------------------------------
// Recent alerts
// ---------------------------------------------------------------------------

QVector<AlertManager::Alert> AlertManager::recentAlerts(int maxCount) const
{
    if (maxCount <= 0 || maxCount >= m_recentAlerts.size())
        return m_recentAlerts;

    return m_recentAlerts.mid(m_recentAlerts.size() - maxCount);
}

// ---------------------------------------------------------------------------
// Persistence (JSON)
// ---------------------------------------------------------------------------

bool AlertManager::loadRules(const QString& configPath)
{
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError)
        return false;

    const QJsonArray arr = doc.object().value(QStringLiteral("rules")).toArray();
    QVector<AlertRule> loaded;
    loaded.reserve(arr.size());

    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        AlertRule rule;
        rule.name = o.value(QStringLiteral("name")).toString();
        rule.toolName = o.value(QStringLiteral("toolName")).toString();
        rule.eventType = o.value(QStringLiteral("eventType")).toString();
        rule.failureThreshold = o.value(QStringLiteral("failureThreshold")).toInt(3);
        rule.windowSeconds = o.value(QStringLiteral("windowSeconds")).toInt(300);
        rule.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        if (!rule.name.isEmpty())
            loaded.append(rule);
    }

    m_rules = loaded;
    m_failureWindows.clear();
    return true;
}

bool AlertManager::saveRules(const QString& configPath) const
{
    QJsonArray arr;
    for (const AlertRule& rule : m_rules) {
        QJsonObject o;
        o[QStringLiteral("name")] = rule.name;
        o[QStringLiteral("toolName")] = rule.toolName;
        o[QStringLiteral("eventType")] = rule.eventType;
        o[QStringLiteral("failureThreshold")] = rule.failureThreshold;
        o[QStringLiteral("windowSeconds")] = rule.windowSeconds;
        o[QStringLiteral("enabled")] = rule.enabled;
        arr.append(o);
    }

    QJsonObject root;
    root[QStringLiteral("rules")] = arr;

    // 确保目录存在
    const QFileInfo fi(configPath);
    QDir().mkpath(fi.absolutePath());

    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void AlertManager::cleanExpiredRecords()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();

    for (auto it = m_failureWindows.begin(); it != m_failureWindows.end(); ++it) {
        // 找到该规则的窗口大小
        int windowSec = 300; // 默认
        for (const AlertRule& rule : m_rules) {
            if (rule.name == it.key()) {
                windowSec = rule.windowSeconds;
                break;
            }
        }

        const QDateTime windowStart = now.addSecs(-windowSec);
        auto& records = it.value();
        while (!records.isEmpty() && records.first().timestamp < windowStart)
            records.removeFirst();
    }
}
