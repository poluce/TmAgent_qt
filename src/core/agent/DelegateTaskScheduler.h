#ifndef DELEGATETASKSCHEDULER_H
#define DELEGATETASKSCHEDULER_H

#include "core/agent/ToolTypes.h"
#include "llm/LLMTypes.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QReadWriteLock>
#include <QSharedPointer>
#include <QStringList>
#include <functional>

class ToolDispatcher;

class DelegateTaskScheduler : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxConcurrentAsyncJobs = 8;
    struct Request {
        QString delegateToolName;
        QString task;
        QString rolePrompt;
        QString backend;
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
        QString backend;
        QString summary;
        QString failureReason;
        QString task;
        QString result;
        QString backendThreadId;
        QString backendTurnId;
        QString backendProgram;
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

    Result submitAsync(const Request& request, const QString& ownerAgentId);
    bool queryJob(const QString& jobId, const QString& ownerAgentId, JobInfo* info) const;
    QList<JobInfo> listJobs(const QString& ownerAgentId, bool activeOnly, int limit) const;
    bool cancelJob(const QString& jobId, const QString& ownerAgentId, QString* error = nullptr);

    QString formatActiveJobsContext(const QString& ownerAgentId) const;
    int activeJobCount() const;

signals:
    void jobSettled(const QString& jobId, const QString& ownerAgentId, bool success, const QString& result);
    void jobStatusChanged(const QString& jobId, const QString& ownerAgentId, const QString& newStatus, const QString& summary);

private:
    explicit DelegateTaskScheduler(QObject* parent = nullptr);
    Q_DISABLE_COPY(DelegateTaskScheduler)

    static QString summarizeTimelineEntry(const ToolExecutionEvent& event);
    static int calcHardTimeoutMs(int softTimeoutMs);
    static int calcStallNoProgressMs(int softTimeoutMs);

    struct AsyncJobRuntime;
    static JobInfo toJobInfo(const QSharedPointer<AsyncJobRuntime>& runtime);
    void pruneJobsLocked();
    void settleAsyncJob(const QSharedPointer<AsyncJobRuntime>& runtime, bool success, const QString& result, const QString& failureReason);

    mutable QReadWriteLock m_lock;
    QHash<QString, QSharedPointer<AsyncJobRuntime>> m_asyncJobs;
    QStringList m_asyncJobOrder;
};

#endif // DELEGATETASKSCHEDULER_H
