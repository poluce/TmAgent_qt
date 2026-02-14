#ifndef CHATSTATEREPOSITORY_H
#define CHATSTATEREPOSITORY_H

#include "core/service/TurnManager.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QString>
#include <functional>

class IdentityManager;
class SessionManager;
class Session;
class ToolDispatcher;
class ChatPersistenceService;
struct LLMConfig;

class ChatStateRepository {
public:
    struct LoadResult {
        bool success = false;
        QString currentSessionId;
        QHash<QString, int> savedMessageCounts;
        QHash<QString, QList<TurnTask>> pendingTurnsBySession;
    };

    ChatStateRepository();

    void setDependencies(IdentityManager* identityManager,
                         SessionManager* sessionManager,
                         ToolDispatcher* toolDispatcher,
                         ChatPersistenceService* persistence);

    QHash<QString, int> saveState(
        const QString& currentSessionId,
        const QHash<QString, int>& previousMessageCounts,
        const std::function<const SessionPipeline*(const QString&)>& pipelineLookup) const;

    LoadResult loadState(const LLMConfig& defaultConfig) const;

private:
    bool isReady() const;
    QStringList collectToolNames() const;
    static QString remapIdentityId(const QString& oldId,
                                   const QHash<QString, QString>& identityIdMap);

    void saveDirectoryStructure() const;
    void saveManifestAndAppState(const QString& currentSessionId) const;
    QStringList saveIdentities() const;
    void cleanupStaleAgentDirs(const QStringList& activeAgentIds) const;
    QJsonObject serializePendingTurns(const SessionPipeline* pipeline) const;
    QJsonObject buildSessionIndexItem(Session* session) const;

    IdentityManager* m_identityManager = nullptr;
    SessionManager* m_sessionManager = nullptr;
    ToolDispatcher* m_toolDispatcher = nullptr;
    ChatPersistenceService* m_persistence = nullptr;
};

#endif // CHATSTATEREPOSITORY_H
