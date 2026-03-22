#ifndef CONVERSATIONSERVICE_H
#define CONVERSATIONSERVICE_H

#include "AppFacade.h"
#include "TurnManager.h"
#include "core/agent/ToolTypes.h"
#include "llm/LLMTypes.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <memory>

class AgentRuntime;
class ApplicationServices;
class Identity;
class RuntimeManager;
class Session;
class TaskStateService;
struct ConversationRuntimeEventsAccess;
struct ConversationCompletionAccess;
struct Message;

class ConversationService final : public IConversationService {
    friend class ApplicationServices;
    friend class WorkspaceService;
    friend class GovernanceService;
    friend class MemoryService;
    friend struct ConversationRuntimeEventsAccess;
    friend struct ConversationCompletionAccess;
public:
    explicit ConversationService(ApplicationServices& app);
    ~ConversationService();

    QString enqueueUserMessage(const QString& sessionId,
                               const QString& text,
                               const QString& clientMessageId = QString()) override;
    QString enqueueUserMessageAs(const QString& actorIdentityId,
                                 const QString& sessionId,
                                 const QString& text,
                                 const QString& clientMessageId = QString()) override;
    void sendUserMessage(const QString& sessionId, const QString& text) override;
    void sendUserMessageAs(const QString& actorIdentityId,
                           const QString& sessionId,
                           const QString& text) override;
    void abortCurrent(const QString& sessionId) override;
    QString abortAndRollback(const QString& sessionId) override;
    bool isSessionStreaming(const QString& sessionId) const override;
    int pendingTurnCount(const QString& sessionId) const override;
    QString activeRunId(const QString& sessionId) const override;
    QJsonObject taskStateForSession(const QString& sessionId) const override;
    QString runtimeIdentityIdForSession(const QString& sessionId) const override;
    QJsonArray ioHistoryForSession(const QString& sessionId) const override;
    QString modelDisplayName(const LLMConfig& config) const override;
    bool renameSessionAndRuntime(const QString& sessionId, const QString& name) override;
    void clearConversationHistory(const QString& sessionId) override;

private:
    struct DelegateStats {
        int totalCount = 0;
        int successCount = 0;
        int failureCount = 0;
        qint64 totalDurationMs = 0;
    };

    RuntimeManager* runtimeManager() const;
    TaskStateService* taskStateService() const;
    TurnManager& turnManager();
    const TurnManager& turnManager() const;
    QHash<QString, QString>& activeSessionByAgent();
    const QHash<QString, QString>& activeSessionByAgent() const;
    QHash<QString, qint64>& delegateStartMsByToolKey();
    const QHash<QString, qint64>& delegateStartMsByToolKey() const;
    QHash<QString, DelegateStats>& delegateStatsBySession();
    const QHash<QString, DelegateStats>& delegateStatsBySession() const;
    QHash<QString, qint64>& toolProgressLastPersistMsByKey();
    const QHash<QString, qint64>& toolProgressLastPersistMsByKey() const;
    QHash<QString, QString>& toolProgressLastDigestByKey();
    const QHash<QString, QString>& toolProgressLastDigestByKey() const;
    SessionPipeline& ensurePipeline(const QString& sessionId);
    SessionPipeline* findPipeline(const QString& sessionId);
    const SessionPipeline* findPipeline(const QString& sessionId) const;
    QString agentIdentityIdForSession(const QString& sessionId) const;
    Identity* findOrCreateAgentIdentity(Session* session);
    AgentRuntime* runtimeForSession(const QString& sessionId) const;
    AgentRuntime* ensureRuntimeForSession(const QString& sessionId);
    AgentRuntime* ensureRuntimeForAgent(Identity* agentIdentity);
    void releaseRuntimeIfUnused(const QString& agentIdentityId);
    LLMConfig composeConfigForIdentity(Identity* identity) const;
    QJsonArray buildRuntimeHistoryFromMessages(Session* session) const;
    void tryStartNextTurn(const QString& sessionId);
    void tryStartNextTurnForAgent(const QString& agentIdentityId);
    void enqueueInternalTurn(const QString& sessionId,
                             const QString& content,
                             const QString& clientMessageId = QString());
    void enqueueTeammateReplyTurn(const QString& sessionId,
                                  const QString& content,
                                  const QJsonObject& payload,
                                  const QString& clientMessageId = QString());
    void resetSessionStreamState(const QString& sessionId);
    void finalizeTurn(const QString& sessionId, TurnTask* outTurn);
    void flushPendingDeltaLog(const QString& sessionId,
                              SessionPipeline* pipeline,
                              const TurnTask* turn,
                              bool force);
    bool appendEventLog(const QJsonObject& event) const;
    void emitPipelineEvent(const QString& type,
                           const QString& sessionId,
                           const TurnTask* turn = nullptr,
                           const QString& delta = QString(),
                           const QString& error = QString(),
                           const QJsonObject& extra = QJsonObject(),
                           bool persistToDisk = true);
    void appendRuntimeIoEventEntry(const QString& sessionId,
                                   const QString& type,
                                   const TurnTask* turn,
                                   const QString& error,
                                   const QJsonObject& extra);
    void clearToolProgressCacheForSession(const QString& sessionId);
    void clearDelegateStartsForSession(const QString& sessionId);
    void onRuntimeStreamData(const QString& sessionId, const QString& data);
    void onRuntimeFinished(const QString& sessionId, const QString& fullContent);
    void onRuntimeError(const QString& sessionId, const QString& errorMsg);
    void onRuntimeToolCallsStarted(const QString& sessionId);
    void connectRuntimeSignals(AgentRuntime* runtime);
    void updateTaskStateForSession(const QString& sessionId,
                                   const QString& state,
                                   const TurnTask* turn,
                                   const QJsonObject& extra = QJsonObject());
    void clearTaskStateForSession(const QString& sessionId);
    void handleTeammateReply(const QString& teammateId,
                             const QString& teammateName,
                             bool success,
                             const QString& content,
                             const QString& threadId);
    void onRuntimeToolEvent(const QString& sessionId, const ToolExecutionEvent& event);

private:
    ApplicationServices& m_app;
    RuntimeManager* m_runtimeManager = nullptr;
    std::unique_ptr<TaskStateService> m_taskStateService;
    TurnManager m_turnManager;
    QHash<QString, QString> m_agentActiveSession;
    QHash<QString, qint64> m_delegateStartMsByToolKey;
    QHash<QString, DelegateStats> m_delegateStatsBySession;
    QHash<QString, qint64> m_toolProgressLastPersistMsByKey;
    QHash<QString, QString> m_toolProgressLastDigestByKey;
};

#endif // CONVERSATIONSERVICE_H
