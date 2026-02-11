#ifndef TURNMANAGER_H
#define TURNMANAGER_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

struct TurnTask {
    QString requestTraceId;
    QString turnId;
    QString runId;
    QString actorIdentityId;
    qint64 enqueuedAtMs = 0;
    int mergedMessageCount = 1;
    QString clientMessageId;
    QString userContent;
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

    QStringList sessionIds() const
    {
        return m_pipelines.keys();
    }

private:
    QHash<QString, SessionPipeline> m_pipelines;
};

#endif // TURNMANAGER_H
