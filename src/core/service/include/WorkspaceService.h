#ifndef WORKSPACESERVICE_H
#define WORKSPACESERVICE_H

#include "AppFacade.h"
#include <QHash>
#include <QString>
#include <memory>

class ApplicationServices;
class ChatStateRepository;
class QTimer;
class Session;
struct Message;

class WorkspaceService final : public IWorkspaceService {
public:
    explicit WorkspaceService(ApplicationServices& app);
    ~WorkspaceService();

    Session* createNewSession(const QString& agentName = QString()) override;
    Session* createSessionForIdentity(const QString& identityId,
                                      const QString& title = QString()) override;
    Session* createSessionForIdentityAs(const QString& actorIdentityId,
                                        const QString& identityId,
                                        const QString& title = QString()) override;
    QList<Session*> sessionsForIdentity(const QString& identityId) const override;
    void removeSession(const QString& sessionId) override;
    bool removeSessionAs(const QString& actorIdentityId, const QString& sessionId) override;
    void switchSession(const QString& sessionId) override;
    QString currentSessionId() const override;
    QString agentDisplayNameForSession(const QString& sessionId) const override;
    bool canIdentityManageSessions(const QString& identityId) const override;
    bool canIdentitySendMessage(const QString& identityId,
                                const QString& sessionId = QString()) const override;
    bool canIdentityManageGlobalConfig(const QString& identityId) const override;
    void saveSessionsToDisk() override;
    bool loadSessionsFromDisk() override;
    void saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId) override;
    ChatTabState loadTabState() const override;

    void initializeStateRepository();
    void startExternalSyncTimer();
    void appendSessionMessageToDisk(const QString& sessionId, const Message& msg);
    void pollExternalChanges();
    const QString& currentSessionIdValue() const;

private:
    ApplicationServices& m_app;
    std::unique_ptr<ChatStateRepository> m_stateRepository;
    QHash<QString, int> m_lastSavedMessageCounts;
    QString m_currentSessionId;
    QTimer* m_syncTimer = nullptr;
    QHash<QString, qint64> m_lastSyncRowIds;
};

#endif // WORKSPACESERVICE_H
