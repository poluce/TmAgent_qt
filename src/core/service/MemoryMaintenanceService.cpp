#include "MemoryMaintenanceService.h"

MemoryMaintenanceService::MemoryMaintenanceService(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void MemoryMaintenanceService::refreshIndexAndEmit(const QString& sessionId,
                                                   const QString& agentId,
                                                   const TurnTask* turn,
                                                   const QString& reason,
                                                   const QString& sourcePath,
                                                   const QJsonObject& sourceMetadata) const
{
    if (!m_dependencies.rebuildSearchIndex || !m_dependencies.emitPipelineEvent)
        return;

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return;

    QJsonObject indexMetadata;
    QString indexError;
    const bool ok =
        m_dependencies.rebuildSearchIndex(trimmedAgentId, &indexMetadata, &indexError);

    QJsonObject extra;
    extra.insert(QStringLiteral("agent_id"), trimmedAgentId);
    extra.insert(QStringLiteral("reason"),
                 reason.trimmed().isEmpty() ? QStringLiteral("unknown") : reason.trimmed());
    if (!sourcePath.trimmed().isEmpty())
        extra.insert(QStringLiteral("source_path"), sourcePath);
    const QString longMemoryPath =
        sourceMetadata.value(QStringLiteral("longMemoryPath")).toString().trimmed();
    if (!longMemoryPath.isEmpty())
        extra.insert(QStringLiteral("longMemoryPath"), longMemoryPath);

    if (ok) {
        for (auto it = indexMetadata.constBegin(); it != indexMetadata.constEnd(); ++it)
            extra.insert(it.key(), it.value());
        m_dependencies.emitPipelineEvent(sessionId,
                                         QStringLiteral("memory.index.updated"),
                                         turn,
                                         QString(),
                                         QString(),
                                         extra,
                                         true);
    } else {
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("memory.index.error"),
            turn,
            QString(),
            indexError.trimmed().isEmpty() ? QStringLiteral("memory index rebuild failed")
                                           : indexError.trimmed(),
            extra,
            true);
    }
}

void MemoryMaintenanceService::maybeReflectAndEmit(const QString& sessionId,
                                                   const QString& agentId,
                                                   const TurnTask& turn,
                                                   bool forceReflection,
                                                   const QString& triggerReason) const
{
    if (!m_dependencies.reflectionEnabled
        || !m_dependencies.reflectionIntervalTurns
        || !m_dependencies.reflectAndScore
        || !m_dependencies.retainedTurnsForAgent
        || !m_dependencies.setRetainedTurnsForAgent
        || !m_dependencies.emitPipelineEvent) {
        return;
    }

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return;
    if (!m_dependencies.reflectionEnabled())
        return;

    const int interval = m_dependencies.reflectionIntervalTurns();
    if (interval <= 0 && !forceReflection)
        return;

    int retainedTurns = m_dependencies.retainedTurnsForAgent(trimmedAgentId);
    if (!forceReflection) {
        retainedTurns += 1;
        m_dependencies.setRetainedTurnsForAgent(trimmedAgentId, retainedTurns);
        if ((retainedTurns % interval) != 0)
            return;
    }

    QString summary;
    QString writtenPath;
    QJsonObject reflectMetadata;
    QString reflectError;
    const bool reflected = m_dependencies.reflectAndScore(trimmedAgentId,
                                                          sessionId,
                                                          turn.turnId,
                                                          turn.requestTraceId,
                                                          &summary,
                                                          &writtenPath,
                                                          &reflectMetadata,
                                                          &reflectError);
    const QString reflectionTrigger = forceReflection
        ? (triggerReason.trimmed().isEmpty() ? QStringLiteral("forced")
                                            : triggerReason.trimmed())
        : QStringLiteral("retain_interval");

    if (!reflected) {
        QJsonObject extra;
        extra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        extra.insert(QStringLiteral("path"), writtenPath);
        extra.insert(QStringLiteral("reflection"), true);
        extra.insert(QStringLiteral("reflection_trigger"), reflectionTrigger);
        extra.insert(QStringLiteral("reflection_interval_turns"), interval);
        extra.insert(QStringLiteral("retained_turn_count"), retainedTurns);
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("memory.error"),
            &turn,
            QString(),
            reflectError.isEmpty() ? QStringLiteral("memory reflection failed") : reflectError,
            extra,
            true);
        return;
    }

    QJsonObject extra = reflectMetadata;
    extra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
    extra.insert(QStringLiteral("summary"), summary);
    extra.insert(QStringLiteral("path"), writtenPath);
    extra.insert(QStringLiteral("reflection"), true);
    extra.insert(QStringLiteral("reflection_trigger"), reflectionTrigger);
    extra.insert(QStringLiteral("reflection_interval_turns"), interval);
    extra.insert(QStringLiteral("retained_turn_count"), retainedTurns);
    m_dependencies.emitPipelineEvent(sessionId,
                                     QStringLiteral("memory.reflected"),
                                     &turn,
                                     QString(),
                                     QString(),
                                     extra,
                                     true);

    QJsonObject qualityExtra = extra;
    qualityExtra.insert(QStringLiteral("quality_score"),
                        reflectMetadata.value(QStringLiteral("quality_score")).toInt());
    qualityExtra.insert(QStringLiteral("quality_level"),
                        reflectMetadata.value(QStringLiteral("quality_level")).toString());
    m_dependencies.emitPipelineEvent(sessionId,
                                     QStringLiteral("memory.quality"),
                                     &turn,
                                     QString(),
                                     QString(),
                                     qualityExtra,
                                     true);

    if (reflectMetadata.value(QStringLiteral("longMemoryAdded")).toInt() > 0) {
        refreshIndexAndEmit(sessionId,
                            trimmedAgentId,
                            &turn,
                            QStringLiteral("reflect_turn"),
                            writtenPath,
                            reflectMetadata);
    }
}
