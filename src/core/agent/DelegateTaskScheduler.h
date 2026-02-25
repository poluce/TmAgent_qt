#ifndef DELEGATETASKSCHEDULER_H
#define DELEGATETASKSCHEDULER_H

#include "core/agent/ToolTypes.h"
#include "llm/LLMTypes.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QReadWriteLock>
#include <QSharedPointer>
#include <QStringList>
#include <functional>

class ToolDispatcher;

class DelegateTaskScheduler {
public:
    struct Request {
        QString delegateToolName;
        QString task;
        QString rolePrompt;
        bool restrictDelegation = false;
        int expectedTimeoutMs = 120000;
        int maxResponseChars = 4000;
        QStringList inheritedAllowedTools;

        LLMConfig parentConfig;
        bool useOverrideConfig = false;
        LLMConfig overrideConfig;
        ToolDispatcher* toolDispatcher = nullptr;

        std::function<void(const ToolExecutionEvent&)> onChildToolEvent;
        std::function<void(const QString&)> onChildStreamData;
    };

    struct Snapshot {
        QString taskId;
        QString status;
        QString failureReason;
        QString summary;

        qint64 startedAtMs = 0;
        qint64 lastProgressAtMs = 0;
        qint64 finishedAtMs = 0;

        int expectedTimeoutMs = 0;
        int softTimeoutMs = 0;
        int hardTimeoutMs = 0;
        int stallNoProgressMs = 0;

        QString childAgentId;
        QString childModel;
        QString childRequestId;
        QString childTraceId;
        QString childFinishReason;
        QString childError;

        int childToolStartedCount = 0;
        int childToolProgressCount = 0;
        int childToolCompletedCount = 0;
        int childToolSuccessCount = 0;
        int childToolFailureCount = 0;
        int childStreamChunkCount = 0;
        int childStreamChars = 0;
        int childTimelineDropped = 0;
        QStringList childTools;
        QJsonArray childTimeline;
    };

    struct Result {
        bool success = false;
        QString rawResult;
        QString userSummary;
        QJsonObject data;
    };

    struct JobInfo {
        QString jobId;
        QString ownerAgentId;
        QString status;
        QString summary;
        QString failureReason;
        QString task;
        QString result;
        qint64 createdAtMs = 0;
        qint64 startedAtMs = 0;
        qint64 lastProgressAtMs = 0;
        qint64 finishedAtMs = 0;
        int expectedTimeoutMs = 0;
        int hardTimeoutMs = 0;
        int stallNoProgressMs = 0;
        int childToolStartedCount = 0;
        int childToolProgressCount = 0;
        int childToolCompletedCount = 0;
        int childToolSuccessCount = 0;
        int childToolFailureCount = 0;
        int childStreamChunkCount = 0;
        int childStreamChars = 0;
        QStringList childTools;
    };

    static DelegateTaskScheduler* instance();

    Result executeSync(const Request& request);
    Result submitAsync(const Request& request, const QString& ownerAgentId);
    bool queryJob(const QString& jobId, const QString& ownerAgentId, JobInfo* info) const;
    QList<JobInfo> listJobs(const QString& ownerAgentId, bool activeOnly, int limit) const;
    bool cancelJob(const QString& jobId, const QString& ownerAgentId, QString* error = nullptr);
    Snapshot snapshot(const QString& taskId) const;
    QList<Snapshot> activeTasks() const;

private:
    DelegateTaskScheduler() = default;
    DelegateTaskScheduler(const DelegateTaskScheduler&) = delete;
    DelegateTaskScheduler& operator=(const DelegateTaskScheduler&) = delete;

    void upsertSnapshot(const Snapshot& snapshot);
    void pruneSnapshotsLocked();
    static QJsonObject collectChildRunData(const QJsonArray& ioHistory);
    static QString summarizeTimelineEntry(const ToolExecutionEvent& event);
    static int calcHardTimeoutMs(int softTimeoutMs);
    static int calcStallNoProgressMs(int softTimeoutMs);
    static bool isChildGuarded(const QString& childFinishReason, const QString& finalResult);

    struct AsyncJobRuntime;
    static JobInfo toJobInfo(const QSharedPointer<AsyncJobRuntime>& runtime);
    void pruneJobsLocked();
    void settleAsyncJob(const QSharedPointer<AsyncJobRuntime>& runtime, bool success, const QString& result, const QString& failureReason);

    mutable QReadWriteLock m_lock;
    QHash<QString, Snapshot> m_snapshots;
    QStringList m_snapshotOrder;
    QHash<QString, QSharedPointer<AsyncJobRuntime>> m_asyncJobs;
    QStringList m_asyncJobOrder;
};

#endif // DELEGATETASKSCHEDULER_H
