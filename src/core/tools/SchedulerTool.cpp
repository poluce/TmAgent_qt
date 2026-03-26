#include "SchedulerTool.h"
#include "ToolSchemaSupport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimeZone>
#include <algorithm>

namespace {

SchedulerTool::Dependencies& schedulerDependencies()
{
    static SchedulerTool::Dependencies deps;
    return deps;
}

QString currentAgentIdFromArgs(const QJsonObject& args)
{
    return args.value(QStringLiteral("_agent_id")).toString().trimmed();
}

QString cronExpectedFormat()
{
    return QStringLiteral("分钟 小时 日 月 周");
}

QString cronRetryRule()
{
    return QStringLiteral("若本轮重试，只修正 cron_expr，其余参数保持不变");
}

bool cronPartLooksValid(const QString& part)
{
    static const QRegularExpression kAllowed(QStringLiteral("^[0-9\\*/,\\-]+$"));
    const QString trimmed = part.trimmed();
    return !trimmed.isEmpty() && kAllowed.match(trimmed).hasMatch();
}

QString suggestConcatenatedMinuteHourCronExpr(const QString& cronExpr)
{
    const QStringList parts = cronExpr.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 4)
        return QString();
    if (parts.at(1) != QLatin1String("*")
        || parts.at(2) != QLatin1String("*")
        || parts.at(3) != QLatin1String("*")) {
        return QString();
    }

    const QString compact = parts.at(0).trimmed();
    static const QRegularExpression kCompactHourMinute(QStringLiteral("^[0-9]{4}$"));
    if (!kCompactHourMinute.match(compact).hasMatch())
        return QString();

    bool minuteOk = false;
    bool hourOk = false;
    const int minute = compact.left(2).toInt(&minuteOk);
    const int hour = compact.mid(2, 2).toInt(&hourOk);
    if (!minuteOk || !hourOk || minute < 0 || minute > 59 || hour < 0 || hour > 23)
        return QString();

    return QStringLiteral("%1 %2 * * *").arg(compact.left(2), compact.mid(2, 2));
}

QString jsonValueToSingleLine(const QJsonValue& value)
{
    if (value.isString())
        return value.toString().trimmed();
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 16);
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return QString();
}

QString structuredErrorBlock(const QJsonObject& data)
{
    if (data.isEmpty())
        return QString();

    static const QStringList orderedKeys = {
        QStringLiteral("failing_field"),
        QStringLiteral("bad_value"),
        QStringLiteral("expected_format"),
        QStringLiteral("example_14_31"),
        QStringLiteral("example_14_35"),
        QStringLiteral("diagnosis"),
        QStringLiteral("suggested_value"),
        QStringLiteral("retry_rule"),
        QStringLiteral("do_not_repeat")
    };

    QStringList lines;
    for (const QString& key : orderedKeys) {
        if (!data.contains(key))
            continue;
        const QString value = jsonValueToSingleLine(data.value(key));
        if (!value.isEmpty())
            lines << QStringLiteral("%1: %2").arg(key, value);
    }

    QStringList remainingKeys = data.keys();
    for (const QString& key : orderedKeys)
        remainingKeys.removeAll(key);
    std::sort(remainingKeys.begin(), remainingKeys.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for (const QString& key : remainingKeys) {
        const QString value = jsonValueToSingleLine(data.value(key));
        if (!value.isEmpty())
            lines << QStringLiteral("%1: %2").arg(key, value);
    }

    return lines.join(QStringLiteral("\n"));
}

QJsonObject buildCronValidationData(const QString& cronExpr)
{
    QJsonObject data;
    data.insert(QStringLiteral("failing_field"), QStringLiteral("cron_expr"));

    const QString trimmedExpr = cronExpr.simplified();
    if (!trimmedExpr.isEmpty()) {
        data.insert(QStringLiteral("bad_value"), trimmedExpr);
        data.insert(QStringLiteral("do_not_repeat"), trimmedExpr);
    }

    data.insert(QStringLiteral("expected_format"), cronExpectedFormat());
    data.insert(QStringLiteral("example_14_31"), QStringLiteral("31 14 * * *"));
    data.insert(QStringLiteral("example_14_35"), QStringLiteral("35 14 * * *"));
    data.insert(QStringLiteral("retry_rule"), cronRetryRule());

    const QString suggestedValue = suggestConcatenatedMinuteHourCronExpr(trimmedExpr);
    if (!suggestedValue.isEmpty()) {
        data.insert(QStringLiteral("diagnosis"),
                    QStringLiteral("你可能把分钟和小时连写成了一个字段"));
        data.insert(QStringLiteral("suggested_value"), suggestedValue);
    }

    return data;
}

QJsonObject buildRunAtValidationData(const QString& runAtText)
{
    QJsonObject data;
    data.insert(QStringLiteral("failing_field"), QStringLiteral("run_at"));
    if (!runAtText.trimmed().isEmpty()) {
        data.insert(QStringLiteral("bad_value"), runAtText.trimmed());
        data.insert(QStringLiteral("do_not_repeat"), runAtText.trimmed());
    }
    data.insert(QStringLiteral("expected_format"),
                QStringLiteral("本地 ISO8601 时间，例如 2026-03-27T14:35:00"));
    return data;
}

QString joinJobBlocks(const QList<ScheduledJob>& jobs)
{
    QStringList lines;
    for (const ScheduledJob& job : jobs) {
        lines << QStringLiteral("- job_id: %1").arg(job.jobId);
        lines << QStringLiteral("  name: %1").arg(job.name);
        lines << QStringLiteral("  schedule_type: %1").arg(job.scheduleType);
        lines << QStringLiteral("  enabled: %1").arg(job.enabled ? QStringLiteral("true")
                                                                  : QStringLiteral("false"));
        if (job.scheduleType == QLatin1String("once")) {
            lines << QStringLiteral("  run_at_utc: %1")
                         .arg(job.runAtUtc.isValid()
                                  ? job.runAtUtc.toUTC().toString(Qt::ISODateWithMs)
                                  : QString());
            lines << QStringLiteral("  consumed_at_utc: %1")
                         .arg(job.consumedAtUtc.isValid()
                                  ? job.consumedAtUtc.toUTC().toString(Qt::ISODateWithMs)
                                  : QString());
        } else {
            lines << QStringLiteral("  cron_expr: %1").arg(job.cronExpr);
        }
        lines << QStringLiteral("  timezone: %1").arg(job.timezone);
        lines << QStringLiteral("  session_target: %1").arg(job.sessionTarget);
        lines << QStringLiteral("  next_fire_at_utc: %1")
                     .arg(job.nextFireAtUtc.isValid()
                              ? job.nextFireAtUtc.toUTC().toString(Qt::ISODateWithMs)
                              : QString());
    }
    return lines.join(QStringLiteral("\n"));
}

} // namespace

QList<Tool> SchedulerTool::toolSchemas()
{
    return {
        makeToolSchema(
            QStringLiteral("scheduler_list"),
            QStringLiteral("列出当前助手自己的定时任务。"),
            QJsonObject {
                { QStringLiteral("enabled_only"),
                  makePropertySchema(QStringLiteral("boolean"),
                                     QStringLiteral("可选。是否仅返回已启用任务，默认 false。")) }
            }),
        makeToolSchema(
            QStringLiteral("scheduler_create"),
            QStringLiteral("为当前助手创建一个新的定时任务。"),
            QJsonObject {
                { QStringLiteral("name"),
                  makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。任务名称。")) },
                { QStringLiteral("prompt"),
                  makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。到点后发送给助手执行的提示词。")) },
                { QStringLiteral("schedule_type"),
                  makePropertySchema(QStringLiteral("string"),
                                     QStringLiteral("可选。cron 或 once，默认 cron。cron 需要 cron_expr，once 需要 run_at。"),
                                     stringListEnum(QStringList { QStringLiteral("cron"),
                                                                  QStringLiteral("once") })) },
                { QStringLiteral("cron_expr"),
                  makePropertySchema(QStringLiteral("string"),
                                     QStringLiteral("当 schedule_type=cron 时必填。5 段 Cron 表达式，格式：分钟 小时 日 月 周。例如 14:31 -> 31 14 * * *，14:35 -> 35 14 * * *。")) },
                { QStringLiteral("run_at"),
                  makePropertySchema(QStringLiteral("string"),
                                     QStringLiteral("当 schedule_type=once 时必填。本地 ISO8601 时间，例如 2026-03-27T14:35:00；按 timezone 解释。")) },
                { QStringLiteral("timezone"),
                  makePropertySchema(QStringLiteral("string"), QStringLiteral("可选。时区，默认系统时区。")) },
                { QStringLiteral("session_target"),
                  makePropertySchema(QStringLiteral("string"),
                                     QStringLiteral("可选。main 或 isolated，默认 main。"),
                                     stringListEnum(QStringList { QStringLiteral("main"),
                                                                  QStringLiteral("isolated") })) },
                { QStringLiteral("enabled"),
                  makePropertySchema(QStringLiteral("boolean"), QStringLiteral("可选。是否启用，默认 true。")) }
            },
            QStringList { QStringLiteral("name"), QStringLiteral("prompt") }),
        makeToolSchema(
            QStringLiteral("scheduler_update"),
            QStringLiteral("更新当前助手已有的定时任务。"),
            QJsonObject {
                { QStringLiteral("job_id"),
                  makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。任务 ID。")) },
                { QStringLiteral("name"),
                  makePropertySchema(QStringLiteral("string"), QStringLiteral("可选。新任务名称。")) },
                { QStringLiteral("prompt"),
                  makePropertySchema(QStringLiteral("string"), QStringLiteral("可选。新的任务提示词。")) },
                { QStringLiteral("schedule_type"),
                  makePropertySchema(QStringLiteral("string"),
                                     QStringLiteral("可选。cron 或 once。切换为 cron 时需要有效 cron_expr，切换为 once 时需要 run_at。"),
                                     stringListEnum(QStringList { QStringLiteral("cron"),
                                                                  QStringLiteral("once") })) },
                { QStringLiteral("cron_expr"),
                  makePropertySchema(QStringLiteral("string"),
                                     QStringLiteral("可选。新的 5 段 Cron 表达式，格式：分钟 小时 日 月 周。例如 14:31 -> 31 14 * * *，14:35 -> 35 14 * * *。")) },
                { QStringLiteral("run_at"),
                  makePropertySchema(QStringLiteral("string"),
                                     QStringLiteral("可选。新的本地 ISO8601 时间，例如 2026-03-27T14:35:00；按 timezone 解释。")) },
                { QStringLiteral("timezone"),
                  makePropertySchema(QStringLiteral("string"), QStringLiteral("可选。新的时区。")) },
                { QStringLiteral("session_target"),
                  makePropertySchema(QStringLiteral("string"),
                                     QStringLiteral("可选。main 或 isolated。"),
                                     stringListEnum(QStringList { QStringLiteral("main"),
                                                                  QStringLiteral("isolated") })) },
                { QStringLiteral("enabled"),
                  makePropertySchema(QStringLiteral("boolean"), QStringLiteral("可选。是否启用。")) }
            },
            QStringList { QStringLiteral("job_id") }),
        makeToolSchema(
            QStringLiteral("scheduler_delete"),
            QStringLiteral("删除当前助手自己的定时任务。"),
            QJsonObject {
                { QStringLiteral("job_id"),
                  makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。任务 ID。")) }
            },
            QStringList { QStringLiteral("job_id") }),
        makeToolSchema(
            QStringLiteral("scheduler_run"),
            QStringLiteral("立即执行一次当前助手自己的定时任务。"),
            QJsonObject {
                { QStringLiteral("job_id"),
                  makePropertySchema(QStringLiteral("string"), QStringLiteral("必填。任务 ID。")) }
            },
            QStringList { QStringLiteral("job_id") })
    };
}

void SchedulerTool::setDependencies(const Dependencies& dependencies)
{
    schedulerDependencies() = dependencies;
}

ToolResult SchedulerTool::executeList(const QJsonObject& args)
{
    const QString agentId = currentAgentId(args);
    if (agentId.isEmpty())
        return missingAgentResult();

    if (!schedulerDependencies().allJobs)
        return ToolResult(QStringLiteral("错误: scheduler_list 当前不可用（未注册处理器）"),
                          QStringLiteral("列出定时任务失败"),
                          false);

    QList<ScheduledJob> jobs = schedulerDependencies().allJobs();
    jobs.erase(std::remove_if(jobs.begin(),
                              jobs.end(),
                              [&](const ScheduledJob& job) {
                                  if (job.agentId.trimmed() != agentId)
                                      return true;
                                  return args.value(QStringLiteral("enabled_only")).toBool(false)
                                      && !job.enabled;
                              }),
               jobs.end());

    std::sort(jobs.begin(), jobs.end(), [](const ScheduledJob& a, const ScheduledJob& b) {
        if (a.nextFireAtUtc.isValid() != b.nextFireAtUtc.isValid())
            return a.nextFireAtUtc.isValid();
        if (a.nextFireAtUtc != b.nextFireAtUtc)
            return a.nextFireAtUtc < b.nextFireAtUtc;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    QJsonArray jobsArray;
    for (const ScheduledJob& job : jobs)
        jobsArray.append(jobToData(job));

    QString raw = QStringLiteral("scheduler_list: 已列出定时任务\nagent_id: %1\ncount: %2")
                      .arg(agentId, QString::number(jobs.size()));
    if (!jobs.isEmpty())
        raw += QStringLiteral("\n\n") + joinJobBlocks(jobs);

    QJsonObject data;
    data.insert(QStringLiteral("agent_id"), agentId);
    data.insert(QStringLiteral("count"), jobs.size());
    data.insert(QStringLiteral("jobs"), jobsArray);
    return ToolResult(raw,
                      jobs.isEmpty() ? QStringLiteral("当前没有定时任务")
                                     : QStringLiteral("已列出 %1 个定时任务").arg(jobs.size()),
                      true,
                      data);
}

ToolResult SchedulerTool::executeCreate(const QJsonObject& args)
{
    const QString agentId = currentAgentId(args);
    if (agentId.isEmpty())
        return missingAgentResult();
    if (!schedulerDependencies().addJob || !schedulerDependencies().jobById)
        return ToolResult(QStringLiteral("错误: scheduler_create 当前不可用（未注册处理器）"),
                          QStringLiteral("创建定时任务失败"),
                          false);

    const QString name = args.value(QStringLiteral("name")).toString().trimmed();
    const QString prompt = args.value(QStringLiteral("prompt")).toString().trimmed();
    const QString scheduleType =
        normalizeScheduleType(args.value(QStringLiteral("schedule_type")).toString());
    const QString cronExpr = args.value(QStringLiteral("cron_expr")).toString().simplified();
    const QString runAtText = args.value(QStringLiteral("run_at")).toString().trimmed();
    const QString timezone = normalizeTimezone(args.value(QStringLiteral("timezone")).toString());
    const QString sessionTarget =
        normalizeSessionTarget(args.value(QStringLiteral("session_target")).toString());

    if (name.isEmpty())
        return invalidArgumentResult(QStringLiteral("scheduler_create"), QStringLiteral("name 不能为空"));
    if (prompt.isEmpty())
        return invalidArgumentResult(QStringLiteral("scheduler_create"), QStringLiteral("prompt 不能为空"));

    QString validationError;
    if (!validateScheduleType(scheduleType, &validationError))
        return invalidArgumentResult(QStringLiteral("scheduler_create"), validationError);
    if (!validateSessionTarget(sessionTarget, &validationError))
        return invalidArgumentResult(QStringLiteral("scheduler_create"), validationError);
    if (!cronExpr.isEmpty() && !runAtText.isEmpty()) {
        return invalidArgumentResult(QStringLiteral("scheduler_create"),
                                     QStringLiteral("cron_expr 和 run_at 不能同时提供"));
    }

    ScheduledJob job;
    job.name = name;
    job.agentId = agentId;
    job.prompt = prompt;
    job.scheduleType = scheduleType;
    job.timezone = timezone;
    job.sessionTarget = sessionTarget;
    job.enabled = args.contains(QStringLiteral("enabled"))
        ? args.value(QStringLiteral("enabled")).toBool(true)
        : true;

    if (scheduleType == QLatin1String("once")) {
        if (runAtText.isEmpty()) {
            return invalidArgumentResult(QStringLiteral("scheduler_create"),
                                         QStringLiteral("schedule_type=once 时必须提供 run_at"),
                                         buildRunAtValidationData(runAtText));
        }
        if (!cronExpr.isEmpty()) {
            return invalidArgumentResult(QStringLiteral("scheduler_create"),
                                         QStringLiteral("schedule_type=once 时不能提供 cron_expr"));
        }
        if (!parseRunAt(runAtText, timezone, &job.runAtUtc, &validationError)) {
            return invalidArgumentResult(QStringLiteral("scheduler_create"),
                                         validationError,
                                         buildRunAtValidationData(runAtText));
        }
    } else {
        if (cronExpr.isEmpty()) {
            return invalidArgumentResult(QStringLiteral("scheduler_create"),
                                         QStringLiteral("schedule_type=cron 时必须提供 cron_expr"),
                                         buildCronValidationData(cronExpr));
        }
        if (!runAtText.isEmpty()) {
            return invalidArgumentResult(QStringLiteral("scheduler_create"),
                                         QStringLiteral("schedule_type=cron 时不能提供 run_at"));
        }
        if (!validateCronExpr(cronExpr, &validationError)) {
            return invalidArgumentResult(QStringLiteral("scheduler_create"),
                                         validationError,
                                         buildCronValidationData(cronExpr));
        }
        job.cronExpr = cronExpr;
    }

    const QString jobId = schedulerDependencies().addJob(job).trimmed();
    if (jobId.isEmpty())
        return ToolResult(QStringLiteral("错误: scheduler_create 失败，底层未返回 job_id"),
                          QStringLiteral("创建定时任务失败"),
                          false);

    ScheduledJob stored;
    if (!schedulerDependencies().jobById(jobId, &stored)) {
        if (schedulerDependencies().removeJob)
            schedulerDependencies().removeJob(jobId);
        return ToolResult(QStringLiteral("错误: scheduler_create 失败，创建后无法读取任务"),
                          QStringLiteral("创建定时任务失败"),
                          false);
    }
    if (stored.enabled && !stored.nextFireAtUtc.isValid()) {
        if (schedulerDependencies().removeJob)
            schedulerDependencies().removeJob(jobId);
        return invalidArgumentResult(
            QStringLiteral("scheduler_create"),
            stored.scheduleType == QLatin1String("once")
                ? QStringLiteral("run_at 非法或无法计算执行时间")
                : QStringLiteral("cron_expr 非法或无法计算下一次触发时间"));
    }

    const QJsonObject data = jobToData(stored);
    const QString raw = QStringLiteral("scheduler_create: 已创建定时任务\n%1").arg(jobToRawBlock(stored));
    return ToolResult(raw,
                      QStringLiteral("已创建定时任务“%1”").arg(stored.name),
                      true,
                      data);
}

ToolResult SchedulerTool::executeUpdate(const QJsonObject& args)
{
    const QString agentId = currentAgentId(args);
    if (agentId.isEmpty())
        return missingAgentResult();
    if (!schedulerDependencies().jobById || !schedulerDependencies().updateJob)
        return ToolResult(QStringLiteral("错误: scheduler_update 当前不可用（未注册处理器）"),
                          QStringLiteral("更新定时任务失败"),
                          false);

    const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
    if (jobId.isEmpty())
        return invalidArgumentResult(QStringLiteral("scheduler_update"), QStringLiteral("job_id 不能为空"));

    ScheduledJob original;
    if (!schedulerDependencies().jobById(jobId, &original))
        return notFoundResult(QStringLiteral("scheduler_update"), jobId);
    if (original.agentId.trimmed() != agentId)
        return permissionDeniedResult(jobId);

    ScheduledJob updated = original;
    QStringList updatedFields;
    QString validationError;
    if (args.contains(QStringLiteral("name"))) {
        updated.name = args.value(QStringLiteral("name")).toString().trimmed();
        if (updated.name.isEmpty())
            return invalidArgumentResult(QStringLiteral("scheduler_update"), QStringLiteral("name 不能为空"));
        updatedFields.append(QStringLiteral("name"));
    }
    if (args.contains(QStringLiteral("prompt"))) {
        updated.prompt = args.value(QStringLiteral("prompt")).toString().trimmed();
        if (updated.prompt.isEmpty())
            return invalidArgumentResult(QStringLiteral("scheduler_update"), QStringLiteral("prompt 不能为空"));
        updatedFields.append(QStringLiteral("prompt"));
    }
    if (args.contains(QStringLiteral("timezone"))) {
        updated.timezone = normalizeTimezone(args.value(QStringLiteral("timezone")).toString());
        updatedFields.append(QStringLiteral("timezone"));
    }
    if (args.contains(QStringLiteral("session_target"))) {
        updated.sessionTarget = normalizeSessionTarget(args.value(QStringLiteral("session_target")).toString());
        if (!validateSessionTarget(updated.sessionTarget, &validationError))
            return invalidArgumentResult(QStringLiteral("scheduler_update"), validationError);
        updatedFields.append(QStringLiteral("session_target"));
    }
    if (args.contains(QStringLiteral("enabled"))) {
        updated.enabled = args.value(QStringLiteral("enabled")).toBool(true);
        updatedFields.append(QStringLiteral("enabled"));
    }
    if (args.contains(QStringLiteral("schedule_type"))) {
        updated.scheduleType =
            normalizeScheduleType(args.value(QStringLiteral("schedule_type")).toString());
        if (!validateScheduleType(updated.scheduleType, &validationError))
            return invalidArgumentResult(QStringLiteral("scheduler_update"), validationError);
        updatedFields.append(QStringLiteral("schedule_type"));
    }

    const QString cronExpr = args.value(QStringLiteral("cron_expr")).toString().simplified();
    const QString runAtText = args.value(QStringLiteral("run_at")).toString().trimmed();
    if (!cronExpr.isEmpty() && !runAtText.isEmpty()) {
        return invalidArgumentResult(QStringLiteral("scheduler_update"),
                                     QStringLiteral("cron_expr 和 run_at 不能同时提供"));
    }

    if (args.contains(QStringLiteral("cron_expr")))
        updatedFields.append(QStringLiteral("cron_expr"));
    if (args.contains(QStringLiteral("run_at")))
        updatedFields.append(QStringLiteral("run_at"));

    if (updated.scheduleType == QLatin1String("once")) {
        if (args.contains(QStringLiteral("cron_expr")) && !cronExpr.isEmpty()) {
            return invalidArgumentResult(QStringLiteral("scheduler_update"),
                                         QStringLiteral("schedule_type=once 时不能提供 cron_expr"));
        }
        if (args.contains(QStringLiteral("run_at"))) {
            if (!parseRunAt(runAtText, updated.timezone, &updated.runAtUtc, &validationError)) {
                return invalidArgumentResult(QStringLiteral("scheduler_update"),
                                             validationError,
                                             buildRunAtValidationData(runAtText));
            }
        }
        if (!updated.runAtUtc.isValid()) {
            return invalidArgumentResult(QStringLiteral("scheduler_update"),
                                         QStringLiteral("schedule_type=once 时必须提供有效的 run_at"),
                                         buildRunAtValidationData(runAtText));
        }
        updated.cronExpr.clear();
        updated.consumedAtUtc = QDateTime();
    } else {
        if (args.contains(QStringLiteral("run_at")) && !runAtText.isEmpty()) {
            return invalidArgumentResult(QStringLiteral("scheduler_update"),
                                         QStringLiteral("schedule_type=cron 时不能提供 run_at"));
        }
        if (args.contains(QStringLiteral("cron_expr"))) {
            updated.cronExpr = cronExpr;
        }
        if (!validateCronExpr(updated.cronExpr, &validationError)) {
            return invalidArgumentResult(QStringLiteral("scheduler_update"),
                                         validationError,
                                         buildCronValidationData(updated.cronExpr));
        }
        updated.runAtUtc = QDateTime();
        updated.consumedAtUtc = QDateTime();
    }

    if (updatedFields.isEmpty())
        return invalidArgumentResult(QStringLiteral("scheduler_update"), QStringLiteral("未提供任何可更新字段"));

    if (!schedulerDependencies().updateJob(jobId, updated))
        return ToolResult(QStringLiteral("错误: scheduler_update 失败，底层更新返回 false"),
                          QStringLiteral("更新定时任务失败"),
                          false);

    ScheduledJob stored;
    if (!schedulerDependencies().jobById(jobId, &stored))
        return ToolResult(QStringLiteral("错误: scheduler_update 失败，更新后无法读取任务"),
                          QStringLiteral("更新定时任务失败"),
                          false);

    if (stored.enabled && !stored.nextFireAtUtc.isValid()) {
        schedulerDependencies().updateJob(jobId, original);
        return invalidArgumentResult(
            QStringLiteral("scheduler_update"),
            stored.scheduleType == QLatin1String("once")
                ? QStringLiteral("run_at 非法或无法计算执行时间")
                : QStringLiteral("cron_expr 非法或无法计算下一次触发时间"));
    }

    QJsonObject data = jobToData(stored);
    data.insert(QStringLiteral("updated_fields"), QJsonArray::fromStringList(updatedFields));
    const QString raw = QStringLiteral("scheduler_update: 已更新定时任务\n%1\nupdated_fields: %2")
                            .arg(jobToRawBlock(stored), updatedFields.join(QStringLiteral(", ")));
    return ToolResult(raw,
                      QStringLiteral("已更新定时任务“%1”").arg(stored.name),
                      true,
                      data);
}

ToolResult SchedulerTool::executeDelete(const QJsonObject& args)
{
    const QString agentId = currentAgentId(args);
    if (agentId.isEmpty())
        return missingAgentResult();
    if (!schedulerDependencies().jobById || !schedulerDependencies().removeJob)
        return ToolResult(QStringLiteral("错误: scheduler_delete 当前不可用（未注册处理器）"),
                          QStringLiteral("删除定时任务失败"),
                          false);

    const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
    if (jobId.isEmpty())
        return invalidArgumentResult(QStringLiteral("scheduler_delete"), QStringLiteral("job_id 不能为空"));

    ScheduledJob stored;
    if (!schedulerDependencies().jobById(jobId, &stored))
        return notFoundResult(QStringLiteral("scheduler_delete"), jobId);
    if (stored.agentId.trimmed() != agentId)
        return permissionDeniedResult(jobId);
    if (!schedulerDependencies().removeJob(jobId))
        return ToolResult(QStringLiteral("错误: scheduler_delete 失败，底层删除返回 false"),
                          QStringLiteral("删除定时任务失败"),
                          false);

    QJsonObject data = jobToData(stored);
    data.insert(QStringLiteral("deleted"), true);
    const QString raw =
        QStringLiteral("scheduler_delete: 已删除定时任务\njob_id: %1\nname: %2\nagent_id: %3")
            .arg(stored.jobId, stored.name, stored.agentId);
    return ToolResult(raw,
                      QStringLiteral("已删除定时任务“%1”").arg(stored.name),
                      true,
                      data);
}

ToolResult SchedulerTool::executeRun(const QJsonObject& args)
{
    const QString agentId = currentAgentId(args);
    if (agentId.isEmpty())
        return missingAgentResult();
    if (!schedulerDependencies().jobById || !schedulerDependencies().triggerJob)
        return ToolResult(QStringLiteral("错误: scheduler_run 当前不可用（未注册处理器）"),
                          QStringLiteral("触发定时任务失败"),
                          false);

    const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
    if (jobId.isEmpty())
        return invalidArgumentResult(QStringLiteral("scheduler_run"), QStringLiteral("job_id 不能为空"));

    ScheduledJob stored;
    if (!schedulerDependencies().jobById(jobId, &stored))
        return notFoundResult(QStringLiteral("scheduler_run"), jobId);
    if (stored.agentId.trimmed() != agentId)
        return permissionDeniedResult(jobId);
    if (!stored.enabled) {
        return ToolResult(QStringLiteral("错误: scheduler_run 失败，任务已禁用"),
                          QStringLiteral("触发定时任务失败"),
                          false);
    }
    if (stored.scheduleType == QLatin1String("once") && stored.consumedAtUtc.isValid()) {
        return ToolResult(QStringLiteral("错误: scheduler_run 失败，单次任务已触发，等待本次执行完成"),
                          QStringLiteral("触发定时任务失败"),
                          false);
    }

    schedulerDependencies().triggerJob(jobId);
    QJsonObject data = jobToData(stored);
    data.insert(QStringLiteral("triggered"), true);
    const QString raw = QStringLiteral("scheduler_run: 已触发定时任务\n%1").arg(jobToRawBlock(stored));
    return ToolResult(raw,
                      QStringLiteral("已触发定时任务“%1”").arg(stored.name),
                      true,
                      data);
}

QString SchedulerTool::currentAgentId(const QJsonObject& args)
{
    return currentAgentIdFromArgs(args);
}

bool SchedulerTool::validateCronExpr(const QString& cronExpr, QString* error)
{
    const QStringList parts = cronExpr.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 5) {
        if (error)
            *error = QStringLiteral("cron_expr 必须是 5 段 Cron 表达式");
        return false;
    }
    for (const QString& part : parts) {
        if (!cronPartLooksValid(part)) {
            if (error)
                *error = QStringLiteral("cron_expr 包含非法字符");
            return false;
        }
    }
    if (error)
        error->clear();
    return true;
}

bool SchedulerTool::validateScheduleType(const QString& scheduleType, QString* error)
{
    const QString normalized = normalizeScheduleType(scheduleType);
    if (normalized == QLatin1String("cron") || normalized == QLatin1String("once")) {
        if (error)
            error->clear();
        return true;
    }
    if (error)
        *error = QStringLiteral("schedule_type 仅允许 cron 或 once");
    return false;
}

bool SchedulerTool::validateSessionTarget(const QString& sessionTarget, QString* error)
{
    const QString normalized = normalizeSessionTarget(sessionTarget);
    if (normalized == QLatin1String("main") || normalized == QLatin1String("isolated")) {
        if (error)
            error->clear();
        return true;
    }
    if (error)
        *error = QStringLiteral("session_target 仅允许 main 或 isolated");
    return false;
}

bool SchedulerTool::parseRunAt(const QString& runAtText,
                               const QString& timezone,
                               QDateTime* runAtUtc,
                               QString* error)
{
    if (runAtUtc)
        *runAtUtc = QDateTime();

    const QString text = runAtText.trimmed();
    if (text.isEmpty()) {
        if (error)
            *error = QStringLiteral("run_at 不能为空");
        return false;
    }

    static const QRegularExpression kOffsetSuffix(QStringLiteral("(Z|[\\+\\-]\\d{2}:\\d{2})$"),
                                                  QRegularExpression::CaseInsensitiveOption);
    if (kOffsetSuffix.match(text).hasMatch()) {
        if (error)
            *error = QStringLiteral("run_at 需要本地 ISO8601 时间，不能携带时区偏移");
        return false;
    }

    static const QStringList formats = {
        QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
        QStringLiteral("yyyy-MM-ddTHH:mm")
    };

    QDateTime parsed;
    for (const QString& format : formats) {
        parsed = QDateTime::fromString(text, format);
        if (parsed.isValid())
            break;
    }
    if (!parsed.isValid()) {
        if (error)
            *error = QStringLiteral("run_at 必须是本地 ISO8601 时间，例如 2026-03-27T14:35:00");
        return false;
    }

    const QTimeZone zone(normalizeTimezone(timezone).toUtf8());
    const QTimeZone resolvedZone = zone.isValid() ? zone : QTimeZone::systemTimeZone();
    const QDateTime zoned(parsed.date(), parsed.time(), resolvedZone);
    if (!zoned.isValid()) {
        if (error)
            *error = QStringLiteral("run_at 无法按 timezone 解释");
        return false;
    }
    if (zoned.toUTC() <= QDateTime::currentDateTimeUtc()) {
        if (error)
            *error = QStringLiteral("run_at 必须晚于当前时间");
        return false;
    }

    if (runAtUtc)
        *runAtUtc = zoned.toUTC();
    if (error)
        error->clear();
    return true;
}

QString SchedulerTool::normalizeScheduleType(const QString& scheduleType)
{
    const QString normalized = scheduleType.trimmed().toLower();
    if (normalized.isEmpty())
        return QStringLiteral("cron");
    return normalized;
}

QString SchedulerTool::normalizeSessionTarget(const QString& sessionTarget)
{
    const QString normalized = sessionTarget.trimmed().toLower();
    if (normalized == QLatin1String("isolated"))
        return QStringLiteral("isolated");
    if (normalized.isEmpty())
        return QStringLiteral("main");
    return normalized;
}

QString SchedulerTool::normalizeTimezone(const QString& timezone)
{
    const QString trimmed = timezone.trimmed();
    return trimmed.isEmpty() ? QString::fromUtf8(QTimeZone::systemTimeZoneId()) : trimmed;
}

QJsonObject SchedulerTool::jobToData(const ScheduledJob& job)
{
    QJsonObject data;
    data.insert(QStringLiteral("job_id"), job.jobId);
    data.insert(QStringLiteral("name"), job.name);
    data.insert(QStringLiteral("agent_id"), job.agentId);
    data.insert(QStringLiteral("schedule_type"), job.scheduleType);
    data.insert(QStringLiteral("enabled"), job.enabled);
    data.insert(QStringLiteral("cron_expr"), job.cronExpr);
    if (job.runAtUtc.isValid())
        data.insert(QStringLiteral("run_at_utc"), job.runAtUtc.toUTC().toString(Qt::ISODateWithMs));
    if (job.consumedAtUtc.isValid()) {
        data.insert(QStringLiteral("consumed_at_utc"),
                    job.consumedAtUtc.toUTC().toString(Qt::ISODateWithMs));
    }
    data.insert(QStringLiteral("timezone"), job.timezone);
    data.insert(QStringLiteral("session_target"), job.sessionTarget);
    if (job.nextFireAtUtc.isValid()) {
        data.insert(QStringLiteral("next_fire_at_utc"),
                    job.nextFireAtUtc.toUTC().toString(Qt::ISODateWithMs));
    }
    return data;
}

QString SchedulerTool::jobToRawBlock(const ScheduledJob& job)
{
    QStringList lines;
    lines << QStringLiteral("job_id: %1").arg(job.jobId);
    lines << QStringLiteral("name: %1").arg(job.name);
    lines << QStringLiteral("agent_id: %1").arg(job.agentId);
    lines << QStringLiteral("schedule_type: %1").arg(job.scheduleType);
    lines << QStringLiteral("enabled: %1").arg(job.enabled ? QStringLiteral("true")
                                                           : QStringLiteral("false"));
    if (job.scheduleType == QLatin1String("once")) {
        lines << QStringLiteral("run_at_utc: %1")
                     .arg(job.runAtUtc.isValid()
                              ? job.runAtUtc.toUTC().toString(Qt::ISODateWithMs)
                              : QString());
        lines << QStringLiteral("consumed_at_utc: %1")
                     .arg(job.consumedAtUtc.isValid()
                              ? job.consumedAtUtc.toUTC().toString(Qt::ISODateWithMs)
                              : QString());
    } else {
        lines << QStringLiteral("cron_expr: %1").arg(job.cronExpr);
    }
    lines << QStringLiteral("timezone: %1").arg(job.timezone);
    lines << QStringLiteral("session_target: %1").arg(job.sessionTarget);
    lines << QStringLiteral("next_fire_at_utc: %1")
                 .arg(job.nextFireAtUtc.isValid()
                          ? job.nextFireAtUtc.toUTC().toString(Qt::ISODateWithMs)
                          : QString());
    return lines.join(QStringLiteral("\n"));
}

ToolResult SchedulerTool::missingAgentResult()
{
    return ToolResult(QStringLiteral("错误: 缺少 _agent_id，无法确定当前助手"),
                      QStringLiteral("定时任务工具当前不可用"),
                      false);
}

ToolResult SchedulerTool::permissionDeniedResult(const QString& jobId)
{
    return ToolResult(QStringLiteral("错误: 无权操作任务 %1，该任务不属于当前助手").arg(jobId),
                      QStringLiteral("定时任务权限不足"),
                      false);
}

ToolResult SchedulerTool::notFoundResult(const QString& action, const QString& jobId)
{
    return ToolResult(QStringLiteral("错误: %1 失败，job_id 不存在: %2").arg(action, jobId),
                      QStringLiteral("定时任务不存在"),
                      false);
}

ToolResult SchedulerTool::invalidArgumentResult(const QString& action,
                                                const QString& detail,
                                                const QJsonObject& extraData)
{
    QString summary = QStringLiteral("定时任务操作失败");
    if (action == QLatin1String("scheduler_create"))
        summary = QStringLiteral("创建定时任务失败");
    else if (action == QLatin1String("scheduler_update"))
        summary = QStringLiteral("更新定时任务失败");
    else if (action == QLatin1String("scheduler_delete"))
        summary = QStringLiteral("删除定时任务失败");
    else if (action == QLatin1String("scheduler_run"))
        summary = QStringLiteral("触发定时任务失败");
    else if (action == QLatin1String("scheduler_list"))
        summary = QStringLiteral("列出定时任务失败");

    QString raw = QStringLiteral("错误: %1 失败，%2").arg(action, detail);
    const QString details = structuredErrorBlock(extraData);
    if (!details.isEmpty())
        raw += QStringLiteral("\n") + details;
    return ToolResult(raw, summary, false, extraData);
}
