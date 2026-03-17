#include "ConversationMemoryFinishCoordinator.h"

ConversationMemoryFinishCoordinator::ConversationMemoryFinishCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void ConversationMemoryFinishCoordinator::handleFinishMemory(const QString& sessionId,
                                                             const QString& agentId,
                                                             const TurnTask& finishedTurn,
                                                             bool skipMemoryForHeartbeat,
                                                             bool heartbeatTurn)
{
    if (!m_dependencies.emitPipelineEvent
        || !m_dependencies.maybeReflectMemoryAndEmit
        || !m_dependencies.refreshMemoryIndexAndEmit) {
        return;
    }

    if (skipMemoryForHeartbeat) {
        QJsonObject memoryExtra;
        memoryExtra.insert(QStringLiteral("reason"), QStringLiteral("heartbeat_turn"));
        memoryExtra.insert(QStringLiteral("reflection_triggered"),
                           m_dependencies.reflectionEnabled ? m_dependencies.reflectionEnabled() : false);
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("memory.skipped"),
            &finishedTurn,
            QString(),
            QString(),
            memoryExtra,
            true);
        if (!agentId.isEmpty()) {
            m_dependencies.maybeReflectMemoryAndEmit(
                sessionId,
                agentId,
                finishedTurn,
                true,
                QStringLiteral("heartbeat_turn"));
        }
        return;
    }

    if (!m_dependencies.retainTurn || agentId.isEmpty())
        return;

    QString memorySummary;
    QString memoryPath;
    QJsonObject memoryMetadata;
    QString memoryError;
    const bool retained = m_dependencies.retainTurn(
        agentId,
        sessionId,
        finishedTurn,
        &memorySummary,
        &memoryPath,
        &memoryMetadata,
        &memoryError);

    if (retained) {
        if (!memorySummary.trimmed().isEmpty()) {
            QJsonObject memoryExtra;
            memoryExtra.insert(QStringLiteral("doc_type"), QStringLiteral("daily"));
            memoryExtra.insert(QStringLiteral("summary"), memorySummary);
            memoryExtra.insert(QStringLiteral("path"), memoryPath);
            for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
                memoryExtra.insert(it.key(), it.value());
            m_dependencies.emitPipelineEvent(
                sessionId,
                QStringLiteral("memory.updated"),
                &finishedTurn,
                QString(),
                QString(),
                memoryExtra,
                true);
        }

        const int compactedCount = memoryMetadata.value(QStringLiteral("compacted_count")).toInt();
        if (compactedCount > 0) {
            QJsonObject compactExtra;
            compactExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
            compactExtra.insert(QStringLiteral("summary"), memorySummary);
            compactExtra.insert(QStringLiteral("compacted_count"), compactedCount);
            compactExtra.insert(QStringLiteral("path"), memoryMetadata.value(QStringLiteral("longMemoryPath")).toString());
            compactExtra.insert(QStringLiteral("longMemoryAdded"), memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt());
            compactExtra.insert(QStringLiteral("longMemoryDuplicate"), memoryMetadata.value(QStringLiteral("longMemoryDuplicate")).toInt());
            compactExtra.insert(QStringLiteral("manualRemember"), memoryMetadata.value(QStringLiteral("manualRemember")).toBool());
            for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
                compactExtra.insert(it.key(), it.value());
            m_dependencies.emitPipelineEvent(
                sessionId,
                QStringLiteral("memory.compacted"),
                &finishedTurn,
                QString(),
                QString(),
                compactExtra,
                true);
        }

        m_dependencies.refreshMemoryIndexAndEmit(
            sessionId,
            agentId,
            &finishedTurn,
            QStringLiteral("retain_turn"),
            memoryPath,
            memoryMetadata);

        m_dependencies.maybeReflectMemoryAndEmit(
            sessionId,
            agentId,
            finishedTurn,
            heartbeatTurn,
            heartbeatTurn ? QStringLiteral("heartbeat_turn") : QString());
        return;
    }

    QJsonObject memoryExtra;
    memoryExtra.insert(QStringLiteral("doc_type"), QStringLiteral("daily"));
    memoryExtra.insert(QStringLiteral("path"), memoryPath);
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("memory.error"),
        &finishedTurn,
        QString(),
        memoryError.isEmpty() ? QStringLiteral("memory retain failed") : memoryError,
        memoryExtra,
        true);
}
