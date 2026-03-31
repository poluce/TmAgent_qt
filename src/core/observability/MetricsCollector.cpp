#include "MetricsCollector.h"
#include "../logging/LogRecordSupport.h"

#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include <algorithm>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

MetricsCollector* MetricsCollector::instance()
{
    static MetricsCollector s_instance;
    return &s_instance;
}

// ---------------------------------------------------------------------------
// Record a tool call
// ---------------------------------------------------------------------------

void MetricsCollector::recordToolCall(const QString& toolName, bool success, qint64 durationMs)
{
    QMutexLocker lock(&m_mutex);

    CallRecord rec;
    rec.durationMs = durationMs;
    rec.success = success;
    rec.timestampMs = QDateTime::currentMSecsSinceEpoch();

    auto& records = m_records[toolName];
    records.append(rec);

    // 超过上限时移除最旧的记录
    while (records.size() > kMaxRecordsPerTool)
        records.removeFirst();
}

// ---------------------------------------------------------------------------
// Compute metrics for one tool (within sliding window)
// ---------------------------------------------------------------------------

MetricsCollector::ToolMetrics MetricsCollector::toolMetrics(const QString& toolName) const
{
    QMutexLocker lock(&m_mutex);

    ToolMetrics m;
    m.toolName = toolName;

    auto it = m_records.constFind(toolName);
    if (it == m_records.constEnd())
        return m;

    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - kWindowMs;
    QVector<qint64> durations;

    for (const CallRecord& rec : it.value()) {
        if (rec.timestampMs < cutoff)
            continue;

        m.totalCalls++;
        if (rec.success)
            m.successCount++;
        else
            m.failureCount++;

        m.totalDurationMs += rec.durationMs;

        if (m.minDurationMs < 0 || rec.durationMs < m.minDurationMs)
            m.minDurationMs = rec.durationMs;
        if (rec.durationMs > m.maxDurationMs)
            m.maxDurationMs = rec.durationMs;

        durations.append(rec.durationMs);
    }

    if (m.totalCalls > 0) {
        m.successRate = static_cast<double>(m.successCount) * 100.0
                        / static_cast<double>(m.totalCalls);
        m.avgDurationMs = static_cast<double>(m.totalDurationMs)
                          / static_cast<double>(m.totalCalls);

        // P50 / P95
        std::sort(durations.begin(), durations.end());
        const int n = durations.size();
        m.p50DurationMs = durations[n / 2];
        m.p95DurationMs = durations[qMin(static_cast<int>(n * 0.95), n - 1)];
    }

    return m;
}

// ---------------------------------------------------------------------------
// All tool metrics
// ---------------------------------------------------------------------------

QVector<MetricsCollector::ToolMetrics> MetricsCollector::allToolMetrics() const
{
    QMutexLocker lock(&m_mutex);

    QVector<ToolMetrics> result;
    result.reserve(m_records.size());

    // 需要临时解锁再调用 toolMetrics（它也会加锁），
    // 所以这里直接内联计算
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - kWindowMs;

    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
        ToolMetrics m;
        m.toolName = it.key();

        QVector<qint64> durations;
        for (const CallRecord& rec : it.value()) {
            if (rec.timestampMs < cutoff)
                continue;

            m.totalCalls++;
            if (rec.success)
                m.successCount++;
            else
                m.failureCount++;

            m.totalDurationMs += rec.durationMs;

            if (m.minDurationMs < 0 || rec.durationMs < m.minDurationMs)
                m.minDurationMs = rec.durationMs;
            if (rec.durationMs > m.maxDurationMs)
                m.maxDurationMs = rec.durationMs;

            durations.append(rec.durationMs);
        }

        if (m.totalCalls > 0) {
            m.successRate = static_cast<double>(m.successCount) * 100.0
                            / static_cast<double>(m.totalCalls);
            m.avgDurationMs = static_cast<double>(m.totalDurationMs)
                              / static_cast<double>(m.totalCalls);

            std::sort(durations.begin(), durations.end());
            const int n = durations.size();
            m.p50DurationMs = durations[n / 2];
            m.p95DurationMs = durations[qMin(static_cast<int>(n * 0.95), n - 1)];
        }

        if (m.totalCalls > 0)
            result.append(m);
    }

    return result;
}

// ---------------------------------------------------------------------------
// System metrics
// ---------------------------------------------------------------------------

MetricsCollector::SystemMetrics MetricsCollector::systemMetrics(const QString& dataRootPath) const
{
    SystemMetrics sm;

    QString root = dataRootPath;
    if (root.isEmpty())
        root = QDir::homePath() + QStringLiteral("/.tmagent");

    QString dbError;
    QSqlDatabase db = LogRecordSupport::openConnection(root, &dbError);
    if (db.isValid() && db.isOpen()) {
        QSqlQuery eventCountQuery(db);
        if (eventCountQuery.exec(QStringLiteral("SELECT COUNT(*), MIN(timestamp), MAX(timestamp) FROM events"))
            && eventCountQuery.next()) {
            sm.totalEvents = eventCountQuery.value(0).toLongLong();
            sm.oldestEvent = QDateTime::fromString(eventCountQuery.value(1).toString().trimmed(), Qt::ISODateWithMs);
            if (!sm.oldestEvent.isValid())
                sm.oldestEvent = QDateTime::fromString(eventCountQuery.value(1).toString().trimmed(), Qt::ISODate);
            sm.newestEvent = QDateTime::fromString(eventCountQuery.value(2).toString().trimmed(), Qt::ISODateWithMs);
            if (!sm.newestEvent.isValid())
                sm.newestEvent = QDateTime::fromString(eventCountQuery.value(2).toString().trimmed(), Qt::ISODate);
        }

        QSqlQuery sessionCountQuery(db);
        if (sessionCountQuery.exec(QStringLiteral("SELECT COUNT(*) FROM sessions")) && sessionCountQuery.next())
            sm.totalSessions = sessionCountQuery.value(0).toLongLong();

        const QFileInfo dbInfo(LogRecordSupport::databasePathFromRoot(root));
        sm.logFileSizeBytes = dbInfo.exists() ? dbInfo.size() : 0;
        return sm;
    }

    // 统计 logs 目录
    const QString logsDir = root + QStringLiteral("/logs");
    QDirIterator logIt(logsDir, QDir::Files, QDirIterator::Subdirectories);
    while (logIt.hasNext()) {
        logIt.next();
        sm.logFileSizeBytes += logIt.fileInfo().size();
        sm.totalEvents++;
    }

    // 统计 sessions
    const QString sessionsDir = root + QStringLiteral("/sessions/data");
    const QDir sessDir(sessionsDir);
    if (sessDir.exists())
        sm.totalSessions = sessDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();

    return sm;
}

// ---------------------------------------------------------------------------
// Format: table
// ---------------------------------------------------------------------------

QString MetricsCollector::formatMetricsTable(const QVector<ToolMetrics>& metrics)
{
    if (metrics.isEmpty())
        return QStringLiteral("(no metrics recorded)");

    // 表头
    QString out;
    out += QStringLiteral("%-20s %6s %7s %5s %7s %8s %7s %7s %7s %7s\n")
               .arg(QStringLiteral("TOOL"),
                    QStringLiteral("CALLS"),
                    QStringLiteral("SUCCESS"),
                    QStringLiteral("FAIL"),
                    QStringLiteral("RATE"),
                    QStringLiteral("AVG_MS"),
                    QStringLiteral("P50_MS"),
                    QStringLiteral("P95_MS"),
                    QStringLiteral("MIN_MS"),
                    QStringLiteral("MAX_MS"));

    for (const ToolMetrics& m : metrics) {
        out += QStringLiteral("%-20s %6lld %7lld %5lld %6.1f%% %8.0f %7lld %7lld %7lld %7lld\n")
                   .arg(m.toolName)
                   .arg(m.totalCalls)
                   .arg(m.successCount)
                   .arg(m.failureCount)
                   .arg(m.successRate)
                   .arg(m.avgDurationMs)
                   .arg(m.p50DurationMs)
                   .arg(m.p95DurationMs)
                   .arg(m.minDurationMs)
                   .arg(m.maxDurationMs);
    }

    return out;
}

// ---------------------------------------------------------------------------
// Format: JSON
// ---------------------------------------------------------------------------

QString MetricsCollector::formatMetricsJson(const QVector<ToolMetrics>& metrics)
{
    QJsonArray arr;
    for (const ToolMetrics& m : metrics) {
        QJsonObject o;
        o[QStringLiteral("tool")] = m.toolName;
        o[QStringLiteral("totalCalls")] = m.totalCalls;
        o[QStringLiteral("successCount")] = m.successCount;
        o[QStringLiteral("failureCount")] = m.failureCount;
        o[QStringLiteral("successRate")] = m.successRate;
        o[QStringLiteral("avgDurationMs")] = m.avgDurationMs;
        o[QStringLiteral("p50DurationMs")] = m.p50DurationMs;
        o[QStringLiteral("p95DurationMs")] = m.p95DurationMs;
        o[QStringLiteral("minDurationMs")] = m.minDurationMs;
        o[QStringLiteral("maxDurationMs")] = m.maxDurationMs;
        arr.append(o);
    }

    QJsonObject root;
    root[QStringLiteral("metrics")] = arr;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void MetricsCollector::reset()
{
    QMutexLocker lock(&m_mutex);
    m_records.clear();
}
