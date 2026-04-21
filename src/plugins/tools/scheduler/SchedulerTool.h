#ifndef SCHEDULERTOOL_H
#define SCHEDULERTOOL_H

#include <tmagent/types/ToolTypes.h>
#include <tmagent/types/CommonTypes.h>
#include <QList>
#include <QString>
#include <functional>

using namespace TmAgent;

class SchedulerTool {
public:
    struct Dependencies {
        std::function<QList<ScheduledJob>()> allJobs;
        std::function<bool(const QString&, ScheduledJob*)> jobById;
        std::function<QString(const ScheduledJob&)> addJob;
        std::function<bool(const QString&, const ScheduledJob&)> updateJob;
        std::function<bool(const QString&)> removeJob;
        std::function<void(const QString&)> triggerJob;
    };

    static QList<Tool> toolSchemas();
    static void setDependencies(const Dependencies& dependencies);

    static ToolResult executeList(const QJsonObject& args);
    static ToolResult executeCreate(const QJsonObject& args);
    static ToolResult executeUpdate(const QJsonObject& args);
    static ToolResult executeDelete(const QJsonObject& args);
    static ToolResult executeRun(const QJsonObject& args);

private:
    static QString currentAgentId(const QJsonObject& args);
    static bool validateCronExpr(const QString& cronExpr, QString* error);
    static bool validateScheduleType(const QString& scheduleType, QString* error);
    static bool validateSessionTarget(const QString& sessionTarget, QString* error);
    static bool parseRunAt(const QString& runAtText,
                           const QString& timezone,
                           QDateTime* runAtUtc,
                           QString* error);
    static QString normalizeScheduleType(const QString& scheduleType);
    static QString normalizeSessionTarget(const QString& sessionTarget);
    static QString normalizeTimezone(const QString& timezone);
    static QJsonObject jobToData(const ScheduledJob& job);
    static QString jobToRawBlock(const ScheduledJob& job);
    static ToolResult missingAgentResult();
    static ToolResult permissionDeniedResult(const QString& jobId);
    static ToolResult notFoundResult(const QString& action, const QString& jobId);
    static ToolResult invalidArgumentResult(const QString& action,
                                            const QString& detail,
                                            const QJsonObject& extraData = QJsonObject());
};

#endif // SCHEDULERTOOL_H
