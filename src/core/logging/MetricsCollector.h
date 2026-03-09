#ifndef METRICSCOLLECTOR_H
#define METRICSCOLLECTOR_H

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QVector>

class MetricsCollector {
public:
    struct ToolMetrics {
        QString toolName;
        qint64 totalCalls = 0;
        qint64 successCount = 0;
        qint64 failureCount = 0;
        double successRate = 0.0;
        qint64 totalDurationMs = 0;
        qint64 minDurationMs = -1;
        qint64 maxDurationMs = -1;
        double avgDurationMs = 0.0;
        qint64 p50DurationMs = 0;
        qint64 p95DurationMs = 0;
    };

    struct SystemMetrics {
        qint64 totalEvents = 0;
        qint64 totalSessions = 0;
        qint64 logFileSizeBytes = 0;
        QDateTime oldestEvent;
        QDateTime newestEvent;
    };

    static MetricsCollector* instance();

    // 记录一次工具调用完成
    void recordToolCall(const QString& toolName, bool success, qint64 durationMs);

    // 获取指定工具的指标
    ToolMetrics toolMetrics(const QString& toolName) const;

    // 获取所有工具的指标
    QVector<ToolMetrics> allToolMetrics() const;

    // 获取系统级指标
    SystemMetrics systemMetrics(const QString& dataRootPath = QString()) const;

    // 格式化输出
    static QString formatMetricsTable(const QVector<ToolMetrics>& metrics);
    static QString formatMetricsJson(const QVector<ToolMetrics>& metrics);

    // 重置统计
    void reset();

private:
    MetricsCollector() = default;

    struct CallRecord {
        qint64 durationMs;
        bool success;
        qint64 timestampMs;
    };

    mutable QMutex m_mutex;
    QHash<QString, QVector<CallRecord>> m_records;  // toolName -> records

    static const int kMaxRecordsPerTool = 10000;
    static const qint64 kWindowMs = 3600000;  // 1小时滑动窗口
};

#endif // METRICSCOLLECTOR_H
