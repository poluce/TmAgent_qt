#include "ConversationToolPersistenceCoordinator.h"

#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

ConversationToolPersistenceCoordinator::ConversationToolPersistenceCoordinator(const Dependencies& dependencies)
    : m_dependencies(dependencies)
{
}

void ConversationToolPersistenceCoordinator::handleToolEvent(const QString& sessionId,
                                                             const QString& agentId,
                                                             const TurnTask* activeTurn,
                                                             const ToolExecutionEvent& event)
{
    if (!activeTurn
        || !m_dependencies.postMessage
        || !m_dependencies.sessionDataDirPath
        || !m_dependencies.sanitizePersistedToolArguments
        || !m_dependencies.sanitizePersistedToolEventData
        || !m_dependencies.sanitizePersistedToolRawResult
        || !m_dependencies.toolEventToJson
        || !m_dependencies.emitToolEvent
        || !m_dependencies.emitPipelineEvent
        || !m_dependencies.toolProgressLastPersistMs
        || !m_dependencies.toolProgressLastDigest
        || !m_dependencies.setToolProgressLastPersistMs
        || !m_dependencies.setToolProgressLastDigest) {
        return;
    }

    const QString toolName = event.toolName.trimmed();
    const QString toolId = event.toolId.trimmed();
    const bool isDelegateTool = (toolName == QLatin1String("delegate_task"));

    if (!agentId.isEmpty()) {
        if (event.status == QLatin1String("started")) {
            if (toolId.isEmpty()) {
                qWarning() << "[ConversationToolPersistenceCoordinator] 跳过无效 tool_call 事件：缺少 toolId，session="
                           << sessionId << "toolName=" << toolName;
            } else {
                Message toolCallMsg = Message::createToolCall(sessionId, agentId, QString(), QJsonObject());
                toolCallMsg.content.text.clear();
                toolCallMsg.traceId = activeTurn->requestTraceId;
                toolCallMsg.turnId = activeTurn->turnId;
                toolCallMsg.status = Message::Status::Completed;
                toolCallMsg.content.payload.insert(QStringLiteral("tool_name"), toolName);
                toolCallMsg.content.payload.insert(QStringLiteral("tool_call_id"), toolId);
                toolCallMsg.content.payload.insert(
                    QStringLiteral("arguments"),
                    m_dependencies.sanitizePersistedToolArguments(toolName, event.data));
                m_dependencies.postMessage(sessionId, toolCallMsg);
            }
        } else if (event.status == QLatin1String("completed")) {
            const bool isPseudoToolEvent = (toolName == QLatin1String("tool_loop_guard"));
            if (toolId.isEmpty() || isPseudoToolEvent) {
                qWarning() << "[ConversationToolPersistenceCoordinator] 跳过无效 tool_result 事件：session="
                           << sessionId << "toolName=" << toolName << "toolId=" << toolId;
            } else {
                Message toolResultMsg = Message::createToolResult(sessionId, agentId, toolId, QString());
                toolResultMsg.content.text.clear();
                toolResultMsg.traceId = activeTurn->requestTraceId;
                toolResultMsg.turnId = activeTurn->turnId;
                toolResultMsg.status = Message::Status::Completed;
                toolResultMsg.content.payload.insert(QStringLiteral("tool_name"), toolName);
                toolResultMsg.content.payload.insert(QStringLiteral("success"), event.success);
                toolResultMsg.content.payload.insert(
                    QStringLiteral("raw_result"),
                    m_dependencies.sanitizePersistedToolRawResult(toolName, event.rawResult));
                toolResultMsg.content.payload.insert(QStringLiteral("formatted_result"), event.formattedResult);
                const QJsonObject persistedEventData =
                    m_dependencies.sanitizePersistedToolEventData(toolName, event.data);
                if (!persistedEventData.isEmpty())
                    toolResultMsg.content.payload.insert(QStringLiteral("event_data"), persistedEventData);
                if (isDelegateTool) {
                    const QString childRequestId = event.data.value(QStringLiteral("child_request_id")).toString().trimmed();
                    if (!childRequestId.isEmpty())
                        toolResultMsg.content.payload.insert(QStringLiteral("child_request_id"), childRequestId);
                    const QString childTraceId = event.data.value(QStringLiteral("child_trace_id")).toString().trimmed();
                    if (!childTraceId.isEmpty())
                        toolResultMsg.content.payload.insert(QStringLiteral("child_trace_id"), childTraceId);
                    const QString childAgentId = event.data.value(QStringLiteral("child_agent_id")).toString().trimmed();
                    if (!childAgentId.isEmpty())
                        toolResultMsg.content.payload.insert(QStringLiteral("child_agent_id"), childAgentId);
                    const QString childModel = event.data.value(QStringLiteral("child_model")).toString().trimmed();
                    if (!childModel.isEmpty())
                        toolResultMsg.content.payload.insert(QStringLiteral("child_model"), childModel);
                    const QString failureReason = event.data.value(QStringLiteral("failure_reason")).toString().trimmed();
                    if (!failureReason.isEmpty())
                        toolResultMsg.content.payload.insert(QStringLiteral("failure_reason"), failureReason);
                }
                m_dependencies.postMessage(sessionId, toolResultMsg);
            }
        }
    }

    if (toolName == QLatin1String("send_file")
        && event.status == QLatin1String("completed")
        && event.success
        && !event.data.isEmpty()) {
        const QString tmpFilePath = event.data.value(QStringLiteral("file_path")).toString();
        const QString fileName = event.data.value(QStringLiteral("file_name")).toString();
        const qint64 fileSize = static_cast<qint64>(event.data.value(QStringLiteral("file_size")).toDouble());
        const QString description = event.data.value(QStringLiteral("description")).toString();
        if (!tmpFilePath.isEmpty() && !fileName.isEmpty() && !agentId.isEmpty()) {
            QString finalFilePath = tmpFilePath;
            const QString sessionDir = m_dependencies.sessionDataDirPath(sessionId);
            if (!sessionDir.isEmpty()) {
                const QString filesDir = sessionDir + QStringLiteral("/files/")
                    + QUuid::createUuid().toString(QUuid::WithoutBraces);
                QDir().mkpath(filesDir);
                const QString destPath = QDir(filesDir).filePath(fileName);
                if (QFile::rename(tmpFilePath, destPath)) {
                    finalFilePath = destPath;
                    QDir tmpDir(QFileInfo(tmpFilePath).absolutePath());
                    tmpDir.removeRecursively();
                }
            }
            Message fileMsg = Message::createFile(sessionId, agentId, finalFilePath, fileName, fileSize, description);
            fileMsg.traceId = activeTurn->requestTraceId;
            fileMsg.turnId = activeTurn->turnId;
            m_dependencies.postMessage(sessionId, fileMsg);
        }
    }

    m_dependencies.emitToolEvent(sessionId, event);

    bool persistToolEvent = true;
    QJsonObject eventObj = m_dependencies.toolEventToJson(event);
    if (event.status == QLatin1String("progress")) {
        QString progressDigest = event.formattedResult;
        progressDigest.replace(QLatin1Char('\r'), QLatin1Char(' '));
        progressDigest.replace(QLatin1Char('\n'), QLatin1Char(' '));
        progressDigest = progressDigest.simplified();
        if (progressDigest.size() > 160)
            progressDigest = progressDigest.left(160) + QStringLiteral("...");

        const QString progressKey = QStringLiteral("%1|%2|%3|%4")
                                        .arg(sessionId.trimmed(),
                                             activeTurn->runId.trimmed(),
                                             toolName,
                                             toolId.isEmpty() ? QStringLiteral("_") : toolId);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 lastMs = m_dependencies.toolProgressLastPersistMs(progressKey);
        const QString lastDigest = m_dependencies.toolProgressLastDigest(progressKey);
        const bool due = (lastMs <= 0)
            || ((nowMs - lastMs) >= m_dependencies.toolProgressPersistMinIntervalMs);
        const bool changed = (!progressDigest.isEmpty() && progressDigest != lastDigest);
        persistToolEvent = due || changed;

        if (persistToolEvent) {
            m_dependencies.setToolProgressLastPersistMs(progressKey, nowMs);
            m_dependencies.setToolProgressLastDigest(progressKey, progressDigest);
        }

        QString clippedFormatted = event.formattedResult;
        if (clippedFormatted.size() > 320)
            clippedFormatted = clippedFormatted.left(320) + QStringLiteral("...");
        eventObj.insert(QStringLiteral("formattedResult"), clippedFormatted);
        eventObj.insert(QStringLiteral("rawResult"), QString());
    }

    QJsonObject extra;
    extra.insert(QStringLiteral("toolEvent"), eventObj);
    m_dependencies.emitPipelineEvent(
        sessionId,
        QStringLiteral("turn_tool_event"),
        activeTurn,
        QString(),
        QString(),
        extra,
        persistToolEvent);
}
