#include "MemoryToolWriteService.h"

#include <QUuid>

MemoryToolWriteService::MemoryToolWriteService(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

ToolResult MemoryToolWriteService::execute(const QJsonObject& args) const
{
    if (!m_dependencies.activeSessionForAgent
        || !m_dependencies.resolveSessionForAgent
        || !m_dependencies.activeTurnForSession
        || !m_dependencies.rememberToolRequested
        || !m_dependencies.emitPipelineEvent
        || !m_dependencies.refreshMemoryIndexAndEmit) {
        return ToolResult(QStringLiteral("错误: memory manager unavailable"),
                          QStringLiteral("记忆写入失败"),
                          false);
    }

    const QString agentId = args.value(QStringLiteral("_agent_id")).toString().trimmed();
    if (agentId.isEmpty()) {
        return ToolResult(
            QStringLiteral("错误: 缺少 _agent_id，上下文无法确定当前助手"),
            QStringLiteral("记忆写入失败：缺少助手上下文"),
            false);
    }

    QString sessionId = m_dependencies.activeSessionForAgent(agentId).trimmed();
    if (sessionId.isEmpty())
        sessionId = m_dependencies.resolveSessionForAgent(agentId).trimmed();

    const QString memoryText = args.value(QStringLiteral("memory")).toString().trimmed();
    const QString reason = args.value(QStringLiteral("reason")).toString().trimmed();
    const QString toolCallId = args.value(QStringLiteral("_tool_call_id")).toString().trimmed();

    TurnTask* activeTurn = sessionId.isEmpty() ? nullptr : m_dependencies.activeTurnForSession(sessionId);
    TurnTask syntheticTurn;
    const TurnTask* eventTurn = activeTurn;
    if (!eventTurn) {
        syntheticTurn.turnId = QStringLiteral("memory_write");
        syntheticTurn.requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        syntheticTurn.runId = QStringLiteral("memory_write");
        syntheticTurn.actorIdentityId = agentId;
        eventTurn = &syntheticTurn;
    }

    QString memorySummary;
    QString memoryPath;
    QJsonObject memoryMetadata;
    QString memoryError;
    const bool ok = m_dependencies.rememberToolRequested(agentId,
                                                         sessionId,
                                                         eventTurn ? eventTurn->turnId : QString(),
                                                         eventTurn ? eventTurn->requestTraceId : QString(),
                                                         memoryText,
                                                         reason,
                                                         &memorySummary,
                                                         &memoryPath,
                                                         &memoryMetadata,
                                                         &memoryError);

    if (!ok) {
        QJsonObject extra;
        extra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        extra.insert(QStringLiteral("path"), memoryPath);
        extra.insert(QStringLiteral("toolRequested"), true);
        if (!toolCallId.isEmpty())
            extra.insert(QStringLiteral("tool_call_id"), toolCallId);
        if (!reason.isEmpty())
            extra.insert(QStringLiteral("reason"), reason);
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("memory.error"),
            eventTurn,
            QString(),
            memoryError.isEmpty() ? QStringLiteral("tool memory write failed") : memoryError,
            extra,
            true);
        return ToolResult(
            memoryError.isEmpty() ? QStringLiteral("错误: memory_write 执行失败") : memoryError,
            QStringLiteral("记忆写入失败"),
            false,
            extra);
    }

    QJsonObject updateExtra;
    updateExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
    updateExtra.insert(QStringLiteral("summary"), memorySummary);
    updateExtra.insert(QStringLiteral("path"), memoryPath);
    updateExtra.insert(QStringLiteral("toolRequested"), true);
    if (!toolCallId.isEmpty())
        updateExtra.insert(QStringLiteral("tool_call_id"), toolCallId);
    if (!reason.isEmpty())
        updateExtra.insert(QStringLiteral("reason"), reason);
    for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
        updateExtra.insert(it.key(), it.value());
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("memory.updated"),
        eventTurn,
        QString(),
        QString(),
        updateExtra,
        true);

    const int compactedCount = memoryMetadata.value(QStringLiteral("compacted_count")).toInt();
    if (compactedCount > 0) {
        QJsonObject compactExtra = updateExtra;
        compactExtra.insert(QStringLiteral("compacted_count"), compactedCount);
        m_dependencies.emitPipelineEvent(
            sessionId,
            QStringLiteral("memory.compacted"),
            eventTurn,
            QString(),
            QString(),
            compactExtra,
            true);
    }

    if (memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt() > 0) {
        m_dependencies.refreshMemoryIndexAndEmit(sessionId,
                                                 agentId,
                                                 eventTurn,
                                                 QStringLiteral("tool_memory_write"),
                                                 memoryPath,
                                                 memoryMetadata);
    }

    QJsonObject resultData = updateExtra;
    resultData.insert(QStringLiteral("agent_id"), agentId);
    resultData.insert(QStringLiteral("session_id"), sessionId);

    const bool duplicateOnly =
        memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt() == 0
        && memoryMetadata.value(QStringLiteral("longMemoryDuplicate")).toInt() > 0;
    const QString raw =
        duplicateOnly
            ? QStringLiteral(
                  "memory_write: 已存在相同长期记忆，无需重复写入\nagent_id: %1\npath: %2\nmemory: %3")
                  .arg(agentId, memoryPath, memorySummary)
            : QStringLiteral("memory_write: 已写入长期记忆\nagent_id: %1\npath: %2\nmemory: %3")
                  .arg(agentId, memoryPath, memorySummary);
    const QString summary =
        duplicateOnly ? QStringLiteral("记忆已存在，无需重复写入")
                      : QStringLiteral("已写入长期记忆");
    return ToolResult(raw, summary, true, resultData);
}
