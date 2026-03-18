#ifndef CONVERSATIONDISPATCHCOORDINATOR_H
#define CONVERSATIONDISPATCHCOORDINATOR_H

#include "TurnManager.h"
#include "core/service/include/ConversationContextTypes.h"
#include "llm/LLMTypes.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>

class Identity;
class Session;

class ConversationDispatchCoordinator {
public:
    struct Limits {
        int memoryContextMaxChars = 4500;
    };

    struct Dependencies {
        TurnManager* turnManager = nullptr;

        std::function<SessionPipeline*(const QString&)> findPipeline;
        std::function<Identity*(const QString&, QString* agentIdOut)> ensureRuntimeIdentityForSession;
        std::function<Session*(const QString&)> findSession;
        std::function<QString(const QString&)> activeSessionForAgent;
        std::function<void(const QString&, const QString&)> setActiveSessionForAgent;
        std::function<QJsonArray(Session*)> buildRuntimeHistoryFromMessages;
        std::function<qint64(const QJsonArray&)> estimateHistoryChars;
        std::function<void(const QString&, const QJsonArray&)> setRuntimeHistory;
        std::function<void(const QString&, const LLMConfig&)> setRuntimeConfig;
        std::function<void(const QString&, const QJsonObject&)> setRuntimeIoContext;
        std::function<void(const QString&, const QString&)> sendRuntimeMessage;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QJsonObject&)> updateTaskStateForSession;
        std::function<QString(const QString&, int)> taskStateTextPreview;
        std::function<void(const QString&, const QString&)> reportPulseProgress;
        std::function<void(Identity*)> ensureMemoryInitializedForAgent;
        std::function<LLMConfig(Identity*)> composeConfigForIdentity;
        std::function<QString(const QString&, int)> composeMemoryContext;
        std::function<QString(const QString&)> delegateContextForAgent;
        std::function<ConversationContext::TaskContextSnapshot(const QString&, bool* ok)> loadTaskContextSnapshot;
        std::function<ConversationContext::ContextCompressionCheckpoint(const QString&, bool* ok)> loadContextCompressionCheckpoint;
    };

    ConversationDispatchCoordinator(const Dependencies& dependencies, const Limits& limits);

    bool tryStartNextTurn(const QString& sessionId);

private:
    Dependencies m_dependencies;
    Limits m_limits;
};

#endif // CONVERSATIONDISPATCHCOORDINATOR_H

