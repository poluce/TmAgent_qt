#include "SchedulerService.h"

#include "core/persistence/ChatPersistenceService.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>

namespace {

struct FiredJobRecord {
    QString jobId;
    QString jobName;
};

QString normalizeSessionTarget(const QString& value)
{
    const QString target = value.trimmed().toLower();
    if (target == QLatin1String("isolated"))
        return QStringLiteral("isolated");
    return QStringLiteral("main");
}

QString normalizeScheduleType(const QString& value)
{
    const QString scheduleType = value.trimmed().toLower();
    if (scheduleType == QLatin1String("once"))
        return QStringLiteral("once");
    return QStringLiteral("cron");
}

bool isOnceScheduleType(const QString& value)
{
    return normalizeScheduleType(value) == QLatin1String("once");
}

QTimeZone resolveTimezone(const QString& timezone)
{
    QTimeZone zone(timezone.trimmed().toUtf8());
    if (!zone.isValid())
        zone = QTimeZone::systemTimeZone();
    return zone;
}

QStringList splitCronExpr(const QString& cronExpr)
{
    return cronExpr.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

bool parseRange(const QString& part, int minValue, int maxValue, int* startOut, int* endOut)
{
    if (!startOut || !endOut)
        return false;
    const QString trimmed = part.trimmed();
    if (trimmed.isEmpty())
        return false;

    const int dashIdx = trimmed.indexOf(QLatin1Char('-'));
    if (dashIdx < 0) {
        bool ok = false;
        const int value = trimmed.toInt(&ok);
        if (!ok || value < minValue || value > maxValue)
            return false;
        *startOut = value;
        *endOut = value;
        return true;
    }

    bool okStart = false;
    bool okEnd = false;
    const int start = trimmed.left(dashIdx).toInt(&okStart);
    const int end = trimmed.mid(dashIdx + 1).toInt(&okEnd);
    if (!okStart || !okEnd || start < minValue || end > maxValue || start > end)
        return false;
    *startOut = start;
    *endOut = end;
    return true;
}

bool matchCronFieldImpl(const QString& expr, int value, int minValue, int maxValue)
{
    const QString field = expr.trimmed();
    if (field.isEmpty())
        return false;
    if (field == QLatin1String("*"))
        return true;

    const QStringList groups = field.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& rawGroup : groups) {
        const QString group = rawGroup.trimmed();
        if (group.isEmpty())
            continue;

        int step = 1;
        QString rangeExpr = group;
        const int slashIdx = group.indexOf(QLatin1Char('/'));
        if (slashIdx >= 0) {
            rangeExpr = group.left(slashIdx).trimmed();
            bool stepOk = false;
            step = group.mid(slashIdx + 1).trimmed().toInt(&stepOk);
            if (!stepOk || step <= 0)
                continue;
            if (rangeExpr.isEmpty())
                rangeExpr = QStringLiteral("*");
        }

        int start = minValue;
        int end = maxValue;
        if (rangeExpr != QLatin1String("*")) {
            if (!parseRange(rangeExpr, minValue, maxValue, &start, &end))
                continue;
        }

        if (value < start || value > end)
            continue;
        if (((value - start) % step) == 0)
            return true;
    }
    return false;
}

QDateTime computeNextFireTime(const QString& cronExpr,
                              const QDateTime& afterUtc,
                              const QString& timezone)
{
    const QStringList parts = splitCronExpr(cronExpr);
    if (parts.size() != 5)
        return QDateTime();

    const QTimeZone zone = resolveTimezone(timezone);
    QDateTime cursor = afterUtc.toTimeZone(zone);
    cursor.setTime(QTime(cursor.time().hour(), cursor.time().minute(), 0, 0));
    cursor = cursor.addSecs(60);

    const int maxIterations = 60 * 24 * 366;
    for (int i = 0; i < maxIterations; ++i) {
        const int minute = cursor.time().minute();
        const int hour = cursor.time().hour();
        const int day = cursor.date().day();
        const int month = cursor.date().month();
        int weekday = cursor.date().dayOfWeek();
        if (weekday == 7)
            weekday = 0;

        const bool weekdayMatched = matchCronFieldImpl(parts.at(4), weekday, 0, 7)
            || (weekday == 0 && matchCronFieldImpl(parts.at(4), 7, 0, 7));

        if (matchCronFieldImpl(parts.at(0), minute, 0, 59)
            && matchCronFieldImpl(parts.at(1), hour, 0, 23)
            && matchCronFieldImpl(parts.at(2), day, 1, 31)
            && matchCronFieldImpl(parts.at(3), month, 1, 12)
            && weekdayMatched) {
            return cursor.toUTC();
        }
        cursor = cursor.addSecs(60);
    }

    return QDateTime();
}

QDateTime parseUtcDateTime(const QJsonObject& obj, const QString& key)
{
    QDateTime dt = QDateTime::fromString(obj.value(key).toString().trimmed(), Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(obj.value(key).toString().trimmed(), Qt::ISODate);
    if (dt.isValid())
        dt = dt.toUTC();
    return dt;
}

bool prepareJobForStorage(ScheduledJob* job,
                          const QDateTime& nowUtc,
                          bool allowPastOnce,
                          QString* error)
{
    if (!job) {
        if (error)
            *error = QStringLiteral("job is null");
        return false;
    }

    job->jobId = job->jobId.trimmed();
    job->name = job->name.trimmed();
    job->agentId = job->agentId.trimmed();
    job->prompt = job->prompt.trimmed();
    job->scheduleType = normalizeScheduleType(job->scheduleType);
    job->cronExpr = job->cronExpr.simplified();
    job->runAtUtc = job->runAtUtc.isValid() ? job->runAtUtc.toUTC() : QDateTime();
    job->consumedAtUtc = job->consumedAtUtc.isValid() ? job->consumedAtUtc.toUTC() : QDateTime();
    job->timezone = job->timezone.trimmed();
    if (job->timezone.isEmpty())
        job->timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    job->sessionTarget = normalizeSessionTarget(job->sessionTarget);
    job->lastFireAtUtc = job->lastFireAtUtc.isValid() ? job->lastFireAtUtc.toUTC() : QDateTime();

    if (job->agentId.isEmpty() || job->prompt.isEmpty()) {
        if (error)
            *error = QStringLiteral("job 缺少 agentId 或 prompt");
        return false;
    }

    if (isOnceScheduleType(job->scheduleType)) {
        job->cronExpr.clear();
        if (!job->runAtUtc.isValid()) {
            if (error)
                *error = QStringLiteral("once 任务缺少有效的 runAtUtc");
            return false;
        }
        if (!allowPastOnce && job->runAtUtc <= nowUtc) {
            if (error)
                *error = QStringLiteral("once 任务执行时间必须晚于当前时间");
            return false;
        }
        job->nextFireAtUtc =
            (job->enabled && !job->consumedAtUtc.isValid()) ? job->runAtUtc : QDateTime();
        if (error)
            error->clear();
        return true;
    }

    job->runAtUtc = QDateTime();
    job->consumedAtUtc = QDateTime();
    if (job->cronExpr.isEmpty()) {
        if (error)
            *error = QStringLiteral("cron 任务缺少 cronExpr");
        return false;
    }

    const QDateTime nextFire = computeNextFireTime(job->cronExpr, nowUtc, job->timezone);
    if (!nextFire.isValid()) {
        if (error)
            *error = QStringLiteral("cronExpr 非法或无法计算下一次触发时间");
        return false;
    }

    job->nextFireAtUtc = job->enabled ? nextFire : QDateTime();
    if (error)
        error->clear();
    return true;
}

QJsonObject jobToJson(const ScheduledJob& job)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("jobId"), job.jobId);
    obj.insert(QStringLiteral("name"), job.name);
    obj.insert(QStringLiteral("agentId"), job.agentId);
    obj.insert(QStringLiteral("prompt"), job.prompt);
    obj.insert(QStringLiteral("scheduleType"), normalizeScheduleType(job.scheduleType));
    obj.insert(QStringLiteral("cronExpr"), job.cronExpr);
    if (job.runAtUtc.isValid())
        obj.insert(QStringLiteral("runAtUtc"), job.runAtUtc.toUTC().toString(Qt::ISODateWithMs));
    if (job.consumedAtUtc.isValid()) {
        obj.insert(QStringLiteral("consumedAtUtc"),
                   job.consumedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    }
    obj.insert(QStringLiteral("timezone"), job.timezone);
    obj.insert(QStringLiteral("sessionTarget"), job.sessionTarget);
    obj.insert(QStringLiteral("enabled"), job.enabled);
    if (job.nextFireAtUtc.isValid()) {
        obj.insert(QStringLiteral("nextFireAtUtc"),
                   job.nextFireAtUtc.toUTC().toString(Qt::ISODateWithMs));
    }
    if (job.lastFireAtUtc.isValid()) {
        obj.insert(QStringLiteral("lastFireAtUtc"),
                   job.lastFireAtUtc.toUTC().toString(Qt::ISODateWithMs));
    }
    return obj;
}

ScheduledJob jobFromJson(const QJsonObject& obj)
{
    ScheduledJob job;
    job.jobId = obj.value(QStringLiteral("jobId")).toString().trimmed();
    job.name = obj.value(QStringLiteral("name")).toString().trimmed();
    job.agentId = obj.value(QStringLiteral("agentId")).toString().trimmed();
    job.prompt = obj.value(QStringLiteral("prompt")).toString().trimmed();
    job.scheduleType = normalizeScheduleType(obj.value(QStringLiteral("scheduleType")).toString());
    job.cronExpr = obj.value(QStringLiteral("cronExpr")).toString().simplified();
    job.runAtUtc = parseUtcDateTime(obj, QStringLiteral("runAtUtc"));
    job.consumedAtUtc = parseUtcDateTime(obj, QStringLiteral("consumedAtUtc"));
    job.timezone = obj.value(QStringLiteral("timezone")).toString().trimmed();
    job.sessionTarget = normalizeSessionTarget(obj.value(QStringLiteral("sessionTarget")).toString());
    job.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    job.nextFireAtUtc = parseUtcDateTime(obj, QStringLiteral("nextFireAtUtc"));
    job.lastFireAtUtc = parseUtcDateTime(obj, QStringLiteral("lastFireAtUtc"));
    return job;
}

} // namespace

SchedulerService::SchedulerService(QObject* parent)
    : QObject(parent)
    , m_tickTimer(new QTimer(this))
{
    m_tickTimer->setInterval(15000);
    connect(m_tickTimer, &QTimer::timeout, this, &SchedulerService::onTick);
}

SchedulerService::~SchedulerService() = default;

void SchedulerService::setPersistence(ChatPersistenceService* persistence)
{
    m_persistence = persistence;
}

QString SchedulerService::addJob(const ScheduledJob& inputJob)
{
    ScheduledJob job = inputJob;
    if (job.jobId.trimmed().isEmpty())
        job.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QString error;
    if (!prepareJobForStorage(&job, QDateTime::currentDateTimeUtc(), false, &error))
        return QString();

    m_jobs.insert(job.jobId, job);
    saveJobs();
    return job.jobId;
}

bool SchedulerService::removeJob(const QString& jobId)
{
    const QString key = jobId.trimmed();
    if (key.isEmpty() || !m_jobs.contains(key))
        return false;
    m_jobs.remove(key);
    return saveJobs();
}

bool SchedulerService::updateJob(const QString& jobId, const ScheduledJob& inputJob)
{
    const QString key = jobId.trimmed();
    if (key.isEmpty() || !m_jobs.contains(key))
        return false;

    ScheduledJob job = inputJob;
    job.jobId = key;
    QString error;
    if (!prepareJobForStorage(&job, QDateTime::currentDateTimeUtc(), false, &error))
        return false;

    m_jobs.insert(key, job);
    return saveJobs();
}

bool SchedulerService::enableJob(const QString& jobId, bool enabled)
{
    const QString key = jobId.trimmed();
    if (key.isEmpty() || !m_jobs.contains(key))
        return false;

    ScheduledJob job = m_jobs.value(key);
    if (isOnceScheduleType(job.scheduleType) && job.consumedAtUtc.isValid())
        return false;

    job.enabled = enabled;
    QString error;
    if (!prepareJobForStorage(&job, QDateTime::currentDateTimeUtc(), false, &error))
        return false;

    m_jobs.insert(key, job);
    return saveJobs();
}

void SchedulerService::triggerJob(const QString& jobId)
{
    const QString key = jobId.trimmed();
    if (key.isEmpty() || !m_jobs.contains(key)) {
        emit jobFailed(key, QStringLiteral("job_not_found"));
        return;
    }

    ScheduledJob job = m_jobs.value(key);
    if (!job.enabled) {
        emit jobFailed(key, QStringLiteral("job_disabled"));
        return;
    }
    if (isOnceScheduleType(job.scheduleType) && job.consumedAtUtc.isValid()) {
        emit jobFailed(key, QStringLiteral("job_already_consumed"));
        return;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    job.lastFireAtUtc = nowUtc;
    if (isOnceScheduleType(job.scheduleType)) {
        job.consumedAtUtc = nowUtc;
        job.nextFireAtUtc = QDateTime();
    } else {
        job.nextFireAtUtc = nextFireTime(job.cronExpr, nowUtc, job.timezone);
    }
    m_jobs.insert(key, job);
    saveJobs();

    emit jobFired(job.jobId,
                  job.name.trimmed().isEmpty() ? QStringLiteral("scheduled-job")
                                               : job.name.trimmed());
}

bool SchedulerService::finalizeTriggeredJob(const QString& jobId, bool success, const QString& detail)
{
    const QString key = jobId.trimmed();
    if (key.isEmpty() || !m_jobs.contains(key))
        return false;

    const ScheduledJob job = m_jobs.value(key);
    if (isOnceScheduleType(job.scheduleType)) {
        m_jobs.remove(key);
        saveJobs();
    }

    const QString finalDetail = detail.trimmed().isEmpty()
        ? (success ? QStringLiteral("completed") : QStringLiteral("failed"))
        : detail.trimmed();
    if (success)
        emit jobCompleted(key, finalDetail);
    else
        emit jobFailed(key, finalDetail);
    return true;
}

QList<ScheduledJob> SchedulerService::allJobs() const
{
    return m_jobs.values();
}

bool SchedulerService::jobById(const QString& jobId, ScheduledJob* outJob) const
{
    if (!outJob)
        return false;
    const QString key = jobId.trimmed();
    if (key.isEmpty() || !m_jobs.contains(key))
        return false;
    *outJob = m_jobs.value(key);
    return true;
}

void SchedulerService::start()
{
    reload();
    if (!m_tickTimer->isActive())
        m_tickTimer->start();
}

void SchedulerService::stop()
{
    if (m_tickTimer->isActive())
        m_tickTimer->stop();
}

bool SchedulerService::reload()
{
    return loadJobs();
}

void SchedulerService::onTick()
{
    if (m_jobs.isEmpty())
        return;

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    bool changed = false;
    QList<FiredJobRecord> firedJobs;
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        ScheduledJob job = it.value();
        if (!job.enabled)
            continue;
        if (job.nextFireAtUtc.isValid() && job.nextFireAtUtc > nowUtc)
            continue;

        if (!job.nextFireAtUtc.isValid()) {
            if (isOnceScheduleType(job.scheduleType)) {
                if (job.consumedAtUtc.isValid() || !job.runAtUtc.isValid())
                    continue;
                job.nextFireAtUtc = job.runAtUtc;
            } else {
                job.nextFireAtUtc = nextFireTime(job.cronExpr, nowUtc, job.timezone);
            }
        }
        if (!job.nextFireAtUtc.isValid() || job.nextFireAtUtc > nowUtc)
            continue;

        job.lastFireAtUtc = nowUtc;
        if (isOnceScheduleType(job.scheduleType)) {
            job.consumedAtUtc = nowUtc;
            job.nextFireAtUtc = QDateTime();
        } else {
            job.nextFireAtUtc = nextFireTime(job.cronExpr, nowUtc, job.timezone);
        }
        it.value() = job;
        firedJobs.append(FiredJobRecord {
            job.jobId,
            job.name.trimmed().isEmpty() ? QStringLiteral("scheduled-job") : job.name.trimmed()
        });
        changed = true;
    }

    if (changed)
        saveJobs();

    for (const FiredJobRecord& fired : firedJobs)
        emit jobFired(fired.jobId, fired.jobName);
}

QDateTime SchedulerService::nextFireTime(const QString& cronExpr,
                                         const QDateTime& afterUtc,
                                         const QString& timezone) const
{
    return computeNextFireTime(cronExpr, afterUtc, timezone);
}

bool SchedulerService::saveJobs() const
{
    if (!m_persistence)
        return false;

    QJsonArray arr;
    const QList<ScheduledJob> jobs = m_jobs.values();
    for (const ScheduledJob& job : jobs)
        arr.append(jobToJson(job));

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 2);
    root.insert(QStringLiteral("jobs"), arr);
    return m_persistence->writeJsonObject(m_persistence->scheduledJobsPath(), root);
}

bool SchedulerService::loadJobs()
{
    m_jobs.clear();
    if (!m_persistence)
        return false;

    bool ok = false;
    const QString path = m_persistence->scheduledJobsPath();
    QJsonObject root = m_persistence->readJsonObject(path, &ok);
    if (!ok || root.isEmpty()) {
        root.insert(QStringLiteral("schemaVersion"), 2);
        root.insert(QStringLiteral("jobs"), QJsonArray());
        m_persistence->writeJsonObject(path, root);
        return true;
    }

    const QJsonArray arr = root.value(QStringLiteral("jobs")).toArray();
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    bool changed = false;
    for (const QJsonValue& value : arr) {
        if (!value.isObject())
            continue;
        ScheduledJob job = jobFromJson(value.toObject());
        QString error;
        if (!prepareJobForStorage(&job, nowUtc, true, &error))
            continue;
        if (isOnceScheduleType(job.scheduleType) && job.consumedAtUtc.isValid()) {
            changed = true;
            continue;
        }
        if (job.enabled) {
            if (isOnceScheduleType(job.scheduleType)) {
                job.nextFireAtUtc = job.runAtUtc;
            } else if (!job.nextFireAtUtc.isValid() || job.nextFireAtUtc <= nowUtc) {
                job.nextFireAtUtc = nextFireTime(job.cronExpr, nowUtc, job.timezone);
            }
        } else {
            job.nextFireAtUtc = QDateTime();
        }
        m_jobs.insert(job.jobId, job);
    }

    if (changed)
        saveJobs();
    return true;
}

bool SchedulerService::matchCronField(const QString& expr, int value, int minValue, int maxValue)
{
    return matchCronFieldImpl(expr, value, minValue, maxValue);
}
