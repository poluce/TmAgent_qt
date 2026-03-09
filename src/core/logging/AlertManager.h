#ifndef ALERTMANAGER_H
#define ALERTMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;

class AlertManager : public QObject {
    Q_OBJECT
public:
    struct AlertRule {
        QString name;           // 规则名称
        QString toolName;       // 监控的工具名（空=全部）
        QString eventType;      // 监控的事件类型（空=全部）
        int failureThreshold = 3;   // 失败次数阈值
        int windowSeconds = 300;    // 时间窗口（秒）
        bool enabled = true;
    };

    struct Alert {
        QString ruleName;
        QString toolName;
        QString message;
        QDateTime triggeredAt;
        int failureCount = 0;
    };

    static AlertManager* instance();

    void addRule(const AlertRule& rule);
    void removeRule(const QString& name);
    QVector<AlertRule> rules() const;

    // 接收事件，检查是否触发告警
    void processEvent(const QJsonObject& event);

    // 获取最近的告警
    QVector<Alert> recentAlerts(int maxCount = 20) const;

    // 从配置文件加载规则
    bool loadRules(const QString& configPath);
    bool saveRules(const QString& configPath) const;

signals:
    void alertTriggered(const Alert& alert);

private:
    explicit AlertManager(QObject* parent = nullptr);

    struct FailureRecord {
        QDateTime timestamp;
        QString toolName;
    };

    void cleanExpiredRecords();

    QVector<AlertRule> m_rules;
    QHash<QString, QVector<FailureRecord>> m_failureWindows;  // ruleName -> records
    QVector<Alert> m_recentAlerts;
    QTimer* m_cleanupTimer = nullptr;

    static const int kMaxRecentAlerts = 100;
};

#endif // ALERTMANAGER_H
