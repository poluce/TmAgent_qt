#include "SchedulerService.h"

#include "core/persistence/ChatPersistenceService.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>

namespace {
QString normalizeSessionTarget(const QString& value)
{
    const QString target = value.trimmed().toLower();
    if (target == QLatin1String("isolated"))
        return QStringLiteral("isolated");
    return QStringLiteral("main");
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

QJsonObject jobToJson(const ScheduledJob& job)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("jobId"), job.jobId);
    obj.insert(QStringLiteral("name"), job.name);
    obj.insert(QStringLiteral("agentId"), job.agentId);
    obj.insert(QStringLiteral("prompt"), job.prompt);
    obj.insert(QStringLiteral("cronExpr"), job.cronExpr);
    obj.insert(QStringLiteral("timezone"), job.timezone);
    obj.insert(QStringLiteral("sessionTarget"), job.sessionTarget);
    obj.insert(QStringLiteral("enabled"), job.enabled);
    if (job.nextFireAtUtc.isValid())
        obj.insert(QStringLiteral("nextFireAtUtc"), job.nextFireAtUtc.toString(Qt::ISODateWithMs));
    if (job.lastFireAtUtc.isValid())
        obj.insert(QStringLiteral("lastFireAtUtc"), job.lastFireAtUtc.toString(Qt::ISODateWithMs));
    return obj;
}

ScheduledJob jobFromJson(const QJsonObject& obj)
{
    ScheduledJob job;
    job.jobId = obj.value(QStringLiteral("jobId")).toString().trimmed();
    job.name = obj.value(QStringLiteral("name")).toString().trimmed();
    job.agentId = obj.value(QStringLiteral("agentId")).toString().trimmed();
    job.prompt = obj.value(QStringLiteral("prompt")).toString().trimmed();
    job.cronExpr = obj.value(QStringLiteral("cronExpr")).toString().simplified();
    job.timezone = obj.value(QStringLiteral("timezone")).toString().trimmed();
    job.sessionTarget = normalizeSessionTarget(obj.value(QStringLiteral("sessionTarget")).toString());
    job.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    job.nextFireAtUtc = QDateTime::fromString(
        obj.value(QStringLiteral("nextFireAtUtc")).toString().trimmed(),
        Qt::ISODateWithMs);
    if (!job.nextFireAtUtc.isValid())
        job.nextFireAtUtc = QDateTime::fromString(
            obj.value(QStringLiteral("nextFireAtUtc")).toString().trimmed(),
            Qt::ISODate);
    if (job.nextFireAtUtc.isValid())
        job.nextFireAtUtc = job.nextFireAtUtc.toUTC();
    job.lastFireAtUtc = QDateTime::fromString(
        obj.value(QStringLiteral("lastFireAtUtc")).toString().trimmed(),
        Qt::ISODateWithMs);
    if (!job.lastFireAtUtc.isValid())
        job.lastFireAtUtc = QDateTime::fromString(
            obj.value(QStringLiteral("lastFireAtUtc")).toString().trimmed(),
            Qt::ISODate);
    if (job.lastFireAtUtc.isValid())
        job.lastFireAtUtc = job.lastFireAtUtc.toUTC();
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
    job.jobId = job.jobId.trimmed();
    job.agentId = job.agentId.trimmed();
    job.prompt = job.prompt.trimmed();
    job.cronExpr = job.cronExpr.simplified();
    job.sessionTarget = normalizeSessionTarget(job.sessionTarget);
    if (job.timezone.trimmed().isEmpty())
        job.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    job.nextFireAtUtc = nextFireTime(job.cronExpr, QDateTime::currentDateTimeUtc(), job.timezone);
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
    job.agentId = job.agentId.trimmed();
    job.prompt = job.prompt.trimmed();
    job.cronExpr = job.cronExpr.simplified();
    job.sessionTarget = normalizeSessionTarget(job.sessionTarget);
    if (job.timezone.trimmed().isEmpty())
        job.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    if (job.enabled)
        job.nextFireAtUtc = nextFireTime(job.cronExpr, QDateTime::currentDateTimeUtc(), job.timezone);
    else
        job.nextFireAtUtc = QDateTime();
    m_jobs.insert(key, job);
    return saveJobs();
}

bool SchedulerService::enableJob(const QString& jobId, bool enabled)
{
    const QString key = jobId.trimmed();
    if (key.isEmpty() || !m_jobs.contains(key))
        return false;
    ScheduledJob job = m_jobs.value(key);
    job.enabled = enabled;
    if (enabled)
        job.nextFireAtUtc = nextFireTime(job.cronExpr, QDateTime::currentDateTimeUtc(), job.timezone);
    else
        job.nextFireAtUtc = QDateTime();
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
    const ScheduledJob job = m_jobs.value(key);
    if (!job.enabled) {
        emit jobFailed(key, QStringLiteral("job_disabled"));
        return;
    }
    emit jobFired(job.jobId, job.name.trimmed().isEmpty() ? QStringLiteral("scheduled-job") : job.name.trimmed());
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
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        ScheduledJob job = it.value();
        if (!job.enabled)
            continue;
        if (job.nextFireAtUtc.isValid() && job.nextFireAtUtc > nowUtc)
            continue;

        if (!job.nextFireAtUtc.isValid())
            job.nextFireAtUtc = nextFireTime(job.cronExpr, nowUtc, job.timezone);
        if (!job.nextFireAtUtc.isValid())
            continue;
        if (job.nextFireAtUtc > nowUtc)
            continue;

        emit jobFired(job.jobId, job.name.trimmed().isEmpty() ? QStringLiteral("scheduled-job") : job.name.trimmed());
        job.lastFireAtUtc = nowUtc;
        job.nextFireAtUtc = nextFireTime(job.cronExpr, nowUtc, job.timezone);
        it.value() = job;
        changed = true;
    }

    if (changed)
        saveJobs();
}

QDateTime SchedulerService::nextFireTime(const QString& cronExpr, const QDateTime& afterUtc, const QString& timezone) const
{
    const QStringList parts = splitCronExpr(cronExpr);
    if (parts.size() != 5)
        return QDateTime();

    const QTimeZone zone = resolveTimezone(timezone);
    QDateTime cursor = afterUtc.toTimeZone(zone);
    cursor.setTime(QTime(cursor.time().hour(), cursor.time().minute(), 0, 0));
    cursor = cursor.addSecs(60); // strictly after

    const int maxIterations = 60 * 24 * 366; // 1 year, minute granularity
    for (int i = 0; i < maxIterations; ++i) {
        const int minute = cursor.time().minute();
        const int hour = cursor.time().hour();
        const int day = cursor.date().day();
        const int month = cursor.date().month();
        int weekday = cursor.date().dayOfWeek(); // 1..7 (Mon..Sun)
        if (weekday == 7)
            weekday = 0; // cron: Sunday = 0 or 7

        const bool weekdayMatched = matchCronField(parts.at(4), weekday, 0, 7)
            || (weekday == 0 && matchCronField(parts.at(4), 7, 0, 7));

        if (matchCronField(parts.at(0), minute, 0, 59)
            && matchCronField(parts.at(1), hour, 0, 23)
            && matchCronField(parts.at(2), day, 1, 31)
            && matchCronField(parts.at(3), month, 1, 12)
            && weekdayMatched) {
            return cursor.toUTC();
        }
        cursor = cursor.addSecs(60);
    }
    return QDateTime();
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
    root.insert(QStringLiteral("schemaVersion"), 1);
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
        root.insert(QStringLiteral("schemaVersion"), 1);
        root.insert(QStringLiteral("jobs"), QJsonArray());
        m_persistence->writeJsonObject(path, root);
        return true;
    }

    const QJsonArray arr = root.value(QStringLiteral("jobs")).toArray();
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    for (const QJsonValue& value : arr) {
        if (!value.isObject())
            continue;
        ScheduledJob job = jobFromJson(value.toObject());
        if (job.jobId.trimmed().isEmpty() || job.agentId.trimmed().isEmpty()
            || job.prompt.trimmed().isEmpty() || job.cronExpr.trimmed().isEmpty()) {
            continue;
        }
        if (job.timezone.trimmed().isEmpty())
            job.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
        if (job.enabled && (!job.nextFireAtUtc.isValid() || job.nextFireAtUtc <= nowUtc))
            job.nextFireAtUtc = nextFireTime(job.cronExpr, nowUtc, job.timezone);
        m_jobs.insert(job.jobId, job);
    }
    return true;
}

bool SchedulerService::matchCronField(const QString& expr, int value, int minValue, int maxValue)
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
