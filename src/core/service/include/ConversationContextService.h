#ifndef CONVERSATIONCONTEXTSERVICE_H
#define CONVERSATIONCONTEXTSERVICE_H

#include "ConversationContextTypes.h"
#include "TurnManager.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <functional>

class ConversationContextService {
public:
    struct Options {
        int compactThresholdChars = 1200;
        int previewChars = 220;
    };

    struct Dependencies {
        std::function<bool(const QString&, const ConversationContext::TaskContextSnapshot&)> saveTaskContextSnapshot;
        std::function<bool(const QString&, const ConversationContext::ContextCompressionCheckpoint&)> saveContextCompressionCheckpoint;
        std::function<bool(const QString&, const ConversationContext::ResumePacket&)> saveResumePacket;
        std::function<ConversationContext::TaskContextSnapshot(const QString&, bool* ok)> loadTaskContextSnapshot;
        std::function<void(const QString&,
                           const QString&,
                           const TurnTask*,
                           const QString&,
                           const QString&,
                           const QJsonObject&,
                           bool)> emitPipelineEvent;
    };

    explicit ConversationContextService(const Dependencies& dependencies);
    ConversationContextService(const Dependencies& dependencies, const Options& options);

    void persistCompletionArtifacts(const QString& sessionId,
                                    const TurnTask& finishedTurn,
                                    const QJsonObject& existingTaskState,
                                    const QDateTime& nowUtc) const;

private:
    Dependencies m_dependencies;
    Options m_options;
};

#endif // CONVERSATIONCONTEXTSERVICE_H
