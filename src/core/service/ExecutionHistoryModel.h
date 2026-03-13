#ifndef EXECUTIONHISTORYMODEL_H
#define EXECUTIONHISTORYMODEL_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace ExecutionHistory {

inline constexpr int kSchemaVersion = 1;

enum class FilterMode {
    All = 0,
    FailuresOnly,
    ToolCallsOnly,
    EventsOnly,
    ActiveOnly
};

struct ToolActivity {
    QString name;
    QString stageLabel;
    QString statusLabel;
    QString statusTone;
    QString inputSummary;
    QString outputSummary;
    QString errorSummary;
};

struct Record {
    int rawIndex = -1;
    QJsonObject rawEntry;
    QJsonObject summaryLayer;
    QJsonObject eventFactsLayer;
    QJsonObject interactionFactsLayer;
    QJsonObject auditLayer;
    QString recordId;
    QString kindLabel;
    QString statusLabel;
    QString statusTone;
    QString listTitle;
    QString inputSummary;
    QString outputSummary;
    QString toolSummary;
    QString timeSummary;
    QString metaSummary;
    QString errorSummary;
    QString detailText;
    QString startedAtDisplay;
    QString finishedAtDisplay;
    QString durationDisplay;
    QVector<ToolActivity> toolActivities;
    bool isEvent = false;
    bool hasError = false;
    bool hasToolCalls = false;
    bool isActive = false;
};

QString filterModeText(FilterMode mode);
QJsonObject schemaDescriptor();
QVector<Record> buildRecords(const QJsonArray& history);
QVector<int> filterRecordIndexes(const QVector<Record>& records, FilterMode mode, int recentLimit);

} // namespace ExecutionHistory

#endif // EXECUTIONHISTORYMODEL_H
