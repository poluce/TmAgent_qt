#ifndef TASKSTATESERVICE_H
#define TASKSTATESERVICE_H

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

class ChatPersistenceService;

class TaskStateService {
public:
    void setPersistence(ChatPersistenceService* persistence);

    QJsonObject stateForSession(const QString& sessionId) const;
    bool updateState(const QString& sessionId, const QJsonObject& patch, QJsonObject* mergedState = nullptr);
    bool clearState(const QString& sessionId);

private:
    QString taskStatePath(const QString& sessionId) const;
    QJsonObject loadState(const QString& sessionId) const;
    static QString normalizeState(const QString& rawState);
    static void normalizePatch(QJsonObject* patch);

    ChatPersistenceService* m_persistence = nullptr;
    mutable QHash<QString, QJsonObject> m_stateCache;
    mutable QSet<QString> m_loadedSessions;
};

#endif // TASKSTATESERVICE_H
