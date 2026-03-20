#ifndef APPLICATIONSERVICES_H
#define APPLICATIONSERVICES_H

#include "AppFacade.h"
#include <QObject>
#include <memory>

class IdentityManager;
class SessionManager;
class ChatPersistenceService;
class WorkspaceService;
class ConversationService;
class GovernanceService;
class MemoryService;
struct ConversationRuntimeEventsAccess;
struct ConversationCompletionAccess;

/**
 * @brief 应用级 facade / composition root。
 *
 * 对外只暴露 IAppFacade，四组子系统能力由独立服务对象承载。
 * 本类只负责生命周期装配、初始化顺序和 AppEventHub 桥接。
 */
class ApplicationServices : public QObject, public IAppFacade {
    Q_OBJECT
    friend class WorkspaceService;
    friend class ConversationService;
    friend class GovernanceService;
    friend class MemoryService;
    friend struct ConversationRuntimeEventsAccess;
    friend struct ConversationCompletionAccess;
public:
    explicit ApplicationServices(QObject* parent = nullptr);
    ~ApplicationServices() override;

    IWorkspaceService& workspace() override;
    IConversationService& conversation() override;
    IGovernanceService& governance() override;
    IMemoryService& memory() override;
    AppEventHub* events() override { return m_eventHub.get(); }
    void initialize() override;
    void setModelConfigPathOverride(const QString& filePath) override;
    void loadConfig() override;

signals:
    void conversationEvent(const QJsonObject& event);
    void streamDataReceived(const QString& sessionId, const QString& data);
    void finished(const QString& sessionId, const QString& fullContent);
    void errorOccurred(const QString& sessionId, const QString& errorMsg);
    void toolCallsStarted(const QString& sessionId);
    void toolEvent(const QString& sessionId, const ToolExecutionEvent& event);
    void reasoningStarted(const QString& sessionId);
    void reasoningStopped(const QString& sessionId);
    void sessionCreated(const QString& sessionId);
    void sessionRemoved(const QString& sessionId);
    void configLoaded();
    void modelCatalogUpdated(const QString& instanceId);

private:
    IdentityManager* m_identityManager = nullptr;
    SessionManager* m_sessionManager = nullptr;
    std::unique_ptr<ChatPersistenceService> m_persistence;
    bool m_logVerboseStreamEvents = false;
    std::unique_ptr<AppEventHub> m_eventHub;
    std::unique_ptr<WorkspaceService> m_workspaceService;
    std::unique_ptr<ConversationService> m_conversationService;
    std::unique_ptr<GovernanceService> m_governanceService;
    std::unique_ptr<MemoryService> m_memoryService;
};

#endif // APPLICATIONSERVICES_H

