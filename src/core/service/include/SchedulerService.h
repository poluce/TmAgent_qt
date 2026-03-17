#ifndef SCHEDULERSERVICE_H
#define SCHEDULERSERVICE_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>

class QTimer;
class ChatPersistenceService;

struct ScheduledJob {
    QString jobId;
    QString name;
    QString agentId;
    QString prompt;
    QString cronExpr;
    QString timezone;
    QString sessionTarget = QStringLiteral("main"); // main / isolated
    bool enabled = true;
    QDateTime nextFireAtUtc;
    QDateTime lastFireAtUtc;
};

class SchedulerService : public QObject {
    Q_OBJECT
public:
    explicit SchedulerService(QObject* parent = nullptr);
    ~SchedulerService() override;

    void setPersistence(ChatPersistenceService* persistence);

    QString addJob(const ScheduledJob& job);
    bool removeJob(const QString& jobId);
    bool updateJob(const QString& jobId, const ScheduledJob& job);
    bool enableJob(const QString& jobId, bool enabled);

    void triggerJob(const QString& jobId);

    QList<ScheduledJob> allJobs() const;
    bool jobById(const QString& jobId, ScheduledJob* outJob) const;

    void start();
    void stop();
    bool reload();

signals:
    void jobFired(const QString& jobId, const QString& jobName);
    void jobCompleted(const QString& jobId, const QString& status);
    void jobFailed(const QString& jobId, const QString& error);

private slots:
    void onTick();

private:
    QDateTime nextFireTime(const QString& cronExpr, const QDateTime& afterUtc, const QString& timezone) const;
    bool saveJobs() const;
    bool loadJobs();
    static bool matchCronField(const QString& expr, int value, int minValue, int maxValue);

    ChatPersistenceService* m_persistence = nullptr;
    QHash<QString, ScheduledJob> m_jobs;
    QTimer* m_tickTimer = nullptr;
};

#endif // SCHEDULERSERVICE_H
