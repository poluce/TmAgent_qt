#ifndef TURNMANAGER_H
#define TURNMANAGER_H

#include <QHash>
#include <QList>
#include <QJsonObject>
#include <QString>
#include <QStringList>

enum class TurnSource {
    User,
    TeammateReply,
    System
};

struct TurnTask {
    TurnSource source = TurnSource::User;
    QString requestTraceId;
    QString turnId;
    QString runId;
    QString actorIdentityId;
    qint64 enqueuedAtMs = 0;
    int mergedMessageCount = 1;
    QString clientMessageId;
    QString userContent;
    QString internalContent;
    QJsonObject internalPayload;
    QString assistantContent;
};

struct SessionPipeline {
    QList<TurnTask> queue;
    TurnTask activeTurn;
    bool hasActiveTurn = false;
    quint64 seq = 0;
    QString pendingDeltaLog;
    int pendingDeltaChunks = 0;
    qint64 pendingDeltaStartedAtMs = 0;
    qint64 lastDeltaFlushedAtMs = 0;
};

class TurnManager {
public:
    SessionPipeline& ensurePipeline(const QString& sessionId)
    {
        return m_pipelines[sessionId];
    }

    SessionPipeline* findPipeline(const QString& sessionId)
    {
        auto it = m_pipelines.find(sessionId);
        if (it == m_pipelines.end())
            return nullptr;
        return &it.value();
    }

    const SessionPipeline* findPipeline(const QString& sessionId) const
    {
        auto it = m_pipelines.constFind(sessionId);
        if (it == m_pipelines.constEnd())
            return nullptr;
        return &it.value();
    }

    void removePipeline(const QString& sessionId)
    {
        m_pipelines.remove(sessionId);
    }

    void clear()
    {
        m_pipelines.clear();
    }

    bool hasActiveTurn(const QString& sessionId) const
    {
        const SessionPipeline* pipeline = findPipeline(sessionId);
        return pipeline && pipeline->hasActiveTurn;
    }

    int queuedTurnCount(const QString& sessionId) const
    {
        const SessionPipeline* pipeline = findPipeline(sessionId);
        return pipeline ? pipeline->queue.size() : 0;
    }

    int totalDepth(const QString& sessionId) const
    {
        const SessionPipeline* pipeline = findPipeline(sessionId);
        if (!pipeline)
            return 0;
        return pipeline->queue.size() + (pipeline->hasActiveTurn ? 1 : 0);
    }

    TurnTask* queuedTail(const QString& sessionId)
    {
        SessionPipeline* pipeline = findPipeline(sessionId);
        if (!pipeline || pipeline->queue.isEmpty())
            return nullptr;
        return &pipeline->queue.last();
    }

    const TurnTask* queuedTail(const QString& sessionId) const
    {
        const SessionPipeline* pipeline = findPipeline(sessionId);
        if (!pipeline || pipeline->queue.isEmpty())
            return nullptr;
        return &pipeline->queue.last();
    }

    void enqueueTurn(const QString& sessionId, const TurnTask& turn)
    {
        ensurePipeline(sessionId).queue.append(turn);
    }

    TurnTask* activeTurn(const QString& sessionId)
    {
        SessionPipeline* pipeline = findPipeline(sessionId);
        if (!pipeline || !pipeline->hasActiveTurn)
            return nullptr;
        return &pipeline->activeTurn;
    }

    const TurnTask* activeTurn(const QString& sessionId) const
    {
        const SessionPipeline* pipeline = findPipeline(sessionId);
        if (!pipeline || !pipeline->hasActiveTurn)
            return nullptr;
        return &pipeline->activeTurn;
    }

    bool startNextTurn(const QString& sessionId, TurnTask* startedTurn = nullptr)
    {
        SessionPipeline* pipeline = findPipeline(sessionId);
        if (!pipeline || pipeline->hasActiveTurn || pipeline->queue.isEmpty())
            return false;

        pipeline->activeTurn = pipeline->queue.takeFirst();
        pipeline->hasActiveTurn = true;
        pipeline->pendingDeltaLog.clear();
        pipeline->pendingDeltaChunks = 0;
        pipeline->pendingDeltaStartedAtMs = 0;
        pipeline->lastDeltaFlushedAtMs = 0;

        if (startedTurn)
            *startedTurn = pipeline->activeTurn;
        return true;
    }

    bool clearActiveTurn(const QString& sessionId, TurnTask* oldTurn = nullptr)
    {
        SessionPipeline* pipeline = findPipeline(sessionId);
        if (!pipeline || !pipeline->hasActiveTurn)
            return false;

        if (oldTurn)
            *oldTurn = pipeline->activeTurn;
        pipeline->activeTurn = TurnTask();
        pipeline->hasActiveTurn = false;
        pipeline->pendingDeltaLog.clear();
        pipeline->pendingDeltaChunks = 0;
        pipeline->pendingDeltaStartedAtMs = 0;
        pipeline->lastDeltaFlushedAtMs = 0;
        return true;
    }

    QStringList sessionIds() const
    {
        return m_pipelines.keys();
    }

private:
    QHash<QString, SessionPipeline> m_pipelines;
};

#endif // TURNMANAGER_H
