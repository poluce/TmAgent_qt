#include "ConversationService.h"

#include "ApplicationServices.h"
#include "GovernanceService.h"
#include "MemoryService.h"
#include "WorkspaceService.h"
#include "AgentRuntime.h"
#include "ChatCoordinatorSupport.h"
#include "AgentPulseRegistry.h"
#include "RuntimeManager.h"
#include "TaskStateService.h"
#include "ChatStateRepository.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "llm/ModelFactory.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

namespace {
using ChatCoordinatorSupport::delegateToolKey;
using ChatCoordinatorSupport::estimateHistoryChars;
using ChatCoordinatorSupport::taskStateTextPreview;

constexpr int kSoftQueueDepth = 10;
constexpr int kHardQueueDepth = 200;
constexpr int kQueueMergeWindowMs = 2500;
constexpr int kQueueMergeMaxMergedMessages = 4;
constexpr int kQueueMergeMaxChars = 12000;
constexpr int kHistoryMaxMessages = 120;
constexpr int kHistoryMaxChars = 32000;
constexpr int kHistoryToolResultMaxChars = 3000;
constexpr int kMemoryContextMaxChars = 4500;
constexpr int kDeltaBatchFlushIntervalMs = 400;
constexpr int kDeltaBatchFlushChars = 120;
constexpr int kDeltaBatchFlushChunks = 20;
constexpr qint64 kToolProgressPersistMinIntervalMs = 1200;
bool shouldMirrorEventToIoHistory(const QString& type)
{
    if (type.startsWith(QStringLiteral("memory.")) || type.startsWith(QStringLiteral("delegate.")))
        return true;
    return type == QLatin1String("turn_started")
        || type == QLatin1String("turn_dispatch_prepare")
        || type == QLatin1String("turn_dispatch_config_applied")
        || type == QLatin1String("turn_dispatch_sent")
        || type == QLatin1String("turn_tool_calls_started")
        || type == QLatin1String("turn_completed")
        || type == QLatin1String("turn_failed")
        || type == QLatin1String("turn_recovered")
        || type == QLatin1String("context.compacted")
        || type == QLatin1String("task_state.updated");
}

QString toolNamesFromToolCalls(const QJsonArray& toolCalls)
{
    QStringList names;
    for (const QJsonValue& value : toolCalls) {
        const QJsonObject toolCall = value.toObject();
        const QString name = toolCall.value(QStringLiteral("function"))
                                 .toObject()
                                 .value(QStringLiteral("name"))
                                 .toString()
                                 .trimmed();
        if (!name.isEmpty())
            names.append(name);
    }
    names.removeDuplicates();
    return names.join(QStringLiteral(", "));
}

QString historySnippetForSummary(const QJsonObject& msg, int maxChars)
{
    const QString role = msg.value(QStringLiteral("role")).toString();
    if (role == QLatin1String("tool")) {
        const QString toolId = msg.value(QStringLiteral("tool_call_id")).toString().trimmed();
        return QStringLiteral("- [tool] result for %1")
            .arg(toolId.isEmpty() ? QStringLiteral("unknown-tool-call") : toolId);
    }

    if (role == QLatin1String("assistant") && msg.contains(QStringLiteral("tool_calls"))) {
        const QString names = toolNamesFromToolCalls(msg.value(QStringLiteral("tool_calls")).toArray());
        return QStringLiteral("- [assistant] requested tools: %1")
            .arg(names.isEmpty() ? QStringLiteral("unknown") : names);
    }

    QString content = msg.value(QStringLiteral("content")).toString();
    content.replace(QLatin1Char('\r'), QLatin1Char(' '));
    content.replace(QLatin1Char('\n'), QLatin1Char(' '));
    content = content.simplified();
    if (content.isEmpty())
        return QString();
    if (maxChars > 0 && content.size() > maxChars)
        content = content.left(maxChars) + QStringLiteral("...");
    return QStringLiteral("- [%1] %2").arg(role, content);
}

QJsonArray compactHistoryWithBudget(const QJsonArray& history, int maxMessages, int maxChars)
{
    if (history.isEmpty())
        return history;

    const int safeMaxMessages = qMax(2, maxMessages);
    const int safeMaxChars = qMax(2048, maxChars);
    const int originalChars = estimateHistoryChars(history);
    if (history.size() <= safeMaxMessages && originalChars <= safeMaxChars)
        return history;

    QJsonArray kept = history;
    QJsonArray removed;

    const int targetMessagesWithoutSummary = qMax(1, safeMaxMessages - 1);
    const int targetCharsWithoutSummary = qMax(1200, safeMaxChars - 1400);
    while (!kept.isEmpty()) {
        if (kept.size() <= targetMessagesWithoutSummary
            && estimateHistoryChars(kept) <= targetCharsWithoutSummary) {
            break;
        }
        removed.append(kept.takeAt(0));
    }

    while (!kept.isEmpty()
           && kept.first().toObject().value(QStringLiteral("role")).toString()
               == QLatin1String("tool")) {
        removed.append(kept.takeAt(0));
    }

    if (kept.isEmpty() && !history.isEmpty())
        kept.append(history.last());

    QStringList highlights;
    int highlightChars = 0;
    const int kMaxHighlights = 10;
    const int kMaxHighlightChars = 1400;
    for (const QJsonValue& value : removed) {
        if (highlights.size() >= kMaxHighlights)
            break;
        const QString snippet = historySnippetForSummary(value.toObject(), 160);
        if (snippet.isEmpty())
            continue;
        if (highlightChars + snippet.size() > kMaxHighlightChars)
            break;
        highlights.append(snippet);
        highlightChars += snippet.size();
    }

    QJsonObject summaryMsg;
    summaryMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
    QString summary = QStringLiteral(
                          "[Context Compact]\n"
                          "Earlier %1 messages were compacted to keep this turn within context budget.\n")
                          .arg(removed.size());
    if (!highlights.isEmpty()) {
        summary += QStringLiteral("Key highlights:\n");
        summary += highlights.join(QStringLiteral("\n"));
    }
    summaryMsg.insert(QStringLiteral("content"), summary.trimmed());

    QJsonArray compacted;
    compacted.append(summaryMsg);
    for (const QJsonValue& value : kept)
        compacted.append(value);

    while (compacted.size() > safeMaxMessages || estimateHistoryChars(compacted) > safeMaxChars) {
        if (compacted.size() <= 2)
            break;
        compacted.removeAt(1);
        while (compacted.size() > 1
               && compacted.at(1).toObject().value(QStringLiteral("role")).toString()
                   == QLatin1String("tool")) {
            compacted.removeAt(1);
        }
    }

    return compacted;
}

} // namespace

ConversationService::ConversationService(ApplicationServices& app)
    : m_app(app)
    , m_runtimeManager(new RuntimeManager(&app))
    , m_taskStateService(new TaskStateService())
{
}

ConversationService::~ConversationService() = default;

bool ConversationService::isSessionStreaming(const QString& sessionId) const
{
    if (m_turnManager.hasActiveTurn(sessionId))
        return true;
    Session* session = m_app.m_sessionManager ? m_app.m_sessionManager->findById(sessionId) : nullptr;
    return session && session->isStreaming();
}

int ConversationService::pendingTurnCount(const QString& sessionId) const
{
    return m_turnManager.queuedTurnCount(sessionId);
}

QString ConversationService::activeRunId(const QString& sessionId) const
{
    const TurnTask* active = m_turnManager.activeTurn(sessionId);
    return active ? active->runId : QString();
}

QJsonObject ConversationService::taskStateForSession(const QString& sessionId) const
{
    return m_taskStateService->stateForSession(sessionId);
}

QString ConversationService::runtimeIdentityIdForSession(const QString& sessionId) const
{
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (!runtime)
        return QString();
    const QString runtimeIdentityId = runtime->identityId().trimmed();
    if (!runtimeIdentityId.isEmpty())
        return runtimeIdentityId;
    return runtime->identity() ? runtime->identity()->id() : QString();
}

QJsonArray ConversationService::ioHistoryForSession(const QString& sessionId) const
{
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime && runtime->currentSessionId() == sessionId)
        return runtime->getIoHistory();
    return QJsonArray();
}

QString ConversationService::modelDisplayName(const LLMConfig& config) const
{
    if (!config.isValid())
        return QStringLiteral("默认模型");
    if (m_app.m_governanceService && m_app.m_governanceService->modelFactory()) {
        const QString modelInfo =
            m_app.m_governanceService->modelFactory()->resolveModelId(config).trimmed();
        return modelInfo.isEmpty() ? QStringLiteral("未指定模型") : modelInfo;
    }
    const QString modelInfo = config.selectedModelId.trimmed();
    return modelInfo.isEmpty() ? QStringLiteral("未指定模型") : modelInfo;
}

bool ConversationService::renameSessionAndRuntime(const QString& sessionId, const QString& name)
{
    const QString trimmedSessionId = sessionId.trimmed();
    const QString trimmedName = name.trimmed();
    if (trimmedSessionId.isEmpty())
        return false;

    Session* session = m_app.m_sessionManager ? m_app.m_sessionManager->findById(trimmedSessionId)
                                              : nullptr;
    if (session)
        session->setTitle(trimmedName);

    AgentRuntime* runtime = runtimeForSession(trimmedSessionId);
    if (runtime && runtime->identity()) {
        runtime->identity()->setName(trimmedName);
        LLMConfig cfg = runtime->config();
        cfg.userName = trimmedName;
        runtime->setConfig(cfg);
    }
    return true;
}

void ConversationService::clearConversationHistory(const QString& sessionId)
{
    const QString trimmedSessionId = sessionId.trimmed();
    if (trimmedSessionId.isEmpty())
        return;

    Session* session = m_app.m_sessionManager ? m_app.m_sessionManager->findById(trimmedSessionId)
                                              : nullptr;
    if (session)
        session->clearMessages();

    AgentRuntime* runtime = runtimeForSession(trimmedSessionId);
    if (runtime && runtime->currentSessionId() == trimmedSessionId)
        runtime->clearHistory();

}

void ConversationService::deliverHeartbeatSummary(const QString& agentId,
                                                  const QString& summary,
                                                  const QJsonObject& metadata)
{
    if (!m_app.m_sessionManager || !m_app.m_memoryService)
        return;
    const QString trimmedAgentId = agentId.trimmed();
    const QString trimmedSummary = summary.trimmed();
    if (trimmedAgentId.isEmpty() || trimmedSummary.isEmpty())
        return;

    const QString sessionId =
        m_app.m_memoryService->resolvePrimarySessionForAgent(trimmedAgentId, true, false, QString());
    if (sessionId.trimmed().isEmpty())
        return;

    Message assistantMsg = Message::createText(sessionId, trimmedAgentId, trimmedSummary);
    assistantMsg.status = Message::Status::Completed;
    assistantMsg.traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    assistantMsg.turnId = QStringLiteral("heartbeat-summary");
    m_app.m_sessionManager->postMessage(sessionId, assistantMsg);

    QJsonObject extra = metadata;
    extra.insert(QStringLiteral("agent_id"), trimmedAgentId);
    extra.insert(QStringLiteral("summary"), trimmedSummary.left(200));
    emitPipelineEvent(QStringLiteral("heartbeat.summary_delivered"),
                      sessionId,
                      nullptr,
                      QString(),
                      QString(),
                      extra,
                      true);
}

RuntimeManager* ConversationService::runtimeManager() const { return m_runtimeManager; }
TaskStateService* ConversationService::taskStateService() const { return m_taskStateService.get(); }
TurnManager& ConversationService::turnManager() { return m_turnManager; }
const TurnManager& ConversationService::turnManager() const { return m_turnManager; }
QHash<QString, QString>& ConversationService::activeSessionByAgent() { return m_agentActiveSession; }
const QHash<QString, QString>& ConversationService::activeSessionByAgent() const { return m_agentActiveSession; }
QHash<QString, qint64>& ConversationService::delegateStartMsByToolKey() { return m_delegateStartMsByToolKey; }
const QHash<QString, qint64>& ConversationService::delegateStartMsByToolKey() const { return m_delegateStartMsByToolKey; }
QHash<QString, ConversationService::DelegateStats>& ConversationService::delegateStatsBySession() { return m_delegateStatsBySession; }
const QHash<QString, ConversationService::DelegateStats>& ConversationService::delegateStatsBySession() const { return m_delegateStatsBySession; }
QHash<QString, qint64>& ConversationService::toolProgressLastPersistMsByKey() { return m_toolProgressLastPersistMsByKey; }
const QHash<QString, qint64>& ConversationService::toolProgressLastPersistMsByKey() const { return m_toolProgressLastPersistMsByKey; }
QHash<QString, QString>& ConversationService::toolProgressLastDigestByKey() { return m_toolProgressLastDigestByKey; }
const QHash<QString, QString>& ConversationService::toolProgressLastDigestByKey() const { return m_toolProgressLastDigestByKey; }
SessionPipeline& ConversationService::ensurePipeline(const QString& sessionId)
{
    return m_turnManager.ensurePipeline(sessionId);
}

SessionPipeline* ConversationService::findPipeline(const QString& sessionId)
{
    return m_turnManager.findPipeline(sessionId);
}

const SessionPipeline* ConversationService::findPipeline(const QString& sessionId) const
{
    return m_turnManager.findPipeline(sessionId);
}

QString ConversationService::agentIdentityIdForSession(const QString& sessionId) const
{
    if (!m_app.m_sessionManager || !m_app.m_identityManager || sessionId.trimmed().isEmpty())
        return QString();

    Session* session = m_app.m_sessionManager->findById(sessionId);
    if (!session)
        return QString();

    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_app.m_identityManager->findById(pid);
        if (identity && identity->isAgent())
            return identity->id();
    }
    return QString();
}

Identity* ConversationService::findOrCreateAgentIdentity(Session* session)
{
    if (!session || !m_app.m_identityManager)
        return nullptr;

    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_app.m_identityManager->findById(pid);
        if (identity && identity->isAgent())
            return identity;
    }

    auto* profile = new IdentityProfile();
    const LLMConfig defaultCfg = m_runtimeManager->defaultAgentConfig();
    profile->setLlmConfig(defaultCfg);
    profile->setSystemPrompt(defaultCfg.systemPrompt);
    profile->setDelegateEnabled(true);
    profile->setAllowedTools(ChatStateRepository::collectToolNamesFrom(
        m_app.m_governanceService ? m_app.m_governanceService->toolDispatcher() : nullptr));
    Identity* agentIdentity = m_app.m_identityManager->createAgent(
        session->title().isEmpty() ? QStringLiteral("TM Agent") : session->title(), profile);
    if (m_app.m_memoryService)
        m_app.m_memoryService->ensureMemoryInitializedForAgent(agentIdentity);
    session->addParticipant(agentIdentity->id());
    return agentIdentity;
}

AgentRuntime* ConversationService::runtimeForSession(const QString& sessionId) const
{
    const QString agentId = agentIdentityIdForSession(sessionId);
    if (agentId.isEmpty() || !m_runtimeManager)
        return nullptr;
    return m_runtimeManager->runtimeForAgent(agentId);
}

AgentRuntime* ConversationService::ensureRuntimeForSession(const QString& sessionId)
{
    if (!m_app.m_sessionManager)
        return nullptr;
    Session* session = m_app.m_sessionManager->findById(sessionId);
    if (!session)
        return nullptr;

    Identity* agentIdentity = findOrCreateAgentIdentity(session);
    AgentRuntime* runtime = ensureRuntimeForAgent(agentIdentity);
    if (!runtime)
        return nullptr;

    const QString activeSessionId = m_agentActiveSession.value(agentIdentity->id());
    if (activeSessionId.isEmpty() || activeSessionId == sessionId) {
        runtime->switchToSession(sessionId);
        runtime->setHistory(buildRuntimeHistoryFromMessages(session));
    }
    return runtime;
}

AgentRuntime* ConversationService::ensureRuntimeForAgent(Identity* agentIdentity)
{
    return m_runtimeManager ? m_runtimeManager->ensureRuntimeForAgent(agentIdentity) : nullptr;
}

void ConversationService::releaseRuntimeIfUnused(const QString& agentIdentityId)
{
    if (m_runtimeManager)
        m_runtimeManager->releaseRuntimeIfUnused(agentIdentityId);
    m_agentActiveSession.remove(agentIdentityId);
}

LLMConfig ConversationService::composeConfigForIdentity(Identity* identity) const
{
    return m_runtimeManager ? m_runtimeManager->composeConfigForIdentity(identity) : LLMConfig();
}

QJsonArray ConversationService::buildRuntimeHistoryFromMessages(Session* session) const
{
    QJsonArray history;
    if (!session)
        return history;

    QSet<QString> validToolCallIds;
    const QList<Message> messages = session->allMessages();
    for (const Message& msg : messages) {
        if (msg.status == Message::Status::Cancelled || msg.status == Message::Status::Interrupted
            || msg.status == Message::Status::Error) {
            continue;
        }

        const QString content = msg.content.text;
        const QString trimmedContent = content.trimmed();

        if (msg.content.type == MessageContent::Type::TeammateReply) {
            QString rawContent =
                msg.content.payload.value(QStringLiteral("raw_content")).toString();
            if (rawContent.trimmed().isEmpty())
                rawContent = content;
            if (rawContent.trimmed().isEmpty())
                continue;
            if (rawContent.size() > kHistoryToolResultMaxChars) {
                rawContent = rawContent.left(kHistoryToolResultMaxChars)
                    + QStringLiteral("\n...[teammate reply truncated]...");
            }

            const QString teammateName =
                msg.content.payload.value(QStringLiteral("teammate_name")).toString().trimmed();
            const QString statusText =
                msg.content.payload.value(QStringLiteral("status")).toString().trimmed();
            const QString threadId =
                msg.content.payload.value(QStringLiteral("thread_id")).toString().trimmed();

            QStringList lines;
            lines << QStringLiteral("[TeammateReply]")
                  << QStringLiteral("name: %1").arg(teammateName.isEmpty() ? QStringLiteral("(unknown)") : teammateName);
            if (!statusText.isEmpty())
                lines << QStringLiteral("status: %1").arg(statusText);
            if (!threadId.isEmpty())
                lines << QStringLiteral("thread_id: %1").arg(threadId);
            lines << QStringLiteral("content:\n%1").arg(rawContent);

            QJsonObject item;
            item.insert(QStringLiteral("role"), QStringLiteral("system"));
            item.insert(QStringLiteral("content"), lines.join(QStringLiteral("\n")));
            history.append(item);
            continue;
        }

        if (msg.content.type == MessageContent::Type::System
            || msg.senderId == QLatin1String("system")) {
            if (trimmedContent.isEmpty())
                continue;
            QJsonObject item;
            item.insert(QStringLiteral("role"), QStringLiteral("system"));
            item.insert(QStringLiteral("content"), content);
            history.append(item);
            continue;
        }

        if (msg.content.type == MessageContent::Type::ToolCall) {
            QJsonObject item;
            item.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            if (!trimmedContent.isEmpty())
                item.insert(QStringLiteral("content"), content);

            QJsonArray toolCalls = msg.content.payload.value(QStringLiteral("tool_calls")).toArray();
            if (toolCalls.isEmpty()) {
                QString toolName =
                    msg.content.payload.value(QStringLiteral("tool_name")).toString().trimmed();
                if (toolName.isEmpty())
                    toolName = trimmedContent;
                if (!toolName.isEmpty()) {
                    QJsonObject args = msg.content.payload.value(QStringLiteral("arguments")).toObject();
                    if (args.isEmpty())
                        args = msg.content.payload;
                    args.remove(QStringLiteral("tool_calls"));
                    args.remove(QStringLiteral("tool_name"));
                    args.remove(QStringLiteral("tool_call_id"));
                    args.remove(QStringLiteral("arguments"));

                    QJsonObject functionObj;
                    functionObj.insert(QStringLiteral("name"), toolName);
                    functionObj.insert(
                        QStringLiteral("arguments"),
                        QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));

                    QJsonObject toolCallObj;
                    toolCallObj.insert(
                        QStringLiteral("id"),
                        msg.content.payload.value(QStringLiteral("tool_call_id"))
                                .toString()
                                .trimmed()
                                .isEmpty()
                            ? msg.id
                            : msg.content.payload.value(QStringLiteral("tool_call_id")).toString().trimmed());
                    toolCallObj.insert(QStringLiteral("type"), QStringLiteral("function"));
                    toolCallObj.insert(QStringLiteral("function"), functionObj);
                    toolCalls.append(toolCallObj);
                }
            }

            if (!toolCalls.isEmpty())
                item.insert(QStringLiteral("tool_calls"), toolCalls);
            for (const QJsonValue& v : toolCalls) {
                const QString toolCallId = v.toObject().value(QStringLiteral("id")).toString().trimmed();
                if (!toolCallId.isEmpty())
                    validToolCallIds.insert(toolCallId);
            }
            if (item.contains(QStringLiteral("content")) || item.contains(QStringLiteral("tool_calls")))
                history.append(item);
            continue;
        }

        if (msg.content.type == MessageContent::Type::ToolResult) {
            QString toolContent = content;
            if (toolContent.trimmed().isEmpty())
                toolContent = msg.content.payload.value(QStringLiteral("raw_result")).toString();
            if (toolContent.trimmed().isEmpty())
                continue;
            if (toolContent.size() > kHistoryToolResultMaxChars) {
                toolContent = toolContent.left(kHistoryToolResultMaxChars)
                    + QStringLiteral("\n...[tool result truncated]...");
            }

            QString toolCallId = msg.content.payload.value(QStringLiteral("tool_call_id")).toString().trimmed();
            if (toolCallId.isEmpty())
                toolCallId = msg.content.payload.value(QStringLiteral("id")).toString().trimmed();
            if (toolCallId.isEmpty())
                continue;
            if (!validToolCallIds.contains(toolCallId))
                continue;

            QJsonObject item;
            item.insert(QStringLiteral("role"), QStringLiteral("tool"));
            item.insert(QStringLiteral("tool_call_id"), toolCallId);
            item.insert(QStringLiteral("content"), toolContent);
            history.append(item);
            continue;
        }

        if (trimmedContent.isEmpty())
            continue;

        QJsonObject item;
        Identity* sender = m_app.m_identityManager ? m_app.m_identityManager->findById(msg.senderId)
                                                   : nullptr;
        const bool isUser = sender && sender->isUser();
        item.insert(QStringLiteral("role"), isUser ? QStringLiteral("user") : QStringLiteral("assistant"));
        item.insert(QStringLiteral("content"), content);
        history.append(item);
    }

    QJsonArray sanitized;
    for (int i = 0; i < history.size(); ++i) {
        QJsonObject msg = history[i].toObject();
        sanitized.append(msg);

        if (msg.value(QStringLiteral("role")).toString() != QLatin1String("assistant")
            || !msg.contains(QStringLiteral("tool_calls"))) {
            continue;
        }

        QJsonArray toolCalls = msg[QStringLiteral("tool_calls")].toArray();
        QSet<QString> expectedIds;
        for (const QJsonValue& tc : toolCalls)
            expectedIds.insert(tc.toObject()[QStringLiteral("id")].toString());

        QSet<QString> foundIds;
        for (int j = i + 1; j < history.size(); ++j) {
            QJsonObject next = history[j].toObject();
            const QString nextRole = next.value(QStringLiteral("role")).toString();
            if (nextRole == QLatin1String("tool")) {
                foundIds.insert(next[QStringLiteral("tool_call_id")].toString());
            } else if (nextRole == QLatin1String("assistant")
                       && next.contains(QStringLiteral("tool_calls"))) {
                break;
            }
        }

        for (const QString& id : expectedIds) {
            if (!foundIds.contains(id)) {
                QJsonObject placeholder;
                placeholder[QStringLiteral("role")] = QStringLiteral("tool");
                placeholder[QStringLiteral("tool_call_id")] = id;
                placeholder[QStringLiteral("content")] = QStringLiteral("[工具结果不可用]");
                sanitized.append(placeholder);
            }
        }
    }
    return compactHistoryWithBudget(sanitized, kHistoryMaxMessages, kHistoryMaxChars);
}

void ConversationService::resetSessionStreamState(const QString& sessionId)
{
    if (!m_app.m_sessionManager)
        return;
    Session* session = m_app.m_sessionManager->findById(sessionId);
    if (!session)
        return;
    Session::StreamState& state = session->streamState();
    state.isStreaming = false;
    state.buffer.clear();
    state.hasPendingMessage = false;
    state.lastMsgIsTool = false;
}

void ConversationService::flushPendingDeltaLog(const QString& sessionId,
                                               SessionPipeline* pipeline,
                                               const TurnTask* turn,
                                               bool force)
{
    if (m_app.m_logVerboseStreamEvents || !pipeline || pipeline->pendingDeltaLog.isEmpty())
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int charCount = pipeline->pendingDeltaLog.size();
    const int chunkCount = pipeline->pendingDeltaChunks;
    const qint64 spanMs =
        pipeline->pendingDeltaStartedAtMs > 0 ? (nowMs - pipeline->pendingDeltaStartedAtMs) : 0;

    if (!force) {
        const bool byChars = charCount >= kDeltaBatchFlushChars;
        const bool byChunks = chunkCount >= kDeltaBatchFlushChunks;
        const bool byInterval = pipeline->lastDeltaFlushedAtMs <= 0
            ? (spanMs >= kDeltaBatchFlushIntervalMs)
            : (nowMs - pipeline->lastDeltaFlushedAtMs >= kDeltaBatchFlushIntervalMs);
        if (!byChars && !byChunks && !byInterval)
            return;
    }

    QJsonObject extra;
    extra.insert(QStringLiteral("chunkCount"), chunkCount);
    extra.insert(QStringLiteral("charCount"), charCount);
    extra.insert(QStringLiteral("batched"), true);
    if (spanMs > 0)
        extra.insert(QStringLiteral("spanMs"), static_cast<double>(spanMs));

    emitPipelineEvent(
        QStringLiteral("turn_delta_batch"), sessionId, turn, pipeline->pendingDeltaLog, QString(), extra, true);

    pipeline->pendingDeltaLog.clear();
    pipeline->pendingDeltaChunks = 0;
    pipeline->pendingDeltaStartedAtMs = 0;
    pipeline->lastDeltaFlushedAtMs = nowMs;
}

bool ConversationService::appendEventLog(const QJsonObject& event) const
{
    return m_app.m_persistence && m_app.m_persistence->appendEventLog(event);
}

void ConversationService::emitPipelineEvent(const QString& type,
                                            const QString& sessionId,
                                            const TurnTask* turn,
                                            const QString& delta,
                                            const QString& error,
                                            const QJsonObject& extra,
                                            bool persistToDisk)
{
    SessionPipeline* pipeline = findPipeline(sessionId);

    QJsonObject event;
    event.insert(QStringLiteral("event_schema_version"), 1);
    event.insert(QStringLiteral("type"), type);
    event.insert(QStringLiteral("sessionId"), sessionId);
    event.insert(QStringLiteral("session_id"), sessionId);
    event.insert(QStringLiteral("timestamp"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (pipeline) {
        event.insert(QStringLiteral("seq"), static_cast<qint64>(++pipeline->seq));
        event.insert(QStringLiteral("queueDepth"), pipeline->queue.size());
        event.insert(QStringLiteral("hasActiveRun"), pipeline->hasActiveTurn);
    }

    if (turn) {
        if (!turn->requestTraceId.isEmpty())
            event.insert(QStringLiteral("trace_id"), turn->requestTraceId);
        event.insert(QStringLiteral("turnId"), turn->turnId);
        event.insert(QStringLiteral("runId"), turn->runId);
        event.insert(QStringLiteral("turn_id"), turn->turnId);
        event.insert(QStringLiteral("run_id"), turn->runId);
        if (!turn->actorIdentityId.isEmpty())
            event.insert(QStringLiteral("actorIdentityId"), turn->actorIdentityId);
        if (turn->enqueuedAtMs > 0)
            event.insert(QStringLiteral("enqueuedAtMs"), static_cast<double>(turn->enqueuedAtMs));
        if (turn->mergedMessageCount > 1)
            event.insert(QStringLiteral("mergedMessageCount"), turn->mergedMessageCount);
        if (!turn->clientMessageId.isEmpty())
            event.insert(QStringLiteral("clientMessageId"), turn->clientMessageId);
    }
    if (!delta.isEmpty())
        event.insert(QStringLiteral("delta"), delta);
    if (!error.isEmpty())
        event.insert(QStringLiteral("error"), error);

    for (auto it = extra.begin(); it != extra.end(); ++it)
        event.insert(it.key(), it.value());

    if (shouldMirrorEventToIoHistory(type))
        appendRuntimeIoEventEntry(sessionId, type, turn, error, extra);

    emit m_app.conversationEvent(event);
    if (persistToDisk && !appendEventLog(event)) {
        const QString logPath = m_app.m_persistence
            ? (DatabaseManager::instance()->isReady()
                   ? QStringLiteral("sqlite://events")
                   : QDir(m_app.m_persistence->dataRootPath()).filePath(
                         QStringLiteral("logs/events-current.jsonl")))
            : QStringLiteral("<persistence-unavailable>");
        qWarning() << "[ApplicationServices] 事件日志写入失败：" << logPath;
    }
}

void ConversationService::appendRuntimeIoEventEntry(const QString& sessionId,
                                                    const QString& type,
                                                    const TurnTask* turn,
                                                    const QString& error,
                                                    const QJsonObject& extra)
{
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (!runtime)
        return;

    const QString recordedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QJsonObject eventObj;
    eventObj.insert(QStringLiteral("type"), type);
    eventObj.insert(QStringLiteral("session_id"), sessionId);
    eventObj.insert(QStringLiteral("timestamp"), recordedAt);
    if (!error.isEmpty())
        eventObj.insert(QStringLiteral("error"), error);
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        eventObj.insert(it.key(), it.value());

    if (turn) {
        if (!turn->requestTraceId.isEmpty())
            eventObj.insert(QStringLiteral("trace_id"), turn->requestTraceId);
        if (!turn->turnId.isEmpty())
            eventObj.insert(QStringLiteral("turn_id"), turn->turnId);
        if (!turn->runId.isEmpty())
            eventObj.insert(QStringLiteral("run_id"), turn->runId);
    }

    QJsonObject entry;
    entry.insert(QStringLiteral("kind"), QStringLiteral("event"));
    entry.insert(QStringLiteral("recorded_at"), recordedAt);
    const QString requestId =
        turn ? QStringLiteral("event:%1:%2").arg(turn->runId.isEmpty() ? type : turn->runId, type)
             : QStringLiteral("event:%1").arg(type);
    entry.insert(QStringLiteral("request_id"), requestId);
    entry.insert(QStringLiteral("event"), eventObj);

    runtime->appendIoHistoryEntry(sessionId, entry);
}

void ConversationService::clearToolProgressCacheForSession(const QString& sessionId)
{
    const QString keyPrefix = sessionId.trimmed() + QStringLiteral("|");
    for (auto it = m_toolProgressLastPersistMsByKey.begin();
         it != m_toolProgressLastPersistMsByKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_toolProgressLastPersistMsByKey.erase(it);
        else
            ++it;
    }
    for (auto it = m_toolProgressLastDigestByKey.begin();
         it != m_toolProgressLastDigestByKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_toolProgressLastDigestByKey.erase(it);
        else
            ++it;
    }
}

void ConversationService::clearDelegateStartsForSession(const QString& sessionId)
{
    const QString keyPrefix = sessionId.trimmed() + QStringLiteral("|");
    for (auto it = m_delegateStartMsByToolKey.begin(); it != m_delegateStartMsByToolKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_delegateStartMsByToolKey.erase(it);
        else
            ++it;
    }
}


void ConversationService::updateTaskStateForSession(const QString& sessionId,
                                                    const QString& state,
                                                    const TurnTask* turn,
                                                    const QJsonObject& extra)
{
    if (sessionId.trimmed().isEmpty())
        return;

    QJsonObject patch = extra;
    patch.insert(QStringLiteral("state"), state);

    const QString agentId = agentIdentityIdForSession(sessionId).trimmed();
    if (!agentId.isEmpty())
        patch.insert(QStringLiteral("agent_id"), agentId);

    if (turn) {
        if (!turn->requestTraceId.trimmed().isEmpty())
            patch.insert(QStringLiteral("trace_id"), turn->requestTraceId.trimmed());
        if (!turn->turnId.trimmed().isEmpty())
            patch.insert(QStringLiteral("turn_id"), turn->turnId.trimmed());
        if (!turn->runId.trimmed().isEmpty())
            patch.insert(QStringLiteral("run_id"), turn->runId.trimmed());
    }

    QJsonObject mergedState;
    if (!m_taskStateService->updateState(sessionId, patch, &mergedState))
        return;

    emitPipelineEvent(QStringLiteral("task_state.updated"),
                      sessionId,
                      turn,
                      QString(),
                      QString(),
                      mergedState);
}

void ConversationService::clearTaskStateForSession(const QString& sessionId)
{
    if (sessionId.trimmed().isEmpty())
        return;
    m_taskStateService->clearState(sessionId);
}

void ConversationService::handleTeammateReply(const QString& teammateId,
                                              const QString& teammateName,
                                              bool success,
                                              const QString& content,
                                              const QString& threadId)
{
    const QString sessionId =
        m_app.m_workspaceService ? m_app.m_workspaceService->currentSessionIdValue() : QString();
    if (sessionId.isEmpty())
        return;

    const QString statusText = success ? QStringLiteral("completed") : QStringLiteral("failed");
    const QString storedContent =
        content.size() > 4000
            ? content.left(4000)
                  + QStringLiteral(
                        "\n...[truncated, total %1 chars, showing first 4000. Use message_teammate to request remaining content]...")
                        .arg(content.size())
            : content;

    Message teammateReply = Message::createTeammateReply(sessionId,
                                                         teammateId,
                                                         teammateName,
                                                         statusText,
                                                         storedContent,
                                                         threadId);
    m_app.m_sessionManager->postMessage(sessionId, teammateReply);
    enqueueTeammateReplyTurn(sessionId,
                             storedContent,
                             teammateReply.content.payload,
                             QStringLiteral("teammate-reply-%1").arg(teammateId));

    QJsonObject extra;
    extra.insert(QStringLiteral("teammate_id"), teammateId);
    extra.insert(QStringLiteral("teammate_name"), teammateName);
    extra.insert(QStringLiteral("success"), success);
    if (!threadId.trimmed().isEmpty())
        extra.insert(QStringLiteral("thread_id"), threadId.trimmed());
    emitPipelineEvent(QStringLiteral("teammate.replied"),
                      sessionId,
                      nullptr,
                      QString(),
                      QString(),
                      extra,
                      true);
}
