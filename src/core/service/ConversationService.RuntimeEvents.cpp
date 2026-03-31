#include "ConversationService.h"

#include "ApplicationServices.h"
#include "MemoryService.h"
#include "AgentRuntime.h"
#include "ChatCoordinatorSupport.h"
#include "HeartbeatService.h"
#include "core/agent/ToolTypes.h"
#include "core/manager/SessionManager.h"
#include "core/model/Message.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

struct ConversationRuntimeEventsAccess {
    static void handleDelegateTracking(ConversationService& service,
                                       const QString& sessionId,
                                       const QString& agentId,
                                       const TurnTask* activeTurn,
                                       const ToolExecutionEvent& event);
    static void handleToolPersistence(ConversationService& service,
                                      const QString& sessionId,
                                      const QString& agentId,
                                      const TurnTask* activeTurn,
                                      const ToolExecutionEvent& event);
};

namespace {
using ChatCoordinatorSupport::delegateToolKey;
using ChatCoordinatorSupport::sanitizePersistedToolArguments;
using ChatCoordinatorSupport::sanitizePersistedToolEventData;
using ChatCoordinatorSupport::sanitizePersistedToolRawResult;
using ChatCoordinatorSupport::taskStateTextPreview;
using ChatCoordinatorSupport::toolEventToJson;

constexpr qint64 kToolProgressPersistMinIntervalMs = 1200;
} // namespace

void ConversationService::onRuntimeStreamData(const QString& sessionId, const QString& data)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (!pipeline || !activeTurn)
        return;

    activeTurn->assistantContent.append(data);
    if (m_app.m_memoryService) {
        m_app.m_memoryService->reportPulseProgress(agentIdentityIdForSession(sessionId),
                                                   QStringLiteral("stream"));
    }

    Session* session = m_app.m_sessionManager ? m_app.m_sessionManager->findById(sessionId) : nullptr;
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.append(data);
        state.isStreaming = true;
    }

    emit m_app.streamDataReceived(sessionId, data);
    emitPipelineEvent(QStringLiteral("turn_delta"),
                      sessionId,
                      activeTurn,
                      data,
                      QString(),
                      QJsonObject(),
                      m_app.m_logVerboseStreamEvents);

    if (!m_app.m_logVerboseStreamEvents && !data.isEmpty()) {
        if (pipeline->pendingDeltaLog.isEmpty())
            pipeline->pendingDeltaStartedAtMs = QDateTime::currentMSecsSinceEpoch();
        pipeline->pendingDeltaLog.append(data);
        ++pipeline->pendingDeltaChunks;
        flushPendingDeltaLog(sessionId, pipeline, activeTurn, false);
    }
}

void ConversationService::onRuntimeToolCallsStarted(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (!pipeline || !activeTurn)
        return;

    flushPendingDeltaLog(sessionId, pipeline, activeTurn, true);

    if (m_app.m_sessionManager) {
        Session* session = m_app.m_sessionManager->findById(sessionId);
        if (session) {
            Session::StreamState& state = session->streamState();
            state.buffer.clear();
            state.lastMsgIsTool = true;
        }
    }

    emit m_app.toolCallsStarted(sessionId);
    emitPipelineEvent(QStringLiteral("turn_tool_calls_started"), sessionId, activeTurn);
}

void ConversationService::onRuntimeToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (!pipeline || !activeTurn || !m_app.m_sessionManager)
        return;

    const QString agentId = agentIdentityIdForSession(sessionId);
    if (m_app.m_memoryService)
        m_app.m_memoryService->reportPulseProgress(agentId, QStringLiteral("tool_event"));

    ConversationRuntimeEventsAccess::handleDelegateTracking(
        *this, sessionId, agentId, activeTurn, event);
    ConversationRuntimeEventsAccess::handleToolPersistence(
        *this, sessionId, agentId, activeTurn, event);
}

void ConversationService::connectRuntimeSignals(AgentRuntime* runtime)
{
    if (!runtime)
        return;
    if (m_app.m_memoryService)
        m_app.m_memoryService->ensureAgentPulse(runtime->identityId());
    QObject::connect(runtime,
                     &AgentRuntime::streamDataReceived,
                     &m_app,
                     [this](const QString& sessionId, const QString& data) {
                         onRuntimeStreamData(sessionId, data);
                     });
    QObject::connect(runtime,
                     &AgentRuntime::finished,
                     &m_app,
                     [this](const QString& sessionId, const QString& fullContent) {
                         onRuntimeFinished(sessionId, fullContent);
                     });
    QObject::connect(runtime,
                     &AgentRuntime::errorOccurred,
                     &m_app,
                     [this](const QString& sessionId, const QString& errorMsg) {
                         onRuntimeError(sessionId, errorMsg);
                     });
    QObject::connect(runtime,
                     &AgentRuntime::toolCallsStarted,
                     &m_app,
                     [this](const QString& sessionId) { onRuntimeToolCallsStarted(sessionId); });
    QObject::connect(runtime,
                     &AgentRuntime::toolEvent,
                     &m_app,
                     [this](const QString& sessionId, const ToolExecutionEvent& event) {
                         onRuntimeToolEvent(sessionId, event);
                     });
    QObject::connect(runtime,
                     &AgentRuntime::reasoningStarted,
                     &m_app,
                     [&app = m_app](const QString& sessionId) { emit app.reasoningStarted(sessionId); });
    QObject::connect(runtime,
                     &AgentRuntime::reasoningStopped,
                     &m_app,
                     [&app = m_app](const QString& sessionId) { emit app.reasoningStopped(sessionId); });
}

void ConversationRuntimeEventsAccess::handleDelegateTracking(ConversationService& service,
                                                             const QString& sessionId,
                                                             const QString& agentId,
                                                             const TurnTask* activeTurn,
                                                             const ToolExecutionEvent& event)
{
    if (!activeTurn)
        return;

    const QString toolName = event.toolName.trimmed();
    const QString toolId = event.toolId.trimmed();
    const bool isDelegateTool = (toolName == QLatin1String("delegate_task"));
    if (!isDelegateTool)
        return;

    if (event.status == QLatin1String("started")) {
        QJsonObject taskExtra;
        taskExtra.insert(QStringLiteral("reason"), QStringLiteral("delegate_tool_started"));
        taskExtra.insert(QStringLiteral("source_event"), QStringLiteral("delegate.tool_started"));
        taskExtra.insert(QStringLiteral("summary"),
                         taskStateTextPreview(activeTurn->userContent, 220));
        taskExtra.insert(QStringLiteral("current_step"), QStringLiteral("正在委派后台子代理"));
        taskExtra.insert(QStringLiteral("next_step"), QStringLiteral("等待后台任务提交结果"));
        service.updateTaskStateForSession(sessionId,
                                          QStringLiteral("running"),
                                          activeTurn,
                                          taskExtra);

        if (!agentId.isEmpty() && service.m_app.m_memoryService->heartbeatService()) {
            service.m_app.m_memoryService->heartbeatService()->suppressAgentHeartbeat(
                agentId, QStringLiteral("delegate_running"));
        }
        if (!toolId.isEmpty()) {
            service.m_delegateStartMsByToolKey.insert(delegateToolKey(sessionId, toolId),
                                                      QDateTime::currentMSecsSinceEpoch());
        }

        QJsonObject delegateExtra;
        delegateExtra.insert(QStringLiteral("toolName"), toolName);
        delegateExtra.insert(QStringLiteral("toolId"), toolId);
        delegateExtra.insert(QStringLiteral("event"), QStringLiteral("started"));
        const QString taskPreview = event.data.value(QStringLiteral("task")).toString().trimmed();
        if (!taskPreview.isEmpty())
            delegateExtra.insert(QStringLiteral("task_preview"), taskPreview.left(200));
        const QString rolePrompt =
            event.data.value(QStringLiteral("role_prompt")).toString().trimmed();
        if (!rolePrompt.isEmpty())
            delegateExtra.insert(QStringLiteral("role_prompt_preview"), rolePrompt.left(120));
        const QString parentAgentId =
            event.data.value(QStringLiteral("_agent_id")).toString().trimmed();
        if (!parentAgentId.isEmpty())
            delegateExtra.insert(QStringLiteral("parent_agent_id"), parentAgentId);

        service.emitPipelineEvent(QStringLiteral("delegate.tool_started"),
                                  sessionId,
                                  activeTurn,
                                  QString(),
                                  QString(),
                                  delegateExtra,
                                  true);
        return;
    }

    if (event.status != QLatin1String("completed"))
        return;

    const QString delegateStatus =
        event.data.value(QStringLiteral("status")).toString().trimmed().toLower();
    const QString delegateBackend =
        event.data.value(QStringLiteral("backend")).toString().trimmed().toLower();
    const QString jobId = event.data.value(QStringLiteral("job_id")).toString().trimmed();
    if (event.success && delegateStatus == QLatin1String("accepted") && !jobId.isEmpty()) {
        bool skipTaskStateUpdate = false;
        const QJsonObject currentState = service.taskStateForSession(sessionId);
        const QString currentStateStr =
            currentState.value(QStringLiteral("state")).toString().trimmed();
        if (currentStateStr == QLatin1String("done")
            || currentStateStr == QLatin1String("failed")) {
            skipTaskStateUpdate = true;
        }

        if (!skipTaskStateUpdate) {
            QJsonObject taskExtra;
            taskExtra.insert(QStringLiteral("reason"), QStringLiteral("delegate_running"));
            taskExtra.insert(QStringLiteral("source_event"),
                             QStringLiteral("delegate.tool_completed"));
            taskExtra.insert(QStringLiteral("summary"),
                             taskStateTextPreview(event.formattedResult.isEmpty()
                                                      ? activeTurn->userContent
                                                      : event.formattedResult,
                                                  220));
            taskExtra.insert(
                QStringLiteral("current_step"),
                delegateBackend == QLatin1String("codex")
                    ? QStringLiteral("等待 Codex 子代理任务完成")
                    : QStringLiteral("等待后台子代理任务完成"));
            taskExtra.insert(QStringLiteral("next_step"),
                             QStringLiteral("可查询进度、等待完成通知或取消任务"));
            taskExtra.insert(QStringLiteral("waiting_job_id"), jobId);
            if (!delegateBackend.isEmpty())
                taskExtra.insert(QStringLiteral("delegate_backend"), delegateBackend);
            taskExtra.insert(QStringLiteral("last_error"), QJsonValue::Null);
            service.updateTaskStateForSession(sessionId,
                                              QStringLiteral("blocked"),
                                              activeTurn,
                                              taskExtra);
        }
    }

    if (!agentId.isEmpty() && service.m_app.m_memoryService->heartbeatService())
        service.m_app.m_memoryService->heartbeatService()->unsuppressAgentHeartbeat(agentId);

    qint64 durationMs = -1;
    if (!toolId.isEmpty()) {
        const QString key = delegateToolKey(sessionId, toolId);
        if (service.m_delegateStartMsByToolKey.contains(key)) {
            durationMs = QDateTime::currentMSecsSinceEpoch()
                - service.m_delegateStartMsByToolKey.value(key);
            service.m_delegateStartMsByToolKey.remove(key);
        }
    }

    ConversationService::DelegateStats stats = service.m_delegateStatsBySession.value(sessionId);
    ++stats.totalCount;
    if (event.success)
        ++stats.successCount;
    else
        ++stats.failureCount;
    if (durationMs >= 0)
        stats.totalDurationMs += durationMs;
    service.m_delegateStatsBySession.insert(sessionId, stats);

    QJsonObject delegateExtra;
    delegateExtra.insert(QStringLiteral("toolName"), toolName);
    delegateExtra.insert(QStringLiteral("toolId"), toolId);
    delegateExtra.insert(QStringLiteral("event"), QStringLiteral("completed"));
    delegateExtra.insert(QStringLiteral("success"), event.success);
    if (durationMs >= 0)
        delegateExtra.insert(QStringLiteral("durationMs"), static_cast<double>(durationMs));
    delegateExtra.insert(QStringLiteral("delegate_total"), stats.totalCount);
    delegateExtra.insert(QStringLiteral("delegate_success"), stats.successCount);
    delegateExtra.insert(QStringLiteral("delegate_failed"), stats.failureCount);
    const int measuredCount = stats.successCount + stats.failureCount;
    if (measuredCount > 0 && stats.totalDurationMs > 0) {
        delegateExtra.insert(QStringLiteral("delegate_avg_duration_ms"),
                             static_cast<double>(stats.totalDurationMs) / measuredCount);
    }
    if (!event.formattedResult.trimmed().isEmpty())
        delegateExtra.insert(QStringLiteral("summary"), event.formattedResult.trimmed());

    const auto copyDelegateMetric = [&](const QString& key) {
        if (event.data.contains(key))
            delegateExtra.insert(key, event.data.value(key));
    };

    const QString childRequestId =
        event.data.value(QStringLiteral("child_request_id")).toString().trimmed();
    if (!childRequestId.isEmpty())
        delegateExtra.insert(QStringLiteral("child_request_id"), childRequestId);
    const QString childTraceId =
        event.data.value(QStringLiteral("child_trace_id")).toString().trimmed();
    if (!childTraceId.isEmpty())
        delegateExtra.insert(QStringLiteral("child_trace_id"), childTraceId);
    const QString childAgentId =
        event.data.value(QStringLiteral("child_agent_id")).toString().trimmed();
    if (!childAgentId.isEmpty())
        delegateExtra.insert(QStringLiteral("child_agent_id"), childAgentId);
    const QString childModel =
        event.data.value(QStringLiteral("child_model")).toString().trimmed();
    if (!childModel.isEmpty())
        delegateExtra.insert(QStringLiteral("child_model"), childModel);
    const QString childFinishReason =
        event.data.value(QStringLiteral("child_finish_reason")).toString().trimmed();
    if (!childFinishReason.isEmpty())
        delegateExtra.insert(QStringLiteral("child_finish_reason"), childFinishReason);
    const QString failureReason =
        event.data.value(QStringLiteral("failure_reason")).toString().trimmed();
    if (!failureReason.isEmpty())
        delegateExtra.insert(QStringLiteral("failure_reason"), failureReason);

    copyDelegateMetric(QStringLiteral("child_duration_ms"));
    copyDelegateMetric(QStringLiteral("child_timeout_ms"));
    copyDelegateMetric(QStringLiteral("max_response_chars"));
    copyDelegateMetric(QStringLiteral("restrict_delegation"));
    copyDelegateMetric(QStringLiteral("inherited_allowed_tools_count"));
    copyDelegateMetric(QStringLiteral("child_io_entries"));
    copyDelegateMetric(QStringLiteral("child_last_request_messages_count"));
    copyDelegateMetric(QStringLiteral("child_finish_reason"));
    copyDelegateMetric(QStringLiteral("child_response_tool_call_batches"));
    copyDelegateMetric(QStringLiteral("child_response_tool_call_total"));
    copyDelegateMetric(QStringLiteral("child_tool_started_count"));
    copyDelegateMetric(QStringLiteral("child_tool_progress_count"));
    copyDelegateMetric(QStringLiteral("child_tool_completed_count"));
    copyDelegateMetric(QStringLiteral("child_tool_success_count"));
    copyDelegateMetric(QStringLiteral("child_tool_failure_count"));
    copyDelegateMetric(QStringLiteral("child_stream_chunk_count"));
    copyDelegateMetric(QStringLiteral("child_stream_chars"));
    copyDelegateMetric(QStringLiteral("child_timeline_dropped"));
    copyDelegateMetric(QStringLiteral("child_error"));
    copyDelegateMetric(QStringLiteral("child_tools"));
    copyDelegateMetric(QStringLiteral("child_timeline"));

    const QString eventType = event.success ? QStringLiteral("delegate.tool_completed")
                                            : QStringLiteral("delegate.tool_failed");
    service.emitPipelineEvent(eventType,
                              sessionId,
                              activeTurn,
                              QString(),
                              event.success ? QString() : event.rawResult.left(300),
                              delegateExtra,
                              true);
}

void ConversationRuntimeEventsAccess::handleToolPersistence(ConversationService& service,
                                                            const QString& sessionId,
                                                            const QString& agentId,
                                                            const TurnTask* activeTurn,
                                                            const ToolExecutionEvent& event)
{
    const QString toolName = event.toolName.trimmed();
    const QString toolId = event.toolId.trimmed();
    const bool isDelegateTool = (toolName == QLatin1String("delegate_task"));

    if (!agentId.isEmpty()) {
        if (event.status == QLatin1String("started")) {
            if (toolId.isEmpty()) {
                qWarning() << "[ConversationService] 跳过无效 tool_call 事件：缺少 toolId，session="
                           << sessionId << "toolName=" << toolName;
            } else {
                Message toolCallMsg =
                    Message::createToolCall(sessionId, agentId, QString(), QJsonObject());
                toolCallMsg.content.text.clear();
                toolCallMsg.traceId = activeTurn->requestTraceId;
                toolCallMsg.turnId = activeTurn->turnId;
                toolCallMsg.status = Message::Status::Completed;
                toolCallMsg.content.payload.insert(QStringLiteral("tool_name"), toolName);
                toolCallMsg.content.payload.insert(QStringLiteral("tool_call_id"), toolId);
                toolCallMsg.content.payload.insert(
                    QStringLiteral("arguments"),
                    sanitizePersistedToolArguments(toolName, event.data));
                service.m_app.m_sessionManager->postMessage(sessionId, toolCallMsg);
            }
        } else if (event.status == QLatin1String("completed")) {
            const bool isPseudoToolEvent = (toolName == QLatin1String("tool_loop_guard"));
            if (toolId.isEmpty() || isPseudoToolEvent) {
                qWarning() << "[ConversationService] 跳过无效 tool_result 事件：session="
                           << sessionId << "toolName=" << toolName << "toolId=" << toolId;
            } else {
                Message toolResultMsg =
                    Message::createToolResult(sessionId, agentId, toolId, QString());
                toolResultMsg.content.text.clear();
                toolResultMsg.traceId = activeTurn->requestTraceId;
                toolResultMsg.turnId = activeTurn->turnId;
                toolResultMsg.status = Message::Status::Completed;
                toolResultMsg.content.payload.insert(QStringLiteral("tool_name"), toolName);
                toolResultMsg.content.payload.insert(QStringLiteral("success"), event.success);
                toolResultMsg.content.payload.insert(
                    QStringLiteral("raw_result"),
                    sanitizePersistedToolRawResult(toolName, event.rawResult));
                toolResultMsg.content.payload.insert(QStringLiteral("formatted_result"),
                                                     event.formattedResult);
                const QJsonObject persistedEventData =
                    sanitizePersistedToolEventData(toolName, event.data);
                if (!persistedEventData.isEmpty())
                    toolResultMsg.content.payload.insert(QStringLiteral("event_data"),
                                                         persistedEventData);
                if (isDelegateTool) {
                    const QString childRequestId =
                        event.data.value(QStringLiteral("child_request_id")).toString().trimmed();
                    if (!childRequestId.isEmpty()) {
                        toolResultMsg.content.payload.insert(QStringLiteral("child_request_id"),
                                                             childRequestId);
                    }
                    const QString childTraceId =
                        event.data.value(QStringLiteral("child_trace_id")).toString().trimmed();
                    if (!childTraceId.isEmpty()) {
                        toolResultMsg.content.payload.insert(QStringLiteral("child_trace_id"),
                                                             childTraceId);
                    }
                    const QString childAgentId =
                        event.data.value(QStringLiteral("child_agent_id")).toString().trimmed();
                    if (!childAgentId.isEmpty()) {
                        toolResultMsg.content.payload.insert(QStringLiteral("child_agent_id"),
                                                             childAgentId);
                    }
                    const QString childModel =
                        event.data.value(QStringLiteral("child_model")).toString().trimmed();
                    if (!childModel.isEmpty()) {
                        toolResultMsg.content.payload.insert(QStringLiteral("child_model"),
                                                             childModel);
                    }
                    const QString failureReason =
                        event.data.value(QStringLiteral("failure_reason")).toString().trimmed();
                    if (!failureReason.isEmpty()) {
                        toolResultMsg.content.payload.insert(QStringLiteral("failure_reason"),
                                                             failureReason);
                    }
                }
                service.m_app.m_sessionManager->postMessage(sessionId, toolResultMsg);
            }
        }
    }

    if (toolName == QLatin1String("send_file")
        && event.status == QLatin1String("completed")
        && event.success
        && !event.data.isEmpty()) {
        const QString tmpFilePath = event.data.value(QStringLiteral("file_path")).toString();
        const QString fileName = event.data.value(QStringLiteral("file_name")).toString();
        const qint64 fileSize =
            static_cast<qint64>(event.data.value(QStringLiteral("file_size")).toDouble());
        const QString description = event.data.value(QStringLiteral("description")).toString();
        if (!tmpFilePath.isEmpty() && !fileName.isEmpty() && !agentId.isEmpty()) {
            QString finalFilePath = tmpFilePath;
            const QString sessionDir = service.m_app.m_persistence
                ? service.m_app.m_persistence->sessionDataDirPath(sessionId)
                : QString();
            if (!sessionDir.isEmpty()) {
                const QString filesDir =
                    sessionDir + QStringLiteral("/files/")
                    + QUuid::createUuid().toString(QUuid::WithoutBraces);
                QDir().mkpath(filesDir);
                const QString destPath = QDir(filesDir).filePath(fileName);
                if (QFile::rename(tmpFilePath, destPath)) {
                    finalFilePath = destPath;
                    QDir tmpDir(QFileInfo(tmpFilePath).absolutePath());
                    tmpDir.removeRecursively();
                }
            }
            Message fileMsg =
                Message::createFile(sessionId, agentId, finalFilePath, fileName, fileSize, description);
            fileMsg.traceId = activeTurn->requestTraceId;
            fileMsg.turnId = activeTurn->turnId;
            service.m_app.m_sessionManager->postMessage(sessionId, fileMsg);
        }
    }

    emit service.m_app.toolEvent(sessionId, event);

    bool persistToolEvent = true;
    QJsonObject eventObj = toolEventToJson(event);
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
        const qint64 lastMs = service.m_toolProgressLastPersistMsByKey.value(progressKey, 0);
        const QString lastDigest = service.m_toolProgressLastDigestByKey.value(progressKey);
        const bool due =
            (lastMs <= 0) || ((nowMs - lastMs) >= kToolProgressPersistMinIntervalMs);
        const bool changed = (!progressDigest.isEmpty() && progressDigest != lastDigest);
        persistToolEvent = due || changed;

        if (persistToolEvent) {
            service.m_toolProgressLastPersistMsByKey.insert(progressKey, nowMs);
            service.m_toolProgressLastDigestByKey.insert(progressKey, progressDigest);
        }

        QString clippedFormatted = event.formattedResult;
        if (clippedFormatted.size() > 320)
            clippedFormatted = clippedFormatted.left(320) + QStringLiteral("...");
        eventObj.insert(QStringLiteral("formattedResult"), clippedFormatted);
        eventObj.insert(QStringLiteral("rawResult"), QString());
    }

    QJsonObject extra;
    extra.insert(QStringLiteral("toolEvent"), eventObj);
    service.emitPipelineEvent(QStringLiteral("turn_tool_event"),
                              sessionId,
                              activeTurn,
                              QString(),
                              QString(),
                              extra,
                              persistToolEvent);
}
