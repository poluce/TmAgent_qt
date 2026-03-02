#include "ChatService.h"
#include "AgentPulse.h"
#include "AgentRuntime.h"
#include "ConfigService.h"
#include "HealthMonitor.h"
#include "HeartbeatService.h"
#include "RuntimeManager.h"
#include "SchedulerService.h"
#include "core/agent/DelegateTaskScheduler.h"
#include "core/agent/LLMAgent.h"
#include "core/agent/McpToolProvider.h"
#include "core/agent/ToolDispatcher.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/memory/MemoryManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "core/service/ChatStateRepository.h"
#include "core/service/MessageRouter.h"
#include "core/utils/DefaultPrompts.h"
#include "core/utils/ModelConfigLoader.h"
#include "llm/LLMTypes.h"
#include "llm/ModelFactory.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace {
QJsonObject toolEventToJson(const ToolExecutionEvent& event)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("toolName"), event.toolName);
    obj.insert(QStringLiteral("toolId"), event.toolId);
    obj.insert(QStringLiteral("status"), event.status);
    obj.insert(QStringLiteral("success"), event.success);
    obj.insert(QStringLiteral("data"), event.data);
    obj.insert(QStringLiteral("rawResult"), event.rawResult);
    obj.insert(QStringLiteral("formattedResult"), event.formattedResult);
    return obj;
}

QString pulseStateToString(AgentPulse::State state)
{
    switch (state) {
    case AgentPulse::Healthy:
        return QStringLiteral("healthy");
    case AgentPulse::SoftTimeout:
        return QStringLiteral("soft_timeout");
    case AgentPulse::Stalled:
        return QStringLiteral("stalled");
    case AgentPulse::HardTimeout:
        return QStringLiteral("hard_timeout");
    case AgentPulse::Dead:
        return QStringLiteral("dead");
    }
    return QStringLiteral("unknown");
}

bool envFlagEnabled(const char* key)
{
    const QString raw = QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(key)).trimmed().toLower();
    return raw == QLatin1String("1")
        || raw == QLatin1String("true")
        || raw == QLatin1String("yes")
        || raw == QLatin1String("on");
}

bool shouldMirrorEventToIoHistory(const QString& type)
{
    return type.startsWith(QStringLiteral("memory."));
}

bool isTransientUpstreamError(const QString& errorMsg)
{
    const QString e = errorMsg.trimmed().toLower();
    if (e.isEmpty())
        return false;

    if (e.contains(QStringLiteral("bad request"))
        || e.contains(QStringLiteral("unauthorized"))
        || e.contains(QStringLiteral("forbidden"))
        || e.contains(QStringLiteral("not found"))
        || e.contains(QStringLiteral("invalid"))
        || e.contains(QStringLiteral("unprocessable"))) {
        return false;
    }

    return e.contains(QStringLiteral("internal server error"))
        || e.contains(QStringLiteral("bad gateway"))
        || e.contains(QStringLiteral("gateway timeout"))
        || e.contains(QStringLiteral("service unavailable"))
        || e.contains(QStringLiteral("server replied: 5"))
        || e.contains(QStringLiteral("connection reset"))
        || e.contains(QStringLiteral("connection closed"))
        || e.contains(QStringLiteral("temporarily unavailable"))
        || e.contains(QStringLiteral("network timeout"))
        || e.contains(QStringLiteral("timed out"));
}

QString buildDelegateRecoveryReply(const QList<DelegateTaskScheduler::JobInfo>& jobs)
{
    QStringList lines;
    lines << QStringLiteral("我这轮遇到了上游网络临时故障，但你发起的子代理任务还在后台继续执行。");
    lines << QStringLiteral("当前可跟踪任务：");

    const int maxPreview = qMin(3, jobs.size());
    for (int i = 0; i < maxPreview; ++i) {
        const DelegateTaskScheduler::JobInfo& job = jobs.at(i);
        const QString id = job.jobId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : job.jobId.trimmed();
        const QString status = job.status.trimmed().isEmpty() ? QStringLiteral("running") : job.status.trimmed();
        lines << QStringLiteral("- job_id=%1 status=%2").arg(id, status);
    }
    if (jobs.size() > maxPreview)
        lines << QStringLiteral("- ... 还有 %1 个任务在运行").arg(jobs.size() - maxPreview);

    lines << QStringLiteral("你可以直接说“查看子代理进度”，我会立即汇报；也可以说“取消 job_id=xxx”。");
    return lines.join(QStringLiteral("\n"));
}

QString delegateToolKey(const QString& sessionId, const QString& toolId)
{
    return sessionId.trimmed() + QStringLiteral("|") + toolId.trimmed();
}

bool isDelegateStatusLikeTool(const QString& toolName)
{
    return toolName == QLatin1String("delegate_status")
        || toolName == QLatin1String("delegate_list_active");
}

QJsonObject sanitizePersistedToolArguments(const QString& toolName, const QJsonObject& args)
{
    if (toolName == QLatin1String("delegate_status")) {
        QJsonObject compact;
        const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
        if (!jobId.isEmpty())
            compact.insert(QStringLiteral("job_id"), jobId);
        return compact;
    }

    if (toolName == QLatin1String("delegate_list_active")) {
        QJsonObject compact;
        if (args.contains(QStringLiteral("limit")))
            compact.insert(QStringLiteral("limit"), args.value(QStringLiteral("limit")));
        return compact;
    }

    if (toolName == QLatin1String("delegate_cancel")) {
        QJsonObject compact;
        const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
        if (!jobId.isEmpty())
            compact.insert(QStringLiteral("job_id"), jobId);
        return compact;
    }

    return args;
}

QJsonObject sanitizePersistedToolEventData(const QString& toolName, const QJsonObject& data)
{
    if (!isDelegateStatusLikeTool(toolName))
        return data;

    QJsonObject compact;
    const auto copyField = [&](const QString& key) {
        if (data.contains(key))
            compact.insert(key, data.value(key));
    };

    copyField(QStringLiteral("job_id"));
    copyField(QStringLiteral("owner_agent_id"));
    copyField(QStringLiteral("status"));
    copyField(QStringLiteral("summary"));
    copyField(QStringLiteral("failure_reason"));
    copyField(QStringLiteral("created_at_ms"));
    copyField(QStringLiteral("started_at_ms"));
    copyField(QStringLiteral("last_progress_at_ms"));
    copyField(QStringLiteral("finished_at_ms"));
    copyField(QStringLiteral("expected_timeout_ms"));
    copyField(QStringLiteral("hard_timeout_ms"));
    copyField(QStringLiteral("stall_no_progress_ms"));
    copyField(QStringLiteral("child_tool_completed_count"));
    copyField(QStringLiteral("child_tool_failure_count"));
    copyField(QStringLiteral("child_tool_success_count"));
    copyField(QStringLiteral("child_stream_chunk_count"));
    copyField(QStringLiteral("child_stream_chars"));

    QString task = data.value(QStringLiteral("task")).toString().trimmed();
    if (!task.isEmpty()) {
        if (task.size() > 240)
            task = task.left(240) + QStringLiteral("...");
        compact.insert(QStringLiteral("task"), task);
    }

    return compact;
}

QString sanitizePersistedToolRawResult(const QString& toolName, const QString& rawResult)
{
    QString out = rawResult.trimmed().isEmpty()
        ? QStringLiteral("[工具执行完成，无输出]")
        : rawResult;

    if (isDelegateStatusLikeTool(toolName) && out.size() > 900)
        out = out.left(900) + QStringLiteral("\n...[status truncated]...");

    return out;
}

int estimateHistoryMessageChars(const QJsonObject& msg)
{
    const QString role = msg.value(QStringLiteral("role")).toString();
    const QString content = msg.value(QStringLiteral("content")).toString();
    int size = role.size() + content.size();
    if (msg.contains(QStringLiteral("tool_call_id")))
        size += msg.value(QStringLiteral("tool_call_id")).toString().size();
    if (msg.contains(QStringLiteral("tool_calls"))) {
        const QJsonArray toolCalls = msg.value(QStringLiteral("tool_calls")).toArray();
        const QByteArray toolCallsJson = QJsonDocument(toolCalls).toJson(QJsonDocument::Compact);
        size += qMin(toolCallsJson.size(), 4096);
    }
    return size;
}

int estimateHistoryChars(const QJsonArray& history)
{
    int total = 0;
    for (const QJsonValue& value : history)
        total += estimateHistoryMessageChars(value.toObject());
    return total;
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
           && kept.first().toObject().value(QStringLiteral("role")).toString() == QLatin1String("tool")) {
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

QString defaultHeartbeatTemplate()
{
    return QStringLiteral(
        "## HEARTBEAT\n"
        "你正在执行后台心跳巡检，请遵循：\n"
        "1. 优先汇总当前任务进度与风险。\n"
        "2. 若有子代理任务，先给出 job 状态摘要。\n"
        "3. 若无关键变化，默认静默（不发聊天消息），仅在手动触发时可回复“当前无关键更新”。\n"
        "4. 输出尽量简短，避免重复。\n");
}

bool containsCjk(const QString& text)
{
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF))
            return true;
    }
    return false;
}

int latinMojibakeCharCount(const QString& text)
{
    int count = 0;
    for (const QChar c : text) {
        const ushort u = c.unicode();
        if ((u >= 0x00C0 && u <= 0x00FF) || (u >= 0x00A1 && u <= 0x00BF))
            ++count;
    }
    return count;
}

QString decodePossiblyMojibakeUtf8(const QByteArray& bytes)
{
    const QString utf8Text = QString::fromUtf8(bytes);
    if (utf8Text.isEmpty())
        return utf8Text;
    if (containsCjk(utf8Text))
        return utf8Text;
    if (latinMojibakeCharCount(utf8Text) < 8)
        return utf8Text;

    const QString repaired = QString::fromUtf8(utf8Text.toLatin1());
    if (containsCjk(repaired))
        return repaired;
    return utf8Text;
}

QString normalizeLegacyHeartbeatLine(const QString& input)
{
    QString out = input;
    const QString replacement = QStringLiteral(
        "3. 若无关键变化，默认静默（不发聊天消息），仅在手动触发时可回复“当前无关键更新”。");
    const QStringList legacyHints = {
        QStringLiteral("3. 若无关键变化，返回“当前无关键更新”。"),
        QStringLiteral("3. 若无关键变化，返回\"当前无关键更新\"。"),
        QStringLiteral("3. 若无关键变化，返回“当前无关键更新”"),
        QStringLiteral("3. 若无关键变化，返回\"当前无关键更新\"")
    };
    for (const QString& legacy : legacyHints) {
        if (out.contains(legacy))
            out.replace(legacy, replacement);
    }
    return out;
}

QString normalizeHeartbeatSignal(const QString& raw)
{
    const QString s = raw.trimmed().toLower();
    if (s == QLatin1String("provider") || s == QLatin1String("provider_status"))
        return QStringLiteral("provider_status");
    if (s == QLatin1String("delegate") || s == QLatin1String("delegate_jobs"))
        return QStringLiteral("delegate_jobs");
    if (s == QLatin1String("pulse") || s == QLatin1String("pulse_state"))
        return QStringLiteral("pulse_state");
    if (s == QLatin1String("scheduler") || s == QLatin1String("scheduler_jobs"))
        return QStringLiteral("scheduler_jobs");
    if (s == QLatin1String("memory") || s == QLatin1String("memory_progress"))
        return QStringLiteral("memory_progress");
    return s;
}

QStringList normalizeHeartbeatSignals(const QStringList& input)
{
    QStringList out;
    for (const QString& raw : input) {
        const QString s = normalizeHeartbeatSignal(raw);
        if (s.isEmpty())
            continue;
        if (!out.contains(s))
            out.append(s);
    }
    if (out.isEmpty()) {
        out << QStringLiteral("provider_status")
            << QStringLiteral("delegate_jobs")
            << QStringLiteral("pulse_state");
    }
    return out;
}

bool isBackgroundHeartbeatClientMessageId(const QString& clientMessageId)
{
    return clientMessageId.trimmed().startsWith(QStringLiteral("heartbeat-bg-"), Qt::CaseInsensitive);
}

bool isHeartbeatPromptText(const QString& text)
{
    return text.trimmed().startsWith(QStringLiteral("【系统心跳任务】"));
}

bool isHeartbeatNoChangeReplyText(const QString& text)
{
    const QString t = text.trimmed();
    return t == QStringLiteral("当前无关键更新。")
        || t == QStringLiteral("当前无关键更新")
        || t == QStringLiteral("无关键更新。")
        || t == QStringLiteral("无关键更新");
}

QDateTime parseIsoDateTimeToUtc(const QString& raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return QDateTime();
    QDateTime dt = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(text, Qt::ISODate);
    if (!dt.isValid())
        return QDateTime();
    return dt.toUTC();
}

QStringList changedTopLevelKeys(const QJsonObject& previous, const QJsonObject& current)
{
    if (previous == current)
        return QStringList();

    QSet<QString> allKeys;
    const QStringList prevKeys = previous.keys();
    const QStringList curKeys = current.keys();
    for (const QString& k : prevKeys)
        allKeys.insert(k);
    for (const QString& k : curKeys)
        allKeys.insert(k);

    QStringList changed;
    changed.reserve(allKeys.size());
    for (auto it = allKeys.constBegin(); it != allKeys.constEnd(); ++it) {
        const QString& key = *it;
        if (previous.value(key) != current.value(key))
            changed.append(key);
    }
    std::sort(changed.begin(), changed.end(), [](const QString& a, const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });
    return changed;
}

bool writeUtf8TextFile(const QString& path, const QString& content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;
    const QByteArray bytes = content.toUtf8();
    const bool ok = (file.write(bytes) == bytes.size());
    file.close();
    return ok;
}

void repairHeartbeatFileIfNeeded(const QString& path)
{
    QFile file(path);
    if (!file.exists())
        return;
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return;
    const QByteArray raw = file.readAll();
    file.close();

    const QString decoded = QString::fromUtf8(raw);
    QString normalized = decodePossiblyMojibakeUtf8(raw);
    normalized = normalizeLegacyHeartbeatLine(normalized);
    if (decoded == normalized)
        return;
    writeUtf8TextFile(path, normalized);
}

} // namespace

ChatService::ChatService(QObject* parent)
    : QObject(parent)
    , m_persistence(new ChatPersistenceService())
    , m_stateRepository(new ChatStateRepository())
    , m_memoryManager(new MemoryManager(m_persistence.get()))
    , m_healthMonitor(new HealthMonitor(this))
    , m_heartbeatService(new HeartbeatService(this))
    , m_schedulerService(new SchedulerService(this))
    , m_runtimeManager(new RuntimeManager(this))
    , m_configService(new ConfigService(this))
    , m_logVerboseStreamEvents(envFlagEnabled("TMAGENT_LOG_STREAM_EVENTS_VERBOSE"))
{
    connect(m_runtimeManager, &RuntimeManager::runtimeCreated, this, &ChatService::connectRuntimeSignals);
    connect(m_configService, &ConfigService::configLoaded, this, &ChatService::configLoaded);
    if (m_heartbeatService) {
        connect(m_heartbeatService.get(), &HeartbeatService::heartbeatTriggered, this, &ChatService::onHeartbeatTriggered);
        connect(m_heartbeatService.get(), &HeartbeatService::heartbeatSkipped, this, [this](const QString& agentId, const QString& reason) {
            QJsonObject extra;
            extra.insert(QStringLiteral("agent_id"), agentId);
            extra.insert(QStringLiteral("reason"), reason);
            emitPipelineEvent(QStringLiteral("heartbeat.skipped"), QString(), nullptr, QString(), reason, extra);
        });
    }
    if (m_schedulerService) {
        connect(m_schedulerService.get(), &SchedulerService::jobFired, this, &ChatService::onScheduledJobTriggered);
    }
    // P0: 连接子 Agent 完成通知
    connect(DelegateTaskScheduler::instance(), &DelegateTaskScheduler::jobSettled, this, &ChatService::onDelegateJobSettled);
}

ChatService::~ChatService()
{
    saveSessionsToDisk();
}

void ChatService::initialize()
{
    // 初始化 SQLite 数据库（在所有服务之前）
    DatabaseManager::instance()->initialize();

    m_identityManager = IdentityManager::instance();
    m_sessionManager = SessionManager::instance();
    m_modelFactory = ModelFactory::instance();

    m_toolDispatcher = ToolDispatcher::instance();
    m_toolDispatcher->registerDefaultTools();

    m_mcpProvider = new McpToolProvider(m_toolDispatcher);
    m_toolDispatcher->registerProvider(m_mcpProvider, "mcp");
    if (m_stateRepository) {
        m_stateRepository->setDependencies(
            m_identityManager,
            m_sessionManager,
            m_toolDispatcher,
            m_persistence.get());
    }

    // 注入依赖到 RuntimeManager
    m_runtimeManager->setModelFactory(m_modelFactory);
    m_runtimeManager->setToolDispatcher(m_toolDispatcher);
    m_runtimeManager->setSessionManager(m_sessionManager);
    m_runtimeManager->setPersistence(m_persistence.get());

    // 注入依赖到 ConfigService
    m_configService->setPersistence(m_persistence.get());
    m_configService->setModelFactory(m_modelFactory);
    m_configService->setMcpProvider(m_mcpProvider);
    m_configService->setToolDispatcher(m_toolDispatcher);
    m_configService->setRuntimeManager(m_runtimeManager);

    m_configService->applyMcpConfig(m_configService->loadMcpConfigSpecs());

    if (m_sessionManager) {
        connect(m_sessionManager, &SessionManager::messagePosted, this, &ChatService::appendSessionMessageToDisk, Qt::UniqueConnection);
    }

    // 确保用户 Identity 存在
    m_identityManager->userIdentity();
    if (m_memoryManager) {
        QString memoryError;
        if (!m_memoryManager->ensureUserMemoryDocument(&memoryError) && !memoryError.isEmpty())
            qWarning() << "[ChatService] user memory init failed:" << memoryError;
    }

    m_configService->loadConfig();

    if (m_healthMonitor) {
        m_healthMonitor->setRuntimeManager(m_runtimeManager);
        m_healthMonitor->setModelFactory(m_modelFactory);
        connect(m_healthMonitor.get(), &HealthMonitor::providerDown, this, [this](const QString& configId, const QString& reason) {
            QJsonObject extra;
            extra.insert(QStringLiteral("provider_id"), configId);
            extra.insert(QStringLiteral("state"), QStringLiteral("down"));
            extra.insert(QStringLiteral("reason"), reason);
            emitPipelineEvent(QStringLiteral("infra.provider_down"), QString(), nullptr, QString(), reason, extra);
        });
        connect(m_healthMonitor.get(), &HealthMonitor::providerRecovered, this, [this](const QString& configId) {
            QJsonObject extra;
            extra.insert(QStringLiteral("provider_id"), configId);
            extra.insert(QStringLiteral("state"), QStringLiteral("recovered"));
            emitPipelineEvent(QStringLiteral("infra.provider_recovered"), QString(), nullptr, QString(), QString(), extra);
        });
        m_healthMonitor->start();
    }

    if (m_heartbeatService)
        m_heartbeatService->setPersistence(m_persistence.get());
    if (m_schedulerService) {
        m_schedulerService->setPersistence(m_persistence.get());
        m_schedulerService->start();
    }

    if (m_identityManager) {
        const QList<Identity*> agents = m_identityManager->allAgents();
        for (Identity* agent : agents) {
            if (!agent || !agent->isAgent())
                continue;
            const QString agentId = agent->id().trimmed();
            if (agentId.isEmpty())
                continue;
            ensureAgentPulse(agentId);
            if (m_heartbeatService)
                m_heartbeatService->startHeartbeat(agentId);
        }
    }

    // 启动跨进程同步轮询定时器
    if (!m_syncTimer) {
        m_syncTimer = new QTimer(this);
        m_syncTimer->setInterval(5000); // 5 秒轮询一次
        connect(m_syncTimer, &QTimer::timeout, this, &ChatService::pollExternalChanges);
        m_syncTimer->start();
        qDebug() << "[ChatService] 跨进程同步轮询已启动（间隔 5s）";
    }
}

QString ChatService::enqueueUserMessage(const QString& sessionId, const QString& text, const QString& clientMessageId)
{
    const QString userId = m_identityManager ? m_identityManager->userIdentity()->id() : QString();
    return enqueueUserMessageAs(userId, sessionId, text, clientMessageId);
}

QString ChatService::enqueueUserMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& text, const QString& clientMessageId)
{
    if (!canIdentitySendMessage(actorIdentityId, sessionId)) {
        qWarning() << "[ChatService] 拒绝发送消息，actor 无权限:" << actorIdentityId
                   << "session:" << sessionId;
        return QString();
    }

    const QString prompt = text.trimmed();
    if (prompt.isEmpty())
        return QString();

    Session* session = m_sessionManager ? m_sessionManager->findById(sessionId) : nullptr;
    if (!session)
        return QString();

    Identity* actor = m_identityManager ? m_identityManager->findById(actorIdentityId) : nullptr;
    if (!actor)
        return QString();
    const QString actorId = actor->id().trimmed();
    if (actorId.isEmpty())
        return QString();

    ensurePipeline(sessionId);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    QStringList participantAgentIds;
    QHash<QString, QString> participantAgentNames;
    for (const QString& participantId : session->participantIds()) {
        Identity* participant = m_identityManager ? m_identityManager->findById(participantId) : nullptr;
        if (!participant || !participant->isAgent())
            continue;
        participantAgentIds.append(participant->id());
        participantAgentNames.insert(participant->id(), participant->name());
    }
    MessageRouter::RouteInput routeInput;
    routeInput.sessionType = session->type();
    routeInput.senderIdentityId = actorId;
    routeInput.userIdentityId = m_identityManager && m_identityManager->userIdentity()
        ? m_identityManager->userIdentity()->id()
        : QString();
    routeInput.text = prompt;
    routeInput.participantAgentIds = participantAgentIds;
    routeInput.agentDisplayNames = participantAgentNames;
    const MessageRouter::RouteResult routeResult = MessageRouter::route(routeInput);

    TurnTask* mergeTarget = nullptr;
    if (TurnTask* tail = m_turnManager.queuedTail(sessionId)) {
        const QString tailActorId = tail->actorIdentityId.trimmed();
        const int tailMergedCount = qMax(1, tail->mergedMessageCount);
        const bool sameActor = !tailActorId.isEmpty() && tailActorId == actorId;
        const bool withinWindow = tail->enqueuedAtMs > 0 && nowMs >= tail->enqueuedAtMs && (nowMs - tail->enqueuedAtMs) <= kQueueMergeWindowMs;
        const bool withinMergeCount = tailMergedCount < kQueueMergeMaxMergedMessages;
        const bool withinMergedSize = (tail->userContent.size() + prompt.size() + 32) <= kQueueMergeMaxChars;
        if (sameActor && withinWindow && withinMergeCount && withinMergedSize)
            mergeTarget = tail;
    }

    const int queueDepthBeforeEnqueue = m_turnManager.totalDepth(sessionId);
    if (!mergeTarget && queueDepthBeforeEnqueue >= kHardQueueDepth) {
        QJsonObject extra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("queue_overflow"));
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        extra.insert(QStringLiteral("queueHardLimit"), kHardQueueDepth);
        emitPipelineEvent(QStringLiteral("turn_rejected"), sessionId, nullptr, QString(), QStringLiteral("queue overflow"), extra);
        return QString();
    }
    if (!mergeTarget && queueDepthBeforeEnqueue >= kSoftQueueDepth) {
        QJsonObject extra;
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        extra.insert(QStringLiteral("queueSoftLimit"), kSoftQueueDepth);
        emitPipelineEvent(QStringLiteral("queue_backpressure"), sessionId, nullptr, QString(), QString(), extra);
    }

    QString requestTraceId = mergeTarget ? mergeTarget->requestTraceId : QString();
    if (requestTraceId.trimmed().isEmpty())
        requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString turnId = mergeTarget ? mergeTarget->turnId : QString();
    if (turnId.trimmed().isEmpty())
        turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (mergeTarget) {
        mergeTarget->requestTraceId = requestTraceId;
        mergeTarget->turnId = turnId;
    }

    TurnTask turn;
    turn.requestTraceId = requestTraceId;
    turn.turnId = turnId;
    turn.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    turn.actorIdentityId = actorId;
    turn.enqueuedAtMs = nowMs;
    turn.mergedMessageCount = 1;
    turn.clientMessageId = clientMessageId.trimmed();
    turn.userContent = prompt;

    const bool skipPersistUserMessage = isBackgroundHeartbeatClientMessageId(turn.clientMessageId);
    if (!skipPersistUserMessage) {
        // Message 成为会话主干数据：用户消息先写入 Session，再进入执行流水线。
        Message userMsg = Message::createText(sessionId, actorId, prompt);
        userMsg.traceId = requestTraceId;
        userMsg.turnId = turnId;
        userMsg.mentions = routeResult.targetAgentIds;
        userMsg.status = Message::Status::Completed;
        m_sessionManager->postMessage(sessionId, userMsg);
    }

    QJsonObject routeExtra;
    routeExtra.insert(
        QStringLiteral("target_agent_ids"),
        QJsonArray::fromStringList(routeResult.targetAgentIds));
    routeExtra.insert(
        QStringLiteral("mention_tokens"),
        QJsonArray::fromStringList(routeResult.mentionTokens));
    routeExtra.insert(
        QStringLiteral("unresolved_mentions"),
        QJsonArray::fromStringList(routeResult.unresolvedMentions));
    routeExtra.insert(QStringLiteral("is_broadcast"), routeResult.isBroadcast);
    routeExtra.insert(QStringLiteral("used_default_route"), routeResult.usedDefaultRoute);
    emitPipelineEvent(QStringLiteral("message_routed"), sessionId, &turn, QString(), QString(), routeExtra, false);

    if (mergeTarget) {
        mergeTarget->enqueuedAtMs = nowMs;
        mergeTarget->mergedMessageCount = qMax(1, mergeTarget->mergedMessageCount) + 1;
        if (!turn.clientMessageId.isEmpty())
            mergeTarget->clientMessageId = turn.clientMessageId;
        mergeTarget->userContent.append(QStringLiteral("\n\n[补充消息]\n"));
        mergeTarget->userContent.append(prompt);

        QJsonObject extra;
        extra.insert(QStringLiteral("mergedIntoTurnId"), mergeTarget->turnId);
        extra.insert(QStringLiteral("mergedMessageCount"), mergeTarget->mergedMessageCount);
        extra.insert(QStringLiteral("queueDepth"), queueDepthBeforeEnqueue);
        emitPipelineEvent(QStringLiteral("turn_merged"), sessionId, mergeTarget, QString(), QString(), extra);
        return mergeTarget->turnId;
    }

    // 多轮连续发消息：只入队，不打断当前正在执行的 turn。
    // 当前 turn 完成后，按队列顺序执行后续消息。
    m_turnManager.enqueueTurn(sessionId, turn);

    emitPipelineEvent(QStringLiteral("turn_queued"), sessionId, &turn);
    tryStartNextTurn(sessionId);
    return turn.turnId;
}

void ChatService::sendUserMessage(const QString& sessionId, const QString& text)
{
    enqueueUserMessage(sessionId, text);
}

void ChatService::sendUserMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& text)
{
    enqueueUserMessageAs(actorIdentityId, sessionId, text);
}

void ChatService::abortCurrent(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !m_turnManager.hasActiveTurn(sessionId)) {
        resetSessionStreamState(sessionId);
        return;
    }

    if (TurnTask* active = m_turnManager.activeTurn(sessionId))
        flushPendingDeltaLog(sessionId, pipeline, active, true);

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime)
        runtime->abort();

    TurnTask cancelled;
    if (!m_turnManager.clearActiveTurn(sessionId, &cancelled))
        return;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);
    QJsonObject extra;
    extra.insert(QStringLiteral("reason"), QStringLiteral("user_stop"));
    emitPipelineEvent(QStringLiteral("turn_cancelled"), sessionId, &cancelled, QString(), QString(), extra);
    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
}

QString ChatService::abortAndRollback(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline || !m_turnManager.hasActiveTurn(sessionId)) {
        resetSessionStreamState(sessionId);
        return QString();
    }

    if (TurnTask* active = m_turnManager.activeTurn(sessionId))
        flushPendingDeltaLog(sessionId, pipeline, active, true);

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    QString rolledBack;
    if (runtime)
        rolledBack = runtime->abortAndRollback();

    TurnTask cancelled;
    if (!m_turnManager.clearActiveTurn(sessionId, &cancelled))
        return rolledBack;
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);

    if (rolledBack.isEmpty())
        rolledBack = cancelled.userContent;

    QJsonObject extra;
    extra.insert(QStringLiteral("reason"), QStringLiteral("user_stop"));
    extra.insert(QStringLiteral("rolledBackUserMessage"), rolledBack);
    emitPipelineEvent(QStringLiteral("turn_cancelled"), sessionId, &cancelled, QString(), QString(), extra);
    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
    return rolledBack;
}

Session* ChatService::createNewSession(const QString& agentName)
{
    QString userId = m_identityManager->userIdentity()->id();

    // 创建 Agent Identity
    auto* profile = new IdentityProfile();
    const LLMConfig defaultCfg = m_runtimeManager->defaultAgentConfig();
    profile->setLlmConfig(defaultCfg);
    profile->setSystemPrompt(defaultCfg.systemPrompt);
    profile->setDelegateEnabled(true);
    profile->setAllowedTools(ChatStateRepository::collectToolNamesFrom(m_toolDispatcher));

    QString name = agentName.isEmpty() ? QStringLiteral("TM Agent") : agentName;
    Identity* agentIdentity = m_identityManager->createAgent(name, profile);
    ensureMemoryInitializedForAgent(agentIdentity);
    if (agentIdentity) {
        const QString agentId = agentIdentity->id().trimmed();
        if (!agentId.isEmpty()) {
            ensureAgentPulse(agentId);
            if (m_heartbeatService)
                m_heartbeatService->startHeartbeat(agentId);
        }
    }

    // 创建 Private Session
    Session* session = m_sessionManager->createPrivateSession(userId, agentIdentity->id());
    session->setTitle(name);

    m_currentSessionId = session->id();
    emit sessionCreated(session->id());
    saveSessionsToDisk();
    return session;
}

Session* ChatService::createSessionForIdentity(const QString& identityId, const QString& title)
{
    const QString userId = m_identityManager ? m_identityManager->userIdentity()->id() : QString();
    return createSessionForIdentityAs(userId, identityId, title);
}

Session* ChatService::createSessionForIdentityAs(const QString& actorIdentityId, const QString& identityId, const QString& title)
{
    if (!canIdentityManageSessions(actorIdentityId)) {
        qWarning() << "[ChatService] 拒绝创建会话，actor 无权限:" << actorIdentityId
                   << "target:" << identityId;
        return nullptr;
    }

    Identity* identity = m_identityManager->findById(identityId);
    if (!identity)
        return nullptr;

    // 用户视角走原逻辑
    if (identity->isUser())
        return createNewSession(title);
    ensureMemoryInitializedForAgent(identity);
    if (identity) {
        const QString agentId = identity->id().trimmed();
        if (!agentId.isEmpty()) {
            ensureAgentPulse(agentId);
            if (m_heartbeatService)
                m_heartbeatService->startHeartbeat(agentId);
        }
    }

    // Agent 视角：复用已有 Agent Identity，创建新 Private Session
    QString userId = m_identityManager->userIdentity()->id();
    Session* session = m_sessionManager->createPrivateSession(userId, identityId);
    session->setTitle(title.isEmpty() ? identity->name() : title);

    emit sessionCreated(session->id());
    saveSessionsToDisk();
    return session;
}

QList<Session*> ChatService::sessionsForIdentity(const QString& identityId) const
{
    return m_sessionManager->sessionsForIdentity(identityId);
}

void ChatService::removeSession(const QString& sessionId)
{
    const QString userId = m_identityManager ? m_identityManager->userIdentity()->id() : QString();
    removeSessionAs(userId, sessionId);
}

bool ChatService::removeSessionAs(const QString& actorIdentityId, const QString& sessionId)
{
    if (!canIdentityManageSessions(actorIdentityId)) {
        qWarning() << "[ChatService] 拒绝删除会话，actor 无权限:" << actorIdentityId
                   << "session:" << sessionId;
        return false;
    }

    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return false;

    const QString agentId = agentIdentityIdForSession(sessionId);
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime && runtime->isStreaming() && m_agentActiveSession.value(agentId) == sessionId) {
        runtime->abort();
        m_agentActiveSession.remove(agentId);
    }

    m_turnManager.removePipeline(sessionId);
    m_delegateStatsBySession.remove(sessionId);
    clearToolProgressCacheForSession(sessionId);
    const QString keyPrefix = sessionId.trimmed() + QStringLiteral("|");
    for (auto it = m_delegateStartMsByToolKey.begin(); it != m_delegateStartMsByToolKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_delegateStartMsByToolKey.erase(it);
        else
            ++it;
    }

    m_sessionManager->removeSession(sessionId);
    if (!agentId.isEmpty()) {
        tryStartNextTurnForAgent(agentId);
        releaseRuntimeIfUnused(agentId);
    }

    if (m_currentSessionId == sessionId) {
        m_currentSessionId.clear();
    }

    emit sessionRemoved(sessionId);
    saveSessionsToDisk();
    return true;
}

bool ChatService::removeAgentMemoryAs(const QString& actorIdentityId, const QString& agentIdentityId)
{
    if (!canIdentityManageSessions(actorIdentityId)) {
        qWarning() << "[ChatService] 拒绝删除 Agent 记忆目录，actor 无权限:" << actorIdentityId
                   << "agent:" << agentIdentityId;
        return false;
    }
    if (!m_memoryManager)
        return true;

    QString err;
    const bool ok = m_memoryManager->removeAgentMemory(agentIdentityId, &err);
    if (ok) {
        m_memoryRetainedTurnsByAgent.remove(agentIdentityId.trimmed());
        m_heartbeatRuntimeByAgent.remove(agentIdentityId.trimmed());
    }
    if (!ok)
        qWarning() << "[ChatService] 删除 Agent 记忆目录失败:" << agentIdentityId << err;
    return ok;
}

bool ChatService::rememberMessageAs(const QString& actorIdentityId, const QString& sessionId, const QString& messageId, const QString& fallbackContent, QString* error)
{
    if (error)
        error->clear();
    if (!canIdentityManageGlobalConfig(actorIdentityId)) {
        if (error)
            *error = QStringLiteral("actor has no permission");
        return false;
    }
    if (!m_memoryManager) {
        if (error)
            *error = QStringLiteral("memory manager unavailable");
        return false;
    }

    Session* session = m_sessionManager ? m_sessionManager->findById(sessionId) : nullptr;
    if (!session) {
        if (error)
            *error = QStringLiteral("session not found");
        return false;
    }

    const QString agentId = agentIdentityIdForSession(sessionId);
    if (agentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent identity not found for session");
        return false;
    }

    QString selectedText;
    QString selectedTurnId;
    QString selectedTraceId;
    const QString trimmedMessageId = messageId.trimmed();
    const QString fallback = fallbackContent.trimmed();
    const QList<Message> allMessages = session->allMessages();

    // Search messages in reverse for a text/system message matching a predicate
    auto findMessage = [&](auto&& match) -> bool {
        for (int i = allMessages.size() - 1; i >= 0; --i) {
            const Message& msg = allMessages.at(i);
            if (msg.content.type != MessageContent::Type::Text
                && msg.content.type != MessageContent::Type::System)
                continue;
            if (!match(msg))
                continue;
            selectedText = msg.content.text.trimmed();
            selectedTurnId = msg.turnId.trimmed();
            selectedTraceId = msg.traceId.trimmed();
            return true;
        }
        return false;
    };

    if (!trimmedMessageId.isEmpty())
        findMessage([&](const Message& msg) { return msg.id == trimmedMessageId; });
    if (selectedText.isEmpty() && !fallback.isEmpty())
        findMessage([&](const Message& msg) { return msg.content.text.trimmed() == fallback; });
    if (selectedText.isEmpty())
        selectedText = fallback;
    if (selectedText.isEmpty()) {
        if (error)
            *error = QStringLiteral("message content is empty");
        return false;
    }

    QString memorySummary;
    QString memoryPath;
    QJsonObject memoryMetadata;
    QString memoryError;
    const bool ok = m_memoryManager->rememberManual(agentId, sessionId, selectedTurnId, selectedTraceId, selectedText, &memorySummary, &memoryPath, &memoryMetadata, &memoryError);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    TurnTask syntheticTurn;
    const TurnTask* eventTurn = activeTurn;
    if (!eventTurn && (!selectedTurnId.isEmpty() || !selectedTraceId.isEmpty())) {
        syntheticTurn.turnId = selectedTurnId;
        syntheticTurn.requestTraceId = selectedTraceId;
        syntheticTurn.runId = QStringLiteral("manual_remember");
        syntheticTurn.actorIdentityId = actorIdentityId;
        eventTurn = &syntheticTurn;
    }
    if (!ok) {
        QJsonObject memoryExtra;
        memoryExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        memoryExtra.insert(QStringLiteral("path"), memoryPath);
        memoryExtra.insert(QStringLiteral("manualRemember"), true);
        if (!selectedTraceId.isEmpty())
            memoryExtra.insert(QStringLiteral("source_trace_id"), selectedTraceId);
        if (!selectedTurnId.isEmpty())
            memoryExtra.insert(QStringLiteral("source_turn_id"), selectedTurnId);
        emitPipelineEvent(QStringLiteral("memory.error"), sessionId, eventTurn, QString(), memoryError.isEmpty() ? QStringLiteral("manual remember failed") : memoryError, memoryExtra);
        if (error)
            *error = memoryError.isEmpty()
                ? QStringLiteral("manual remember failed")
                : memoryError;
        return false;
    }

    QJsonObject updateExtra;
    updateExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
    updateExtra.insert(QStringLiteral("summary"), memorySummary);
    updateExtra.insert(QStringLiteral("path"), memoryPath);
    updateExtra.insert(QStringLiteral("manualRemember"), true);
    if (!selectedTraceId.isEmpty())
        updateExtra.insert(QStringLiteral("source_trace_id"), selectedTraceId);
    if (!selectedTurnId.isEmpty())
        updateExtra.insert(QStringLiteral("source_turn_id"), selectedTurnId);
    for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
        updateExtra.insert(it.key(), it.value());
    emitPipelineEvent(QStringLiteral("memory.updated"), sessionId, eventTurn, QString(), QString(), updateExtra);

    const int compactedCount = memoryMetadata.value(QStringLiteral("compacted_count")).toInt();
    if (compactedCount > 0) {
        QJsonObject compactExtra;
        compactExtra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        compactExtra.insert(QStringLiteral("summary"), memorySummary);
        compactExtra.insert(QStringLiteral("compacted_count"), compactedCount);
        compactExtra.insert(QStringLiteral("path"), memoryMetadata.value(QStringLiteral("longMemoryPath")).toString());
        compactExtra.insert(QStringLiteral("longMemoryAdded"), memoryMetadata.value(QStringLiteral("longMemoryAdded")).toInt());
        compactExtra.insert(QStringLiteral("longMemoryDuplicate"), memoryMetadata.value(QStringLiteral("longMemoryDuplicate")).toInt());
        compactExtra.insert(QStringLiteral("manualRemember"), true);
        for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
            compactExtra.insert(it.key(), it.value());
        if (!selectedTraceId.isEmpty())
            compactExtra.insert(QStringLiteral("source_trace_id"), selectedTraceId);
        if (!selectedTurnId.isEmpty())
            compactExtra.insert(QStringLiteral("source_turn_id"), selectedTurnId);
        emitPipelineEvent(QStringLiteral("memory.compacted"), sessionId, eventTurn, QString(), QString(), compactExtra);
    }

    refreshMemoryIndexAndEmit(sessionId, agentId, eventTurn, QStringLiteral("manual_remember"), memoryPath, memoryMetadata);

    return true;
}

bool ChatService::rebuildMemoryIndexAs(const QString& actorIdentityId, const QString& agentIdentityId, QJsonObject* result, QString* error)
{
    if (result)
        *result = QJsonObject();
    if (error)
        error->clear();

    if (!canIdentityManageGlobalConfig(actorIdentityId)) {
        if (error)
            *error = QStringLiteral("actor has no permission");
        return false;
    }
    if (!m_memoryManager || !m_identityManager) {
        if (error)
            *error = QStringLiteral("memory manager unavailable");
        return false;
    }

    QStringList targetAgents;
    const QString targetAgentId = agentIdentityId.trimmed();
    if (!targetAgentId.isEmpty()) {
        Identity* agent = m_identityManager->findById(targetAgentId);
        if (!agent || !agent->isAgent()) {
            if (error)
                *error = QStringLiteral("agent not found");
            return false;
        }
        targetAgents.append(agent->id());
    } else {
        const QList<Identity*> agents = m_identityManager->allAgents();
        for (Identity* agent : agents) {
            if (!agent || !agent->isAgent())
                continue;
            targetAgents.append(agent->id());
        }
    }

    if (targetAgents.isEmpty()) {
        if (result) {
            result->insert(QStringLiteral("agents_total"), 0);
            result->insert(QStringLiteral("agents_success"), 0);
            result->insert(QStringLiteral("agents_failed"), 0);
            result->insert(QStringLiteral("rows_indexed"), 0);
            result->insert(QStringLiteral("items"), QJsonArray());
        }
        return true;
    }

    int successCount = 0;
    int failedCount = 0;
    int totalRows = 0;
    QJsonArray items;
    QStringList failures;

    for (const QString& id : targetAgents) {
        QJsonObject indexMetadata;
        QString indexError;
        const bool ok = m_memoryManager->rebuildSearchIndex(id, &indexMetadata, &indexError);

        QJsonObject item;
        item.insert(QStringLiteral("agent_id"), id);
        item.insert(QStringLiteral("success"), ok);
        if (ok) {
            ++successCount;
            totalRows += indexMetadata.value(QStringLiteral("rows_indexed")).toInt();
            for (auto it = indexMetadata.constBegin(); it != indexMetadata.constEnd(); ++it)
                item.insert(it.key(), it.value());
        } else {
            ++failedCount;
            item.insert(QStringLiteral("error"), indexError);
            failures.append(QStringLiteral("%1: %2").arg(id, indexError));
        }
        items.append(item);

        QString sessionId;
        if (m_sessionManager) {
            const QList<Session*> sessions = m_sessionManager->sessionsForIdentity(id);
            if (!sessions.isEmpty())
                sessionId = sessions.first()->id();
        }
        QJsonObject eventExtra = indexMetadata;
        eventExtra.insert(QStringLiteral("agent_id"), id);
        eventExtra.insert(QStringLiteral("reason"), QStringLiteral("manual_rebuild"));
        eventExtra.insert(QStringLiteral("scope"), targetAgentId.isEmpty() ? QStringLiteral("all") : QStringLiteral("single"));
        emitPipelineEvent(ok ? QStringLiteral("memory.index.updated") : QStringLiteral("memory.index.error"), sessionId, nullptr, QString(), ok ? QString() : indexError, eventExtra);
    }

    if (result) {
        result->insert(QStringLiteral("agents_total"), targetAgents.size());
        result->insert(QStringLiteral("agents_success"), successCount);
        result->insert(QStringLiteral("agents_failed"), failedCount);
        result->insert(QStringLiteral("rows_indexed"), totalRows);
        result->insert(QStringLiteral("items"), items);
    }

    if (failedCount > 0) {
        if (error)
            *error = failures.join(QStringLiteral("; "));
        return false;
    }
    return true;
}

void ChatService::switchSession(const QString& sessionId)
{
    if (sessionId == m_currentSessionId)
        return;
    m_currentSessionId = sessionId;

    // 同步 Runtime 的会话上下文（仅在未流式时切换，避免串流路由错位）。
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (runtime && !runtime->isStreaming())
        runtime->switchToSession(sessionId);
}

QString ChatService::currentSessionId() const { return m_currentSessionId; }

RuntimeManager* ChatService::runtimeManager() const { return m_runtimeManager; }
ConfigService* ChatService::configService() const { return m_configService; }
HeartbeatService* ChatService::heartbeatService() const { return m_heartbeatService.get(); }
SchedulerService* ChatService::schedulerService() const { return m_schedulerService.get(); }

AgentRuntime* ChatService::runtimeForSession(const QString& sessionId) const
{
    const QString agentId = agentIdentityIdForSession(sessionId);
    if (agentId.isEmpty())
        return nullptr;
    return m_runtimeManager->runtimeForAgent(agentId);
}

AgentRuntime* ChatService::ensureRuntimeForSession(const QString& sessionId)
{
    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return nullptr;

    Identity* agentIdentity = findOrCreateAgentIdentity(session);
    AgentRuntime* runtime = ensureRuntimeForAgent(agentIdentity);
    if (!runtime)
        return nullptr;

    // 仅在 Runtime 空闲或已绑定当前会话时装载历史，避免覆盖其他会话正在运行的上下文。
    const QString activeSessionId = m_agentActiveSession.value(agentIdentity->id());
    if (activeSessionId.isEmpty() || activeSessionId == sessionId) {
        runtime->switchToSession(sessionId);
        runtime->setHistory(buildRuntimeHistoryFromMessages(session));
    }
    return runtime;
}

void ChatService::setDefaultAgentConfig(const LLMConfig& config)
{
    m_runtimeManager->setDefaultAgentConfig(config);
}

LLMConfig ChatService::defaultAgentConfig() const { return m_runtimeManager->defaultAgentConfig(); }

void ChatService::applyConfigToAllRuntimes()
{
    m_runtimeManager->applyConfigToAllRuntimes();
}

void ChatService::applyToolDispatcherToAllRuntimes()
{
    m_runtimeManager->applyToolDispatcherToAllRuntimes();
}

bool ChatService::isSessionStreaming(const QString& sessionId) const
{
    if (m_turnManager.hasActiveTurn(sessionId))
        return true;

    Session* session = m_sessionManager->findById(sessionId);
    return session && session->isStreaming();
}

int ChatService::pendingTurnCount(const QString& sessionId) const
{
    return m_turnManager.queuedTurnCount(sessionId);
}

QString ChatService::activeRunId(const QString& sessionId) const
{
    const TurnTask* active = m_turnManager.activeTurn(sessionId);
    if (!active)
        return QString();
    return active->runId;
}

QString ChatService::agentDisplayNameForSession(const QString& sessionId) const
{
    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return QStringLiteral("TM Agent");

    // 找到 Agent 参与者的名称
    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_identityManager->findById(pid);
        if (identity && identity->isAgent())
            return identity->name();
    }

    if (!session->title().isEmpty())
        return session->title();

    return QStringLiteral("TM Agent");
}

bool ChatService::canIdentityManageSessions(const QString& identityId) const
{
    return isUserIdentity(identityId);
}

bool ChatService::canIdentitySendMessage(const QString& identityId, const QString& sessionId) const
{
    if (!isUserIdentity(identityId))
        return false;

    if (sessionId.isEmpty())
        return true;

    return m_sessionManager && m_sessionManager->findById(sessionId) != nullptr;
}

bool ChatService::canIdentityManageGlobalConfig(const QString& identityId) const
{
    return isUserIdentity(identityId);
}

ModelFactory* ChatService::modelFactory() const { return m_modelFactory; }
ToolDispatcher* ChatService::toolDispatcher() const { return m_toolDispatcher; }
McpToolProvider* ChatService::mcpProvider() const { return m_mcpProvider; }

void ChatService::applyMcpConfig(const QStringList& specs)
{
    m_configService->applyMcpConfig(specs);
}

QStringList ChatService::loadMcpConfigSpecs() const
{
    return m_configService->loadMcpConfigSpecs();
}

bool ChatService::saveMcpConfigSpecs(const QStringList& specs) const
{
    return m_configService->saveMcpConfigSpecs(specs);
}

QString ChatService::mcpConfigPath() const
{
    return m_configService->mcpConfigPath();
}

QString ChatService::modelConfigPath() const
{
    return m_configService->modelConfigPath();
}

void ChatService::loadConfig()
{
    m_configService->loadConfig();
}

void ChatService::appendSessionMessageToDisk(const QString& sessionId, const Message& msg)
{
    if (!m_persistence || sessionId.trimmed().isEmpty() || !msg.isValid())
        return;
    if (!m_persistence->appendSessionMessage(sessionId, m_persistence->messageToJson(msg))) {
        qWarning() << "[ChatService] 消息追加写入失败，sessionId=" << sessionId
                   << "messageId=" << msg.id;
        return;
    }

    Session* session = m_sessionManager ? m_sessionManager->findById(sessionId) : nullptr;
    if (session)
        m_lastSavedMessageCounts.insert(sessionId, session->messageCount());
}

void ChatService::pollExternalChanges()
{
    if (!m_persistence || !m_sessionManager || !DatabaseManager::instance()->isReady())
        return;

    const QList<Session*> sessions = m_sessionManager->allSessions();
    for (Session* session : sessions) {
        if (!session)
            continue;
        const QString sid = session->id();

        // 获取上次已知的最大 rowid
        const qint64 lastRowId = m_lastSyncRowIds.value(sid, 0);

        // 查询数据库中当前最大 rowid
        const qint64 currentMaxRowId = m_persistence->maxMessageRowId(sid);
        if (currentMaxRowId <= lastRowId)
            continue; // 没有新消息

        // 加载增量消息
        const QList<Message> newMessages = m_persistence->loadNewMessagesFromDb(sid, lastRowId);
        if (newMessages.isEmpty()) {
            m_lastSyncRowIds.insert(sid, currentMaxRowId);
            continue;
        }
        int injectedCount = 0;
        for (const Message& msg : newMessages) {
            // 跳过本进程已知的消息（通过 ID 去重）
            if (session->findMessageById(msg.id))
                continue;

            // 通过 SessionManager 注入，触发 messagePosted 信号 → UI 实时刷新
            // （INSERT OR IGNORE 保证幂等性，不会重复写入 DB）
            m_sessionManager->postMessage(sid, msg);
            ++injectedCount;
        }

        m_lastSyncRowIds.insert(sid, currentMaxRowId);

        if (injectedCount > 0) {
            qDebug() << "[ChatService] 跨进程同步：会话" << sid
                     << "注入" << injectedCount << "条新消息";

            // 通知 UI 刷新消息列表
            QJsonObject syncEvent;
            syncEvent.insert(QStringLiteral("type"), QStringLiteral("sync_messages_injected"));
            syncEvent.insert(QStringLiteral("sessionId"), sid);
            syncEvent.insert(QStringLiteral("count"), injectedCount);
            emit conversationEvent(syncEvent);
        }
    }
}

void ChatService::saveSessionsToDisk()
{
    if (!m_stateRepository)
        return;
    m_lastSavedMessageCounts = m_stateRepository->saveState(
        m_currentSessionId,
        m_lastSavedMessageCounts,
        [this](const QString& sid) -> const SessionPipeline* {
            return findPipeline(sid);
        });
}

bool ChatService::loadSessionsFromDisk()
{
    if (!m_stateRepository)
        return false;

    // 清理现有 Runtime（由 RuntimeManager 管理，这里只清空引用）
    auto& runtimes = m_runtimeManager->runtimes();
    for (AgentRuntime* runtime : runtimes)
        runtime->deleteLater();
    runtimes.clear();
    m_turnManager.clear();
    m_agentActiveSession.clear();
    m_lastSavedMessageCounts.clear();
    m_delegateStartMsByToolKey.clear();
    m_delegateStatsBySession.clear();
    m_toolProgressLastPersistMsByKey.clear();
    m_toolProgressLastDigestByKey.clear();

    const ChatStateRepository::LoadResult loaded = m_stateRepository->loadState(m_runtimeManager->defaultAgentConfig());
    if (!loaded.success)
        return false;

    m_lastSavedMessageCounts = loaded.savedMessageCounts;
    m_currentSessionId = loaded.currentSessionId;

    if (m_memoryManager) {
        QString memoryError;
        if (!m_memoryManager->ensureUserMemoryDocument(&memoryError) && !memoryError.isEmpty())
            qWarning() << "[ChatService] user memory init failed after load:" << memoryError;
    }
    if (m_identityManager) {
        const QList<Identity*> agents = m_identityManager->allAgents();
        for (Identity* agent : agents)
            ensureMemoryInitializedForAgent(agent);
    }

    for (auto it = loaded.pendingTurnsBySession.constBegin();
         it != loaded.pendingTurnsBySession.constEnd();
         ++it) {
        SessionPipeline& pipeline = ensurePipeline(it.key());
        for (const TurnTask& turn : it.value())
            pipeline.queue.append(turn);
        tryStartNextTurn(it.key());
    }

    return true;
}

SessionPipeline& ChatService::ensurePipeline(const QString& sessionId)
{
    return m_turnManager.ensurePipeline(sessionId);
}

SessionPipeline* ChatService::findPipeline(const QString& sessionId)
{
    return m_turnManager.findPipeline(sessionId);
}

const SessionPipeline* ChatService::findPipeline(const QString& sessionId) const
{
    return m_turnManager.findPipeline(sessionId);
}

QString ChatService::agentIdentityIdForSession(const QString& sessionId) const
{
    if (!m_sessionManager || !m_identityManager || sessionId.trimmed().isEmpty())
        return QString();

    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return QString();

    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_identityManager->findById(pid);
        if (identity && identity->isAgent())
            return identity->id();
    }
    return QString();
}

Identity* ChatService::findOrCreateAgentIdentity(Session* session)
{
    if (!session || !m_identityManager)
        return nullptr;

    for (const QString& pid : session->participantIds()) {
        Identity* identity = m_identityManager->findById(pid);
        if (identity && identity->isAgent())
            return identity;
    }

    auto* profile = new IdentityProfile();
    const LLMConfig defaultCfg2 = m_runtimeManager->defaultAgentConfig();
    profile->setLlmConfig(defaultCfg2);
    profile->setSystemPrompt(defaultCfg2.systemPrompt);
    profile->setDelegateEnabled(true);
    profile->setAllowedTools(ChatStateRepository::collectToolNamesFrom(m_toolDispatcher));
    Identity* agentIdentity = m_identityManager->createAgent(
        session->title().isEmpty() ? QStringLiteral("TM Agent") : session->title(),
        profile);
    ensureMemoryInitializedForAgent(agentIdentity);
    session->addParticipant(agentIdentity->id());
    return agentIdentity;
}

LLMConfig ChatService::composeConfigForIdentity(Identity* identity) const
{
    return m_runtimeManager->composeConfigForIdentity(identity);
}

QJsonArray ChatService::buildRuntimeHistoryFromMessages(Session* session) const
{
    QJsonArray history;
    if (!session)
        return history;

    QSet<QString> validToolCallIds;
    const QList<Message> messages = session->allMessages();
    QSet<QString> heartbeatTraceIds;
    for (const Message& msg : messages) {
        if (isHeartbeatPromptText(msg.content.text) && !msg.traceId.trimmed().isEmpty())
            heartbeatTraceIds.insert(msg.traceId.trimmed());
    }
    for (const Message& msg : messages) {
        if (msg.status == Message::Status::Cancelled
            || msg.status == Message::Status::Interrupted
            || msg.status == Message::Status::Error) {
            continue;
        }

        const QString content = msg.content.text;
        const QString trimmedContent = content.trimmed();
        const QString traceId = msg.traceId.trimmed();

        if (isHeartbeatPromptText(content))
            continue;
        if (!traceId.isEmpty()
            && heartbeatTraceIds.contains(traceId)
            && isHeartbeatNoChangeReplyText(content)) {
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
                QString toolName = msg.content.payload.value(QStringLiteral("tool_name")).toString().trimmed();
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
        Identity* sender = m_identityManager ? m_identityManager->findById(msg.senderId) : nullptr;
        const bool isUser = sender && sender->isUser();
        item.insert(QStringLiteral("role"), isUser ? QStringLiteral("user") : QStringLiteral("assistant"));
        item.insert(QStringLiteral("content"), content);
        history.append(item);
    }

    // 后处理：确保每个带 tool_calls 的 assistant 消息后都有完整的 tool result
    QJsonArray sanitized;
    for (int i = 0; i < history.size(); ++i) {
        QJsonObject msg = history[i].toObject();
        sanitized.append(msg);

        if (msg.value(QStringLiteral("role")).toString() != QLatin1String("assistant")
            || !msg.contains(QStringLiteral("tool_calls")))
            continue;

        QJsonArray toolCalls = msg[QStringLiteral("tool_calls")].toArray();
        QSet<QString> expectedIds;
        for (const QJsonValue& tc : toolCalls)
            expectedIds.insert(tc.toObject()[QStringLiteral("id")].toString());

        // 收集后续 tool 消息已覆盖的 id（不限于紧邻，跳过中间非 tool 消息）
        QSet<QString> foundIds;
        for (int j = i + 1; j < history.size(); ++j) {
            QJsonObject next = history[j].toObject();
            const QString nextRole = next.value(QStringLiteral("role")).toString();
            if (nextRole == QLatin1String("tool")) {
                foundIds.insert(next[QStringLiteral("tool_call_id")].toString());
            } else if (nextRole == QLatin1String("assistant") && next.contains(QStringLiteral("tool_calls"))) {
                break; // 遇到下一个带 tool_calls 的 assistant 消息，停止搜索
            }
        }

        // 补齐缺失的 tool result
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

AgentRuntime* ChatService::ensureRuntimeForAgent(Identity* agentIdentity)
{
    return m_runtimeManager->ensureRuntimeForAgent(agentIdentity);
}

void ChatService::releaseRuntimeIfUnused(const QString& agentIdentityId)
{
    m_runtimeManager->releaseRuntimeIfUnused(agentIdentityId);
    m_agentActiveSession.remove(agentIdentityId);
}

void ChatService::tryStartNextTurnForAgent(const QString& agentIdentityId)
{
    if (agentIdentityId.trimmed().isEmpty())
        return;

    if (!m_agentActiveSession.value(agentIdentityId).isEmpty())
        return;

    const QStringList sessionIds = m_turnManager.sessionIds();
    for (const QString& sid : sessionIds) {
        if (agentIdentityIdForSession(sid) != agentIdentityId)
            continue;
        tryStartNextTurn(sid);
        if (!m_agentActiveSession.value(agentIdentityId).isEmpty())
            break;
    }
}

void ChatService::resetSessionStreamState(const QString& sessionId)
{
    Session* session = m_sessionManager->findById(sessionId);
    if (!session)
        return;
    Session::StreamState& state = session->streamState();
    state.isStreaming = false;
    state.buffer.clear();
    state.hasPendingMessage = false;
    state.lastMsgIsTool = false;
}

void ChatService::flushPendingDeltaLog(const QString& sessionId, SessionPipeline* pipeline, const TurnTask* turn, bool force)
{
    if (m_logVerboseStreamEvents || !pipeline || pipeline->pendingDeltaLog.isEmpty())
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const int charCount = pipeline->pendingDeltaLog.size();
    const int chunkCount = pipeline->pendingDeltaChunks;
    const qint64 spanMs = pipeline->pendingDeltaStartedAtMs > 0 ? (nowMs - pipeline->pendingDeltaStartedAtMs) : 0;

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

    emitPipelineEvent(QStringLiteral("turn_delta_batch"), sessionId, turn, pipeline->pendingDeltaLog, QString(), extra, true);

    pipeline->pendingDeltaLog.clear();
    pipeline->pendingDeltaChunks = 0;
    pipeline->pendingDeltaStartedAtMs = 0;
    pipeline->lastDeltaFlushedAtMs = nowMs;
}

bool ChatService::appendEventLog(const QJsonObject& event) const
{
    return m_persistence && m_persistence->appendEventLog(event);
}

void ChatService::emitPipelineEvent(const QString& type, const QString& sessionId, const TurnTask* turn, const QString& delta, const QString& error, const QJsonObject& extra, bool persistToDisk)
{
    SessionPipeline* pipeline = findPipeline(sessionId);

    QJsonObject event;
    event.insert(QStringLiteral("event_schema_version"), 1);
    event.insert(QStringLiteral("type"), type);
    event.insert(QStringLiteral("sessionId"), sessionId);
    event.insert(QStringLiteral("session_id"), sessionId);
    event.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
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

    emit conversationEvent(event);
    if (persistToDisk && !appendEventLog(event)) {
        const QString logPath = m_persistence
            ? m_persistence->eventsCurrentLogPath()
            : QStringLiteral("<persistence-unavailable>");
        qWarning() << "[ChatService] 事件日志写入失败：" << logPath;
    }
}

void ChatService::appendRuntimeIoEventEntry(const QString& sessionId, const QString& type, const TurnTask* turn, const QString& error, const QJsonObject& extra)
{
    AgentRuntime* runtime = runtimeForSession(sessionId);
    if (!runtime)
        return;

    QJsonObject eventObj;
    eventObj.insert(QStringLiteral("type"), type);
    eventObj.insert(QStringLiteral("session_id"), sessionId);
    eventObj.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
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
    const QString requestId = turn
        ? QStringLiteral("event:%1:%2")
              .arg(turn->runId.isEmpty() ? type : turn->runId, type)
        : QStringLiteral("event:%1").arg(type);
    entry.insert(QStringLiteral("request_id"), requestId);
    entry.insert(QStringLiteral("event"), eventObj);

    runtime->appendIoHistoryEntry(sessionId, entry);
}

void ChatService::clearToolProgressCacheForSession(const QString& sessionId)
{
    const QString keyPrefix = sessionId.trimmed() + QStringLiteral("|");
    for (auto it = m_toolProgressLastPersistMsByKey.begin(); it != m_toolProgressLastPersistMsByKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_toolProgressLastPersistMsByKey.erase(it);
        else
            ++it;
    }
    for (auto it = m_toolProgressLastDigestByKey.begin(); it != m_toolProgressLastDigestByKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_toolProgressLastDigestByKey.erase(it);
        else
            ++it;
    }
}

void ChatService::tryStartNextTurn(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    if (!pipeline)
        return;
    if (m_turnManager.hasActiveTurn(sessionId)
        || m_turnManager.queuedTurnCount(sessionId) <= 0) {
        return;
    }

    AgentRuntime* runtime = ensureRuntimeForSession(sessionId);
    if (!runtime)
        return;
    const QString agentId = runtime->identityId().trimmed();
    if (agentId.isEmpty())
        return;

    const QString activeSessionId = m_agentActiveSession.value(agentId);
    if (!activeSessionId.isEmpty() && activeSessionId != sessionId)
        return; // 同一 Agent 的 Runtime 正在处理另一个会话

    TurnTask startedTurn;
    if (!m_turnManager.startNextTurn(sessionId, &startedTurn))
        return;
    if (startedTurn.requestTraceId.isEmpty()) {
        startedTurn.requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (TurnTask* active = m_turnManager.activeTurn(sessionId))
            active->requestTraceId = startedTurn.requestTraceId;
    }
    m_agentActiveSession.insert(agentId, sessionId);

    Session* session = m_sessionManager->findById(sessionId);
    QJsonArray runtimeHistory = buildRuntimeHistoryFromMessages(session);
    runtime->setHistory(runtimeHistory);
    if (!runtimeHistory.isEmpty()) {
        const QJsonObject first = runtimeHistory.first().toObject();
        const QString firstRole = first.value(QStringLiteral("role")).toString();
        const QString firstContent = first.value(QStringLiteral("content")).toString();
        if (firstRole == QLatin1String("system")
            && firstContent.startsWith(QStringLiteral("[Context Compact]"))) {
            QJsonObject compactExtra;
            compactExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
            compactExtra.insert(QStringLiteral("historyChars"), estimateHistoryChars(runtimeHistory));
            emitPipelineEvent(QStringLiteral("context.compacted"), sessionId, &startedTurn, QString(), QString(), compactExtra);
        }
    }

    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.clear();
        state.hasPendingMessage = false;
        state.lastMsgIsTool = false;
        state.isStreaming = true;
    }

    emitPipelineEvent(QStringLiteral("turn_started"), sessionId, &startedTurn);
    reportPulseProgress(agentId, QStringLiteral("turn_started"));

    ensureMemoryInitializedForAgent(runtime->identity());
    LLMConfig runtimeConfig = composeConfigForIdentity(runtime->identity());
    if (m_memoryManager) {
        const QString memoryContext = m_memoryManager->composeMemoryContext(agentId, kMemoryContextMaxChars);
        if (!memoryContext.isEmpty()) {
            if (runtimeConfig.systemPrompt.trimmed().isEmpty()) {
                runtimeConfig.systemPrompt = memoryContext;
            } else {
                runtimeConfig.systemPrompt = runtimeConfig.systemPrompt.trimmed()
                    + QStringLiteral("\n\n")
                    + memoryContext;
            }
            QJsonObject extra;
            extra.insert(QStringLiteral("memoryContextChars"), memoryContext.size());
            emitPipelineEvent(QStringLiteral("memory.recalled"), sessionId, &startedTurn, QString(), QString(), extra);
        }
    }

    // P1: 注入活跃子 Agent Job 上下文
    {
        const QString delegateContext = DelegateTaskScheduler::instance()
                                            ->formatActiveJobsContext(agentId);
        if (!delegateContext.isEmpty()) {
            if (runtimeConfig.systemPrompt.trimmed().isEmpty()) {
                runtimeConfig.systemPrompt = delegateContext;
            } else {
                runtimeConfig.systemPrompt = runtimeConfig.systemPrompt.trimmed()
                    + QStringLiteral("\n\n")
                    + delegateContext;
            }
        }
    }
    {
        QJsonObject dispatchExtra;
        const QString modelId = m_modelFactory ? m_modelFactory->resolveModelId(runtimeConfig) : runtimeConfig.selectedModelId.trimmed();
        if (!modelId.isEmpty())
            dispatchExtra.insert(QStringLiteral("model"), modelId);
        dispatchExtra.insert(QStringLiteral("historyMessages"), runtimeHistory.size());
        dispatchExtra.insert(QStringLiteral("historyChars"), estimateHistoryChars(runtimeHistory));
        emitPipelineEvent(QStringLiteral("turn_dispatch_prepare"), sessionId, &startedTurn, QString(), QString(), dispatchExtra);
    }
    runtime->setConfig(runtimeConfig);
    emitPipelineEvent(QStringLiteral("turn_dispatch_config_applied"), sessionId, &startedTurn);

    runtime->sendMessage(sessionId, startedTurn.userContent);
    emitPipelineEvent(QStringLiteral("turn_dispatch_sent"), sessionId, &startedTurn);
}

void ChatService::finalizeTurn(const QString& sessionId, TurnTask* outTurn)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (pipeline && activeTurn)
        flushPendingDeltaLog(sessionId, pipeline, activeTurn, true);

    m_turnManager.clearActiveTurn(sessionId, outTurn);

    // 清理当前会话中未回收的委派工具起始时间，避免打断后残留脏状态。
    const QString keyPrefix = sessionId.trimmed() + QStringLiteral("|");
    for (auto it = m_delegateStartMsByToolKey.begin(); it != m_delegateStartMsByToolKey.end();) {
        if (it.key().startsWith(keyPrefix))
            it = m_delegateStartMsByToolKey.erase(it);
        else
            ++it;
    }
    clearToolProgressCacheForSession(sessionId);

    const QString agentId = agentIdentityIdForSession(sessionId);
    if (!agentId.isEmpty() && m_agentActiveSession.value(agentId) == sessionId)
        m_agentActiveSession.remove(agentId);
    resetSessionStreamState(sessionId);

    tryStartNextTurn(sessionId);
    if (!agentId.isEmpty())
        tryStartNextTurnForAgent(agentId);
}

void ChatService::onRuntimeStreamData(const QString& sessionId, const QString& data)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (!pipeline || !activeTurn)
        return;

    activeTurn->assistantContent.append(data);
    const bool backgroundHeartbeat = isBackgroundHeartbeatClientMessageId(activeTurn->clientMessageId);
    reportPulseProgress(agentIdentityIdForSession(sessionId), QStringLiteral("stream"));

    if (backgroundHeartbeat)
        return;

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.append(data);
        state.isStreaming = true;
    }

    emit streamDataReceived(sessionId, data);
    emitPipelineEvent(QStringLiteral("turn_delta"), sessionId, activeTurn, data, QString(), QJsonObject(), m_logVerboseStreamEvents);

    if (!m_logVerboseStreamEvents && !data.isEmpty()) {
        if (pipeline->pendingDeltaLog.isEmpty())
            pipeline->pendingDeltaStartedAtMs = QDateTime::currentMSecsSinceEpoch();
        pipeline->pendingDeltaLog.append(data);
        ++pipeline->pendingDeltaChunks;
        flushPendingDeltaLog(sessionId, pipeline, activeTurn, false);
    }
}

void ChatService::onRuntimeFinished(const QString& sessionId, const QString& fullContent)
{
    TurnTask finishedTurn;
    finalizeTurn(sessionId, &finishedTurn);
    if (!fullContent.isEmpty())
        finishedTurn.assistantContent = fullContent;

    const QString agentId = agentIdentityIdForSession(sessionId);
    const bool skipPersistForBackgroundHeartbeat = isBackgroundHeartbeatClientMessageId(finishedTurn.clientMessageId);
    reportPulseProgress(agentId, QStringLiteral("finished"));
    if (!skipPersistForBackgroundHeartbeat
        && !finishedTurn.assistantContent.trimmed().isEmpty()
        && !agentId.isEmpty()) {
        Message assistantMsg = Message::createText(sessionId, agentId, finishedTurn.assistantContent);
        assistantMsg.traceId = finishedTurn.requestTraceId;
        assistantMsg.turnId = finishedTurn.turnId;
        assistantMsg.status = Message::Status::Completed;
        m_sessionManager->postMessage(sessionId, assistantMsg);
    }

    QJsonObject extra;
    extra.insert(QStringLiteral("fullContent"), finishedTurn.assistantContent);
    if (!skipPersistForBackgroundHeartbeat)
        emit finished(sessionId, finishedTurn.assistantContent);
    emitPipelineEvent(QStringLiteral("turn_completed"), sessionId, &finishedTurn, QString(), QString(), extra);

    const bool skipMemoryForHeartbeat = skipPersistForBackgroundHeartbeat;
    if (skipMemoryForHeartbeat) {
        QJsonObject memoryExtra;
        memoryExtra.insert(QStringLiteral("reason"), QStringLiteral("heartbeat_turn"));
        emitPipelineEvent(QStringLiteral("memory.skipped"), sessionId, &finishedTurn, QString(), QString(), memoryExtra);
    } else if (m_memoryManager && !agentId.isEmpty()) {
        QString memorySummary;
        QString memoryPath;
        QJsonObject memoryMetadata;
        QString memoryError;
        const bool retained = m_memoryManager->retainTurn(
            agentId, sessionId, finishedTurn, &memorySummary, &memoryPath, &memoryMetadata, &memoryError);
        if (retained) {
            if (!memorySummary.trimmed().isEmpty()) {
                QJsonObject memoryExtra;
                memoryExtra.insert(QStringLiteral("doc_type"), QStringLiteral("daily"));
                memoryExtra.insert(QStringLiteral("summary"), memorySummary);
                memoryExtra.insert(QStringLiteral("path"), memoryPath);
                for (auto it = memoryMetadata.constBegin(); it != memoryMetadata.constEnd(); ++it)
                    memoryExtra.insert(it.key(), it.value());
                emitPipelineEvent(QStringLiteral("memory.updated"), sessionId, &finishedTurn, QString(), QString(), memoryExtra);
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
                emitPipelineEvent(QStringLiteral("memory.compacted"), sessionId, &finishedTurn, QString(), QString(), compactExtra);
            }

            refreshMemoryIndexAndEmit(sessionId, agentId, &finishedTurn, QStringLiteral("retain_turn"), memoryPath, memoryMetadata);
            maybeReflectMemoryAndEmit(sessionId, agentId, finishedTurn);
        } else {
            QJsonObject memoryExtra;
            memoryExtra.insert(QStringLiteral("doc_type"), QStringLiteral("daily"));
            memoryExtra.insert(QStringLiteral("path"), memoryPath);
            emitPipelineEvent(QStringLiteral("memory.error"), sessionId, &finishedTurn, QString(), memoryError.isEmpty() ? QStringLiteral("memory retain failed") : memoryError, memoryExtra);
        }
    }
}

void ChatService::onRuntimeError(const QString& sessionId, const QString& errorMsg)
{
    TurnTask failedTurn;
    finalizeTurn(sessionId, &failedTurn);
    const bool skipNotifyForBackgroundHeartbeat = isBackgroundHeartbeatClientMessageId(failedTurn.clientMessageId);

    const QString agentId = agentIdentityIdForSession(sessionId);
    reportPulseProgress(agentId, QStringLiteral("error"));
    const bool transientError = isTransientUpstreamError(errorMsg);
    if (transientError && !agentId.isEmpty()) {
        const QList<DelegateTaskScheduler::JobInfo> activeJobs = DelegateTaskScheduler::instance()->listJobs(agentId, true, 5);
        if (!activeJobs.isEmpty()) {
            const QString fallbackReply = buildDelegateRecoveryReply(activeJobs);

            Message assistantMsg = Message::createText(sessionId, agentId, fallbackReply);
            assistantMsg.traceId = failedTurn.requestTraceId;
            assistantMsg.turnId = failedTurn.turnId;
            assistantMsg.status = Message::Status::Completed;
            m_sessionManager->postMessage(sessionId, assistantMsg);

            QJsonObject extra;
            extra.insert(QStringLiteral("recovered"), true);
            extra.insert(QStringLiteral("reason"), QStringLiteral("transient_upstream_error_with_active_delegate_jobs"));
            extra.insert(QStringLiteral("active_delegate_jobs"), activeJobs.size());
            QJsonArray jobIds;
            for (const DelegateTaskScheduler::JobInfo& job : activeJobs) {
                const QString jobId = job.jobId.trimmed();
                if (!jobId.isEmpty())
                    jobIds.append(jobId);
            }
            extra.insert(QStringLiteral("job_ids"), jobIds);
            extra.insert(QStringLiteral("error"), errorMsg);

            emit finished(sessionId, fallbackReply);
            emitPipelineEvent(QStringLiteral("turn_recovered"), sessionId, &failedTurn, QString(), errorMsg, extra);
            emitPipelineEvent(QStringLiteral("turn_failed"), sessionId, &failedTurn, QString(), errorMsg, extra);
            return;
        }
    }

    if (!skipNotifyForBackgroundHeartbeat)
        emit errorOccurred(sessionId, errorMsg);
    emitPipelineEvent(QStringLiteral("turn_failed"), sessionId, &failedTurn, QString(), errorMsg);
}

void ChatService::onRuntimeToolCallsStarted(const QString& sessionId)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (!pipeline || !activeTurn)
        return;

    flushPendingDeltaLog(sessionId, pipeline, activeTurn, true);

    Session* session = m_sessionManager->findById(sessionId);
    if (session) {
        Session::StreamState& state = session->streamState();
        state.buffer.clear();
        state.lastMsgIsTool = true;
    }

    emit toolCallsStarted(sessionId);
    emitPipelineEvent(QStringLiteral("turn_tool_calls_started"), sessionId, activeTurn);
}

void ChatService::onRuntimeToolEvent(const QString& sessionId, const ToolExecutionEvent& event)
{
    SessionPipeline* pipeline = findPipeline(sessionId);
    TurnTask* activeTurn = m_turnManager.activeTurn(sessionId);
    if (!pipeline || !activeTurn)
        return;

    const QString agentId = agentIdentityIdForSession(sessionId);
    reportPulseProgress(agentId, QStringLiteral("tool_event"));
    const QString toolName = event.toolName.trimmed();
    const QString toolId = event.toolId.trimmed();
    const bool isDelegateTool = (toolName == QLatin1String("delegate_task"));

    if (isDelegateTool) {
        if (event.status == QLatin1String("started")) {
            if (m_heartbeatService && !agentId.isEmpty())
                m_heartbeatService->suppressHeartbeat(agentId, QStringLiteral("delegate_running"));
            if (!toolId.isEmpty())
                m_delegateStartMsByToolKey.insert(delegateToolKey(sessionId, toolId), QDateTime::currentMSecsSinceEpoch());

            QJsonObject delegateExtra;
            delegateExtra.insert(QStringLiteral("toolName"), toolName);
            delegateExtra.insert(QStringLiteral("toolId"), toolId);
            delegateExtra.insert(QStringLiteral("event"), QStringLiteral("started"));
            const QString taskPreview = event.data.value(QStringLiteral("task")).toString().trimmed();
            if (!taskPreview.isEmpty()) {
                delegateExtra.insert(
                    QStringLiteral("task_preview"),
                    taskPreview.left(200));
            }
            const QString rolePrompt = event.data.value(QStringLiteral("role_prompt")).toString().trimmed();
            if (!rolePrompt.isEmpty())
                delegateExtra.insert(QStringLiteral("role_prompt_preview"), rolePrompt.left(120));
            const QString parentAgentId = event.data.value(QStringLiteral("_agent_id")).toString().trimmed();
            if (!parentAgentId.isEmpty())
                delegateExtra.insert(QStringLiteral("parent_agent_id"), parentAgentId);
            emitPipelineEvent(QStringLiteral("delegate.tool_started"), sessionId, activeTurn, QString(), QString(), delegateExtra);
        } else if (event.status == QLatin1String("completed")) {
            if (m_heartbeatService && !agentId.isEmpty())
                m_heartbeatService->unsuppressHeartbeat(agentId);
            qint64 durationMs = -1;
            if (!toolId.isEmpty()) {
                const QString key = delegateToolKey(sessionId, toolId);
                if (m_delegateStartMsByToolKey.contains(key)) {
                    durationMs = QDateTime::currentMSecsSinceEpoch() - m_delegateStartMsByToolKey.value(key);
                    m_delegateStartMsByToolKey.remove(key);
                }
            }

            DelegateStats stats = m_delegateStatsBySession.value(sessionId);
            ++stats.totalCount;
            if (event.success)
                ++stats.successCount;
            else
                ++stats.failureCount;
            if (durationMs >= 0)
                stats.totalDurationMs += durationMs;
            m_delegateStatsBySession.insert(sessionId, stats);

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
                delegateExtra.insert(
                    QStringLiteral("delegate_avg_duration_ms"),
                    static_cast<double>(stats.totalDurationMs) / measuredCount);
            }
            if (!event.formattedResult.trimmed().isEmpty())
                delegateExtra.insert(QStringLiteral("summary"), event.formattedResult.trimmed());
            const auto copyDelegateMetric = [&](const QString& key) {
                if (event.data.contains(key))
                    delegateExtra.insert(key, event.data.value(key));
            };
            const QString childRequestId = event.data.value(QStringLiteral("child_request_id")).toString().trimmed();
            if (!childRequestId.isEmpty())
                delegateExtra.insert(QStringLiteral("child_request_id"), childRequestId);
            const QString childTraceId = event.data.value(QStringLiteral("child_trace_id")).toString().trimmed();
            if (!childTraceId.isEmpty())
                delegateExtra.insert(QStringLiteral("child_trace_id"), childTraceId);
            const QString childAgentId = event.data.value(QStringLiteral("child_agent_id")).toString().trimmed();
            if (!childAgentId.isEmpty())
                delegateExtra.insert(QStringLiteral("child_agent_id"), childAgentId);
            const QString childModel = event.data.value(QStringLiteral("child_model")).toString().trimmed();
            if (!childModel.isEmpty())
                delegateExtra.insert(QStringLiteral("child_model"), childModel);
            const QString childFinishReason = event.data.value(QStringLiteral("child_finish_reason")).toString().trimmed();
            if (!childFinishReason.isEmpty())
                delegateExtra.insert(QStringLiteral("child_finish_reason"), childFinishReason);
            const QString failureReason = event.data.value(QStringLiteral("failure_reason")).toString().trimmed();
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

            const QString eventType = event.success
                ? QStringLiteral("delegate.tool_completed")
                : QStringLiteral("delegate.tool_failed");
            emitPipelineEvent(
                eventType,
                sessionId,
                activeTurn,
                QString(),
                event.success ? QString() : event.rawResult.left(300),
                delegateExtra);
        }
    }

    if (m_sessionManager && !agentId.isEmpty()) {
        if (event.status == QLatin1String("started")) {
            if (toolId.isEmpty()) {
                qWarning() << "[ChatService] 跳过无效 tool_call 事件：缺少 toolId，session=" << sessionId
                           << "toolName=" << toolName;
            } else {
                Message toolCallMsg = Message::createToolCall(sessionId, agentId, QString(), QJsonObject());
                toolCallMsg.content.text.clear(); // 不在聊天区展示，仅用于上下文重建
                toolCallMsg.traceId = activeTurn->requestTraceId;
                toolCallMsg.turnId = activeTurn->turnId;
                toolCallMsg.status = Message::Status::Completed;
                toolCallMsg.content.payload.insert(QStringLiteral("tool_name"), toolName);
                toolCallMsg.content.payload.insert(QStringLiteral("tool_call_id"), toolId);
                toolCallMsg.content.payload.insert(
                    QStringLiteral("arguments"),
                    sanitizePersistedToolArguments(toolName, event.data));
                m_sessionManager->postMessage(sessionId, toolCallMsg);
            }
        } else if (event.status == QLatin1String("completed")) {
            const bool isPseudoToolEvent = (toolName == QLatin1String("tool_loop_guard"));
            if (toolId.isEmpty() || isPseudoToolEvent) {
                qWarning() << "[ChatService] 跳过无效 tool_result 事件：session=" << sessionId
                           << "toolName=" << toolName
                           << "toolId=" << toolId;
            } else {
                Message toolResultMsg = Message::createToolResult(sessionId, agentId, toolId, QString());
                toolResultMsg.content.text.clear(); // 避免污染聊天气泡
                toolResultMsg.traceId = activeTurn->requestTraceId;
                toolResultMsg.turnId = activeTurn->turnId;
                toolResultMsg.status = Message::Status::Completed;
                toolResultMsg.content.payload.insert(QStringLiteral("tool_name"), toolName);
                toolResultMsg.content.payload.insert(QStringLiteral("success"), event.success);
                const QString safeRawResult = sanitizePersistedToolRawResult(toolName, event.rawResult);
                toolResultMsg.content.payload.insert(QStringLiteral("raw_result"), safeRawResult);
                toolResultMsg.content.payload.insert(QStringLiteral("formatted_result"), event.formattedResult);
                const QJsonObject persistedEventData = sanitizePersistedToolEventData(toolName, event.data);
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
                m_sessionManager->postMessage(sessionId, toolResultMsg);
            }
        }
    }

    // ---- send_file 工具特殊处理：将临时文件移到 session 目录，生成 File 类型消息 ----
    if (toolName == QLatin1String("send_file")
        && event.status == QLatin1String("completed")
        && event.success
        && !event.data.isEmpty()) {
        const QString tmpFilePath = event.data.value(QStringLiteral("file_path")).toString();
        const QString fileName = event.data.value(QStringLiteral("file_name")).toString();
        const qint64 fileSize = static_cast<qint64>(event.data.value(QStringLiteral("file_size")).toDouble());
        const QString description = event.data.value(QStringLiteral("description")).toString();
        if (!tmpFilePath.isEmpty() && !fileName.isEmpty()) {
            // 将文件从临时目录移到 session 数据目录下的 files/<uuid>/
            QString finalFilePath = tmpFilePath;
            const QString sessionDir = m_persistence->sessionDataDirPath(sessionId);
            if (!sessionDir.isEmpty()) {
                const QString filesDir = sessionDir + QStringLiteral("/files/")
                    + QUuid::createUuid().toString(QUuid::WithoutBraces);
                QDir().mkpath(filesDir);
                const QString destPath = QDir(filesDir).filePath(fileName);
                if (QFile::rename(tmpFilePath, destPath)) {
                    finalFilePath = destPath;
                    // 清理临时目录（rename 后源目录为空）
                    QDir tmpDir(QFileInfo(tmpFilePath).absolutePath());
                    tmpDir.removeRecursively();
                }
            }
            Message fileMsg = Message::createFile(sessionId, agentId, finalFilePath, fileName, fileSize, description);
            fileMsg.traceId = activeTurn->requestTraceId;
            fileMsg.turnId = activeTurn->turnId;
            m_sessionManager->postMessage(sessionId, fileMsg);
        }
    }

    emit toolEvent(sessionId, event);

    bool persistToolEvent = true;
    QJsonObject eventObj = toolEventToJson(event);
    if (event.status == QLatin1String("progress")) {
        // 进度事件只用于观测，不应影响主链上下文；同时做磁盘节流避免日志膨胀。
        QString progressDigest = event.formattedResult;
        progressDigest.replace(QLatin1Char('\r'), QLatin1Char(' '));
        progressDigest.replace(QLatin1Char('\n'), QLatin1Char(' '));
        progressDigest = progressDigest.simplified();
        if (progressDigest.size() > 160)
            progressDigest = progressDigest.left(160) + QStringLiteral("...");

        const QString progressKey = QStringLiteral("%1|%2|%3|%4")
                                        .arg(sessionId.trimmed(), activeTurn->runId.trimmed(), toolName, toolId.isEmpty() ? QStringLiteral("_") : toolId);
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 lastMs = m_toolProgressLastPersistMsByKey.value(progressKey, 0);
        const QString lastDigest = m_toolProgressLastDigestByKey.value(progressKey);
        const bool due = (lastMs <= 0) || ((nowMs - lastMs) >= kToolProgressPersistMinIntervalMs);
        const bool changed = (!progressDigest.isEmpty() && progressDigest != lastDigest);
        persistToolEvent = due || changed;

        if (persistToolEvent) {
            m_toolProgressLastPersistMsByKey.insert(progressKey, nowMs);
            m_toolProgressLastDigestByKey.insert(progressKey, progressDigest);
        }

        QString clippedFormatted = event.formattedResult;
        if (clippedFormatted.size() > 320)
            clippedFormatted = clippedFormatted.left(320) + QStringLiteral("...");
        eventObj.insert(QStringLiteral("formattedResult"), clippedFormatted);
        eventObj.insert(QStringLiteral("rawResult"), QString());
    }

    QJsonObject extra;
    extra.insert(QStringLiteral("toolEvent"), eventObj);
    emitPipelineEvent(QStringLiteral("turn_tool_event"), sessionId, activeTurn, QString(), QString(), extra, persistToolEvent);
}

void ChatService::connectRuntimeSignals(AgentRuntime* runtime)
{
    if (!runtime)
        return;
    ensureAgentPulse(runtime->identityId());
    connect(runtime, &AgentRuntime::streamDataReceived, this, &ChatService::onRuntimeStreamData);
    connect(runtime, &AgentRuntime::finished, this, &ChatService::onRuntimeFinished);
    connect(runtime, &AgentRuntime::errorOccurred, this, &ChatService::onRuntimeError);
    connect(runtime, &AgentRuntime::toolCallsStarted, this, &ChatService::onRuntimeToolCallsStarted);
    connect(runtime, &AgentRuntime::toolEvent, this, &ChatService::onRuntimeToolEvent);
    connect(runtime, &AgentRuntime::reasoningStarted, this, &ChatService::reasoningStarted);
    connect(runtime, &AgentRuntime::reasoningStopped, this, &ChatService::reasoningStopped);
}

bool ChatService::isUserIdentity(const QString& identityId) const
{
    if (!m_identityManager || identityId.trimmed().isEmpty())
        return false;
    Identity* identity = m_identityManager->findById(identityId);
    return identity && identity->isUser();
}

void ChatService::refreshMemoryIndexAndEmit(const QString& sessionId, const QString& agentId, const TurnTask* turn, const QString& reason, const QString& sourcePath, const QJsonObject& sourceMetadata)
{
    if (!m_memoryManager)
        return;

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return;

    QJsonObject indexMetadata;
    QString indexError;
    const bool ok = m_memoryManager->rebuildSearchIndex(trimmedAgentId, &indexMetadata, &indexError);

    // Build common extra fields
    QJsonObject extra;
    extra.insert(QStringLiteral("agent_id"), trimmedAgentId);
    extra.insert(QStringLiteral("reason"), reason.trimmed().isEmpty() ? QStringLiteral("unknown") : reason.trimmed());
    if (!sourcePath.trimmed().isEmpty())
        extra.insert(QStringLiteral("source_path"), sourcePath);
    const QString longMemoryPath = sourceMetadata.value(QStringLiteral("longMemoryPath")).toString().trimmed();
    if (!longMemoryPath.isEmpty())
        extra.insert(QStringLiteral("longMemoryPath"), longMemoryPath);

    if (ok) {
        for (auto it = indexMetadata.constBegin(); it != indexMetadata.constEnd(); ++it)
            extra.insert(it.key(), it.value());
        emitPipelineEvent(QStringLiteral("memory.index.updated"), sessionId, turn, QString(), QString(), extra);
    } else {
        emitPipelineEvent(QStringLiteral("memory.index.error"), sessionId, turn, QString(), indexError.trimmed().isEmpty() ? QStringLiteral("memory index rebuild failed") : indexError.trimmed(), extra);
    }
}

void ChatService::maybeReflectMemoryAndEmit(const QString& sessionId, const QString& agentId, const TurnTask& turn)
{
    if (!m_memoryManager)
        return;

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return;
    if (!m_memoryManager->reflectionEnabled())
        return;

    const int interval = m_memoryManager->reflectionIntervalTurns();
    if (interval <= 0)
        return;

    const int retainedTurns = m_memoryRetainedTurnsByAgent.value(trimmedAgentId, 0) + 1;
    m_memoryRetainedTurnsByAgent.insert(trimmedAgentId, retainedTurns);
    if ((retainedTurns % interval) != 0)
        return;

    QString summary;
    QString writtenPath;
    QJsonObject reflectMetadata;
    QString reflectError;
    const bool reflected = m_memoryManager->reflectAndScore(trimmedAgentId, sessionId, turn.turnId, turn.requestTraceId, &summary, &writtenPath, &reflectMetadata, &reflectError);
    if (!reflected) {
        QJsonObject extra;
        extra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
        extra.insert(QStringLiteral("path"), writtenPath);
        extra.insert(QStringLiteral("reflection"), true);
        extra.insert(QStringLiteral("reflection_interval_turns"), interval);
        extra.insert(QStringLiteral("retained_turn_count"), retainedTurns);
        emitPipelineEvent(QStringLiteral("memory.error"), sessionId, &turn, QString(), reflectError.isEmpty() ? QStringLiteral("memory reflection failed") : reflectError, extra);
        return;
    }

    QJsonObject extra = reflectMetadata;
    extra.insert(QStringLiteral("doc_type"), QStringLiteral("long_term"));
    extra.insert(QStringLiteral("summary"), summary);
    extra.insert(QStringLiteral("path"), writtenPath);
    extra.insert(QStringLiteral("reflection"), true);
    extra.insert(QStringLiteral("reflection_interval_turns"), interval);
    extra.insert(QStringLiteral("retained_turn_count"), retainedTurns);
    emitPipelineEvent(QStringLiteral("memory.reflected"), sessionId, &turn, QString(), QString(), extra);

    QJsonObject qualityExtra = extra;
    qualityExtra.insert(QStringLiteral("quality_score"), reflectMetadata.value(QStringLiteral("quality_score")).toInt());
    qualityExtra.insert(QStringLiteral("quality_level"), reflectMetadata.value(QStringLiteral("quality_level")).toString());
    emitPipelineEvent(QStringLiteral("memory.quality"), sessionId, &turn, QString(), QString(), qualityExtra);

    const int longMemoryAdded = reflectMetadata.value(QStringLiteral("longMemoryAdded")).toInt();
    if (longMemoryAdded > 0) {
        refreshMemoryIndexAndEmit(sessionId, trimmedAgentId, &turn, QStringLiteral("reflect_turn"), writtenPath, reflectMetadata);
    }
}

QString ChatService::resolvePrimarySessionForAgent(const QString& agentId, bool createIfMissing, bool isolated, const QString& titleSuffix)
{
    if (!m_sessionManager || !m_identityManager)
        return QString();

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return QString();

    const QList<Session*> sessions = m_sessionManager->sessionsForIdentity(trimmedAgentId);
    Session* best = nullptr;
    for (Session* session : sessions) {
        if (!session)
            continue;
        if (!best || session->lastActiveAt() > best->lastActiveAt())
            best = session;
    }
    if (best && !isolated)
        return best->id();

    if (!createIfMissing && !isolated)
        return best ? best->id() : QString();

    Identity* identity = m_identityManager->findById(trimmedAgentId);
    if (!identity || !identity->isAgent())
        return QString();

    const QString userId = m_identityManager->userIdentity()->id();
    QString title = identity->name();
    const QString suffix = titleSuffix.trimmed();
    if (!suffix.isEmpty())
        title = QStringLiteral("%1 [%2]").arg(title, suffix);
    Session* session = createSessionForIdentityAs(userId, trimmedAgentId, title);
    return session ? session->id() : QString();
}

QString ChatService::buildHeartbeatPrompt(const QString& agentId, const QString& reason) const
{
    QString instruction;
    if (m_heartbeatService) {
        const QString path = m_heartbeatService->heartbeatPathForAgent(agentId);
        if (!path.trimmed().isEmpty()) {
            repairHeartbeatFileIfNeeded(path);
            QFile file(path);
            if (file.exists() && file.open(QFile::ReadOnly | QFile::Text)) {
                instruction = decodePossiblyMojibakeUtf8(file.readAll()).trimmed();
                file.close();
            }
        }
    }

    if (instruction.isEmpty()) {
        instruction = QStringLiteral(
            "请执行一次轻量心跳巡检：\n"
            "1) 回顾最近任务进度；\n"
            "2) 若有后台委派任务，汇总当前状态；\n"
            "3) 若无重要变更，默认静默（不发聊天消息）；手动触发时可简短回复“当前无关键更新”。");
    }

    const QString reasonLabel = reason.trimmed().isEmpty()
        ? QStringLiteral("interval")
        : reason.trimmed();
    return QStringLiteral("【系统心跳任务】reason=%1\n%2").arg(reasonLabel, instruction);
}

// P0: 子 Agent 完成自动通知
void ChatService::onDelegateJobSettled(const QString& jobId, const QString& ownerAgentId, bool success, const QString& result)
{
    if (ownerAgentId.trimmed().isEmpty())
        return;

    // 构造通知消息
    QString notification = QStringLiteral(
                               "[子代理任务完成通知]\n"
                               "job_id: %1\n"
                               "状态: %2\n")
                               .arg(jobId, success ? QStringLiteral("成功") : QStringLiteral("失败"));

    if (!result.trimmed().isEmpty())
        notification += QStringLiteral("结果摘要: %1\n").arg(result.left(500));

    // 找到 ownerAgentId 对应的 session 并注入系统通知
    const QString sessionId = resolvePrimarySessionForAgent(ownerAgentId, false, false);
    if (!sessionId.isEmpty() && m_sessionManager) {
        Message notifyMsg = Message::createSystem(sessionId, notification);
        m_sessionManager->postMessage(sessionId, notifyMsg);
    }

    // 触发即时心跳让父 Agent 感知
    if (m_heartbeatService)
        m_heartbeatService->triggerHeartbeat(ownerAgentId, QStringLiteral("delegate_job_settled"));

    QJsonObject extra;
    extra.insert(QStringLiteral("job_id"), jobId);
    extra.insert(QStringLiteral("owner_agent_id"), ownerAgentId);
    extra.insert(QStringLiteral("success"), success);
    emitPipelineEvent(QStringLiteral("delegate.job_settled"), sessionId, nullptr, QString(), QString(), extra);
}

void ChatService::onHeartbeatTriggered(const QString& agentId, const QString& reason)
{
    if (!m_identityManager)
        return;

    Identity* agent = m_identityManager->findById(agentId);
    if (!agent || !agent->isAgent())
        return;

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return;

    const QString reasonLabel = reason.trimmed().isEmpty()
        ? QStringLiteral("interval")
        : reason.trimmed();
    const bool forceInteractive = (reasonLabel == QLatin1String("manual_ui") || reasonLabel == QLatin1String("requested"));

    HeartbeatConfig hbCfg;
    if (m_heartbeatService)
        hbCfg = m_heartbeatService->configForAgent(trimmedAgentId);
    hbCfg.snapshotSignals = normalizeHeartbeatSignals(hbCfg.snapshotSignals);
    const QSet<QString> enabledSignals(hbCfg.snapshotSignals.begin(), hbCfg.snapshotSignals.end());
    const bool watchProvider = enabledSignals.contains(QStringLiteral("provider_status"));
    const bool watchDelegate = enabledSignals.contains(QStringLiteral("delegate_jobs"));
    const bool watchPulse = enabledSignals.contains(QStringLiteral("pulse_state"));
    const bool watchScheduler = enabledSignals.contains(QStringLiteral("scheduler_jobs"));
    const bool watchMemory = enabledSignals.contains(QStringLiteral("memory_progress"));

    QString providerId;
    bool providerDown = false;
    if (watchProvider && m_healthMonitor) {
        const LLMConfig cfg = composeConfigForIdentity(agent);
        providerId = ModelFactory::resolveInstanceId(cfg);
        providerDown = (!providerId.isEmpty() && m_healthMonitor->isProviderDown(providerId));
    }

    const QList<DelegateTaskScheduler::JobInfo> activeJobs = DelegateTaskScheduler::instance()->listJobs(trimmedAgentId, true, 50);

    QJsonObject snapshot;
    QJsonArray signalArr;
    for (const QString& s : hbCfg.snapshotSignals)
        signalArr.append(s);
    snapshot.insert(QStringLiteral("watch_signals"), signalArr);
    if (watchProvider) {
        snapshot.insert(QStringLiteral("provider_down"), providerDown);
        snapshot.insert(QStringLiteral("provider_id"), providerId);
    }
    if (watchDelegate) {
        snapshot.insert(QStringLiteral("active_jobs_count"), activeJobs.size());
        QJsonArray jobsArr;
        for (const DelegateTaskScheduler::JobInfo& job : activeJobs) {
            QJsonObject item;
            item.insert(QStringLiteral("job_id"), job.jobId.trimmed());
            item.insert(QStringLiteral("status"), job.status.trimmed());
            item.insert(QStringLiteral("summary"), job.summary.left(120));
            jobsArr.append(item);
            if (jobsArr.size() >= 10)
                break;
        }
        snapshot.insert(QStringLiteral("active_jobs"), jobsArr);
    }
    if (watchPulse) {
        ensureAgentPulse(trimmedAgentId);
        AgentPulse* pulse = m_agentPulses.value(trimmedAgentId, nullptr);
        if (pulse) {
            snapshot.insert(QStringLiteral("pulse_state"), pulseStateToString(pulse->currentState()));
        }
    }
    if (watchScheduler && m_schedulerService) {
        int enabledCount = 0;
        QDateTime nearestNext;
        const QList<ScheduledJob> jobs = m_schedulerService->allJobs();
        for (const ScheduledJob& job : jobs) {
            if (job.agentId.trimmed() != trimmedAgentId)
                continue;
            if (job.enabled)
                ++enabledCount;
            if (job.nextFireAtUtc.isValid()
                && (!nearestNext.isValid() || job.nextFireAtUtc < nearestNext)) {
                nearestNext = job.nextFireAtUtc;
            }
        }
        snapshot.insert(QStringLiteral("scheduler_enabled_jobs"), enabledCount);
        if (nearestNext.isValid())
            snapshot.insert(QStringLiteral("scheduler_next_fire_at_utc"), nearestNext.toUTC().toString(Qt::ISODateWithMs));
    }
    if (watchMemory) {
        snapshot.insert(QStringLiteral("memory_retained_turns"), m_memoryRetainedTurnsByAgent.value(trimmedAgentId, 0));
        const QString memoryMdPath = QDir(QDir(m_persistence ? m_persistence->agentsDirPath() : QString())
                                              .filePath(trimmedAgentId))
                                         .filePath(QStringLiteral("memory.md"));
        if (!memoryMdPath.trimmed().isEmpty() && QFile::exists(memoryMdPath))
            snapshot.insert(QStringLiteral("memory_doc_size_bytes"), QFileInfo(memoryMdPath).size());
    }
    const QByteArray snapshotBytes = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
    const QString snapshotDigest = QString::fromLatin1(
        QCryptographicHash::hash(snapshotBytes, QCryptographicHash::Sha1).toHex());

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    HeartbeatRuntimeState& runtimeState = m_heartbeatRuntimeByAgent[trimmedAgentId];
    if (!runtimeState.loaded) {
        runtimeState.loaded = true;
        if (m_persistence) {
            runtimeState.statePath = QDir(QDir(m_persistence->agentsDirPath()).filePath(trimmedAgentId))
                                         .filePath(QStringLiteral("heartbeat_state.json"));
            if (!runtimeState.statePath.trimmed().isEmpty()) {
                runtimeState.stateObj = m_persistence->readJsonObject(runtimeState.statePath);
                runtimeState.lastSnapshotDigest = runtimeState.stateObj.value(QStringLiteral("last_snapshot_digest")).toString().trimmed();
                runtimeState.lastSnapshotObj = runtimeState.stateObj.value(QStringLiteral("last_snapshot")).toObject();
                runtimeState.hasSnapshot = !runtimeState.lastSnapshotObj.isEmpty();
                runtimeState.lastNotifyAtUtc = parseIsoDateTimeToUtc(
                    runtimeState.stateObj.value(QStringLiteral("last_notify_at_utc")).toString());
                runtimeState.lastPersistAtUtc = parseIsoDateTimeToUtc(
                    runtimeState.stateObj.value(QStringLiteral("last_snapshot_at_utc")).toString());
            }
        }
    }

    const bool hadPreviousSnapshot = runtimeState.hasSnapshot;
    const QStringList changedKeys = hadPreviousSnapshot
        ? changedTopLevelKeys(runtimeState.lastSnapshotObj, snapshot)
        : QStringList();
    const bool hasChange = hadPreviousSnapshot && !changedKeys.isEmpty();
    const bool changedProvider = changedKeys.contains(QStringLiteral("provider_down"))
        || changedKeys.contains(QStringLiteral("provider_id"));
    const bool changedDelegate = changedKeys.contains(QStringLiteral("active_jobs_count"))
        || changedKeys.contains(QStringLiteral("active_jobs"));
    const bool changedScheduler = changedKeys.contains(QStringLiteral("scheduler_enabled_jobs"))
        || changedKeys.contains(QStringLiteral("scheduler_next_fire_at_utc"));
    // pulse/memory 变化可写入状态文件，但默认不触发心跳通知，避免“无任务也频繁打扰”。
    const bool hasActionableChange = changedProvider || changedDelegate || changedScheduler;

    runtimeState.hasSnapshot = true;
    runtimeState.lastSnapshotObj = snapshot;
    runtimeState.lastSnapshotDigest = snapshotDigest;
    runtimeState.stateObj.insert(QStringLiteral("last_snapshot"), snapshot);
    runtimeState.stateObj.insert(QStringLiteral("last_snapshot_digest"), snapshotDigest);
    runtimeState.stateObj.insert(QStringLiteral("last_snapshot_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
    runtimeState.stateObj.insert(QStringLiteral("last_reason"), reasonLabel);
    runtimeState.stateObj.insert(QStringLiteral("watch_signals"), signalArr);
    runtimeState.stateObj.insert(QStringLiteral("active_jobs_count"), activeJobs.size());
    runtimeState.stateObj.insert(QStringLiteral("provider_down"), providerDown);
    if (hasChange)
        runtimeState.stateObj.insert(QStringLiteral("last_change_at_utc"), nowUtc.toString(Qt::ISODateWithMs));

    const bool persistIntervalElapsed = (!runtimeState.lastPersistAtUtc.isValid())
        || (runtimeState.lastPersistAtUtc.msecsTo(nowUtc) >= qMax(1000, hbCfg.statePersistIntervalMs));
    bool shouldPersistState = hasChange
        || hbCfg.persistStateOnNoChange
        || persistIntervalElapsed
        || forceInteractive;
    auto persistStateIfNeeded = [&](bool forcePersist) {
        if (!m_persistence || runtimeState.statePath.trimmed().isEmpty())
            return;
        if (!forcePersist && !shouldPersistState)
            return;
        if (m_persistence->writeJsonObject(runtimeState.statePath, runtimeState.stateObj))
            runtimeState.lastPersistAtUtc = nowUtc;
    };

    QJsonObject triggeredExtra;
    triggeredExtra.insert(QStringLiteral("agent_id"), trimmedAgentId);
    triggeredExtra.insert(QStringLiteral("reason"), reasonLabel);
    triggeredExtra.insert(QStringLiteral("has_change"), hasChange);
    triggeredExtra.insert(QStringLiteral("has_actionable_change"), hasActionableChange);
    triggeredExtra.insert(QStringLiteral("first_snapshot"), !hadPreviousSnapshot);
    triggeredExtra.insert(QStringLiteral("active_delegate_jobs"), activeJobs.size());
    if (!providerId.isEmpty())
        triggeredExtra.insert(QStringLiteral("provider_id"), providerId);
    if (!changedKeys.isEmpty()) {
        QJsonArray changedArr;
        for (const QString& key : changedKeys)
            changedArr.append(key);
        triggeredExtra.insert(QStringLiteral("changed_keys"), changedArr);
    }
    emitPipelineEvent(QStringLiteral("heartbeat.triggered"), QString(), nullptr, QString(), QString(), triggeredExtra);

    if (providerDown) {
        persistStateIfNeeded(false);
        QJsonObject extra = triggeredExtra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("provider_down"));
        emitPipelineEvent(QStringLiteral("heartbeat.skipped"), QString(), nullptr, QString(), QStringLiteral("provider_down"), extra);
        return;
    }

    bool shouldNotify = true;
    QString skipReason;
    if (!forceInteractive && hasChange && !hasActionableChange) {
        shouldNotify = false;
        skipReason = QStringLiteral("non_actionable_change");
    } else if (!forceInteractive && !hasChange && hbCfg.silentWhenNoChange) {
        shouldNotify = false;
        skipReason = QStringLiteral("silent_no_change");
    } else if (!forceInteractive && hbCfg.notifyOnChangeOnly && !hasChange) {
        shouldNotify = false;
        skipReason = QStringLiteral("notify_on_change_only");
    } else if (!forceInteractive && hbCfg.notifyMinIntervalMs > 0 && !hasChange && runtimeState.lastNotifyAtUtc.isValid() && runtimeState.lastNotifyAtUtc.msecsTo(nowUtc) < hbCfg.notifyMinIntervalMs) {
        shouldNotify = false;
        skipReason = QStringLiteral("notify_rate_limited");
    }

    if (!shouldNotify) {
        persistStateIfNeeded(false);
        QJsonObject completeExtra = triggeredExtra;
        completeExtra.insert(QStringLiteral("silent"), true);
        completeExtra.insert(QStringLiteral("silent_reason"), skipReason);
        emitPipelineEvent(QStringLiteral("heartbeat.completed"), QString(), nullptr, QString(), QString(), completeExtra);
        return;
    }

    const QString sessionId = resolvePrimarySessionForAgent(trimmedAgentId, true, false, QStringLiteral("heartbeat"));
    if (sessionId.isEmpty()) {
        persistStateIfNeeded(false);
        QJsonObject extra = triggeredExtra;
        extra.insert(QStringLiteral("reason"), QStringLiteral("no_session"));
        emitPipelineEvent(QStringLiteral("heartbeat.skipped"), QString(), nullptr, QString(), QStringLiteral("no_session"), extra);
        return;
    }

    if (!forceInteractive) {
        const int pipelineDepth = m_turnManager.totalDepth(sessionId);
        if (pipelineDepth > 0) {
            persistStateIfNeeded(false);
            QJsonObject extra = triggeredExtra;
            extra.insert(QStringLiteral("session_id"), sessionId);
            extra.insert(QStringLiteral("queue_depth"), pipelineDepth);
            extra.insert(QStringLiteral("reason"), QStringLiteral("pipeline_busy"));
            emitPipelineEvent(QStringLiteral("heartbeat.skipped"), sessionId, nullptr, QString(), QStringLiteral("pipeline_busy"), extra);
            return;
        }
    }

    const QString userId = m_identityManager->userIdentity()->id();
    QString prompt = buildHeartbeatPrompt(trimmedAgentId, reasonLabel);
    if (hasChange) {
        QStringList delta;
        if (watchDelegate) {
            if (activeJobs.isEmpty()) {
                delta << QStringLiteral("活跃子代理任务: 0");
            } else {
                delta << QStringLiteral("活跃子代理任务: %1").arg(activeJobs.size());
                const DelegateTaskScheduler::JobInfo& first = activeJobs.first();
                if (!first.jobId.trimmed().isEmpty()) {
                    delta << QStringLiteral("首个任务：job_id=%1 status=%2")
                                 .arg(first.jobId.trimmed(), first.status.trimmed().isEmpty() ? QStringLiteral("running") : first.status.trimmed());
                }
            }
        }
        if (watchProvider && !providerId.trimmed().isEmpty())
            delta << QStringLiteral("Provider 状态: %1").arg(providerDown ? QStringLiteral("down") : QStringLiteral("up"));
        if (watchPulse) {
            AgentPulse* pulse = m_agentPulses.value(trimmedAgentId, nullptr);
            if (pulse)
                delta << QStringLiteral("主代理状态: %1").arg(pulseStateToString(pulse->currentState()));
        }
        if (delta.isEmpty())
            delta << QStringLiteral("状态发生变化。");
        prompt += QStringLiteral("\n\n[Heartbeat Delta]\n") + delta.join(QStringLiteral("\n"));
    } else if (forceInteractive) {
        prompt += QStringLiteral("\n\n[Heartbeat Delta]\n当前无关键变化。");
    }

    const QString hbTag = forceInteractive ? QStringLiteral("heartbeat-manual") : QStringLiteral("heartbeat-bg");
    const QString clientMessageId = QStringLiteral("%1-%2")
                                        .arg(hbTag, QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString turnId = enqueueUserMessageAs(userId, sessionId, prompt, clientMessageId);
    if (turnId.isEmpty()) {
        persistStateIfNeeded(false);
        QJsonObject extra = triggeredExtra;
        extra.insert(QStringLiteral("session_id"), sessionId);
        extra.insert(QStringLiteral("reason"), QStringLiteral("enqueue_failed"));
        emitPipelineEvent(QStringLiteral("heartbeat.failed"), sessionId, nullptr, QString(), QStringLiteral("enqueue_failed"), extra);
        return;
    }

    runtimeState.lastNotifyAtUtc = nowUtc;
    runtimeState.stateObj.insert(QStringLiteral("last_notify_at_utc"), nowUtc.toString(Qt::ISODateWithMs));
    shouldPersistState = true;
    persistStateIfNeeded(true);

    QJsonObject completeExtra = triggeredExtra;
    completeExtra.insert(QStringLiteral("session_id"), sessionId);
    completeExtra.insert(QStringLiteral("turn_id"), turnId);
    emitPipelineEvent(QStringLiteral("heartbeat.completed"), sessionId, nullptr, QString(), QString(), completeExtra);
}

void ChatService::onScheduledJobTriggered(const QString& jobId, const QString& jobName)
{
    if (!m_schedulerService || !m_identityManager)
        return;

    ScheduledJob job;
    if (!m_schedulerService->jobById(jobId, &job)) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), jobId);
        emitPipelineEvent(QStringLiteral("scheduler.failed"), QString(), nullptr, QString(), QStringLiteral("job_not_found"), extra);
        return;
    }

    const QString agentId = job.agentId.trimmed();
    Identity* agent = m_identityManager->findById(agentId);
    if (!agent || !agent->isAgent()) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), job.jobId);
        extra.insert(QStringLiteral("agent_id"), agentId);
        emitPipelineEvent(QStringLiteral("scheduler.failed"), QString(), nullptr, QString(), QStringLiteral("agent_not_found"), extra);
        return;
    }

    const bool isolated = job.sessionTarget.trimmed().compare(QStringLiteral("isolated"), Qt::CaseInsensitive) == 0;
    const QString sessionId = resolvePrimarySessionForAgent(agentId, true, isolated, QStringLiteral("scheduler"));
    if (sessionId.isEmpty()) {
        QJsonObject extra;
        extra.insert(QStringLiteral("job_id"), job.jobId);
        extra.insert(QStringLiteral("agent_id"), agentId);
        emitPipelineEvent(QStringLiteral("scheduler.failed"), QString(), nullptr, QString(), QStringLiteral("session_unavailable"), extra);
        return;
    }

    const QString userId = m_identityManager->userIdentity()->id();
    const QString prompt = QStringLiteral("【定时任务:%1】\n%2")
                               .arg(jobName.trimmed().isEmpty() ? QStringLiteral("scheduled-job") : jobName.trimmed(), job.prompt);
    const QString clientMessageId = QStringLiteral("scheduler-%1-%2")
                                        .arg(job.jobId, QUuid::createUuid().toString(QUuid::WithoutBraces));

    QJsonObject fireExtra;
    fireExtra.insert(QStringLiteral("job_id"), job.jobId);
    fireExtra.insert(QStringLiteral("job_name"), job.name);
    fireExtra.insert(QStringLiteral("agent_id"), agentId);
    fireExtra.insert(QStringLiteral("session_id"), sessionId);
    fireExtra.insert(QStringLiteral("session_target"), job.sessionTarget);
    fireExtra.insert(QStringLiteral("cron"), job.cronExpr);
    emitPipelineEvent(QStringLiteral("scheduler.fired"), sessionId, nullptr, QString(), QString(), fireExtra);

    const QString turnId = enqueueUserMessageAs(userId, sessionId, prompt, clientMessageId);
    if (turnId.isEmpty()) {
        emitPipelineEvent(QStringLiteral("scheduler.failed"), sessionId, nullptr, QString(), QStringLiteral("enqueue_failed"), fireExtra);
        return;
    }

    QJsonObject completeExtra = fireExtra;
    completeExtra.insert(QStringLiteral("turn_id"), turnId);
    emitPipelineEvent(QStringLiteral("scheduler.completed"), sessionId, nullptr, QString(), QString(), completeExtra);
}

void ChatService::ensureAgentPulse(const QString& agentId)
{
    const QString key = agentId.trimmed();
    if (key.isEmpty())
        return;
    if (m_agentPulses.contains(key))
        return;

    AgentPulse* pulse = new AgentPulse(key, this);
    pulse->start(1000);
    connect(pulse, &AgentPulse::stateChanged, this, [this](const QString& changedAgentId, AgentPulse::State state) {
        QJsonObject extra;
        extra.insert(QStringLiteral("agent_id"), changedAgentId);
        extra.insert(QStringLiteral("state"), pulseStateToString(state));
        emitPipelineEvent(QStringLiteral("pulse.state_changed"), QString(), nullptr, QString(), QString(), extra);
    });
    connect(pulse, &AgentPulse::hardTimeoutReached, this, [this](const QString& changedAgentId) {
        QJsonObject extra;
        extra.insert(QStringLiteral("agent_id"), changedAgentId);
        emitPipelineEvent(QStringLiteral("pulse.hard_timeout"), QString(), nullptr, QString(), QStringLiteral("agent_no_progress"), extra);
    });
    m_agentPulses.insert(key, pulse);
}

void ChatService::reportPulseProgress(const QString& agentId, const QString& summary)
{
    const QString key = agentId.trimmed();
    if (key.isEmpty())
        return;
    ensureAgentPulse(key);
    AgentPulse* pulse = m_agentPulses.value(key, nullptr);
    if (!pulse)
        return;
    pulse->reportProgress(summary);
}

void ChatService::ensureMemoryInitializedForAgent(Identity* agentIdentity)
{
    if (!m_memoryManager || !agentIdentity || !agentIdentity->isAgent())
        return;

    if (m_persistence) {
        const QString agentId = agentIdentity->id().trimmed();
        const QString workspacePath = QDir(
                                          m_persistence->agentsDirPath())
                                          .filePath(agentId + QStringLiteral("/workspace"));
        if (!QDir().mkpath(workspacePath)) {
            qWarning() << "[ChatService] agent workspace init failed:"
                       << agentId
                       << workspacePath;
        }

        const QString heartbeatMdPath = m_persistence->agentHeartbeatInstructionPath(agentId);
        if (!QFile::exists(heartbeatMdPath)) {
            writeUtf8TextFile(heartbeatMdPath, defaultHeartbeatTemplate());
        } else {
            repairHeartbeatFileIfNeeded(heartbeatMdPath);
        }

        const QString heartbeatCfgPath = m_persistence->agentHeartbeatConfigPath(agentId);
        if (!QFile::exists(heartbeatCfgPath)) {
            QJsonObject cfg;
            cfg.insert(QStringLiteral("enabled"), true);
            cfg.insert(QStringLiteral("intervalMs"), 30 * 60 * 1000);
            cfg.insert(QStringLiteral("coalesceMs"), 250);
            cfg.insert(QStringLiteral("duplicateWindowMs"), 24 * 60 * 60 * 1000);
            cfg.insert(QStringLiteral("silentWhenNoChange"), true);
            cfg.insert(QStringLiteral("notifyOnChangeOnly"), true);
            cfg.insert(QStringLiteral("notifyMinIntervalMs"), 30 * 60 * 1000);
            cfg.insert(QStringLiteral("persistStateOnNoChange"), false);
            cfg.insert(QStringLiteral("statePersistIntervalMs"), 60 * 1000);
            QJsonArray snapshotSignals;
            snapshotSignals.append(QStringLiteral("provider_status"));
            snapshotSignals.append(QStringLiteral("delegate_jobs"));
            snapshotSignals.append(QStringLiteral("pulse_state"));
            cfg.insert(QStringLiteral("snapshotSignals"), snapshotSignals);
            cfg.insert(QStringLiteral("heartbeatPath"), heartbeatMdPath);
            QJsonObject activeHours;
            activeHours.insert(QStringLiteral("start"), QStringLiteral("08:00"));
            activeHours.insert(QStringLiteral("end"), QStringLiteral("23:00"));
            activeHours.insert(QStringLiteral("timezone"), QStringLiteral("Asia/Shanghai"));
            cfg.insert(QStringLiteral("activeHours"), activeHours);
            m_persistence->writeJsonObject(heartbeatCfgPath, cfg);
        }
    }

    QString memoryError;
    if (!m_memoryManager->initializeForAgent(agentIdentity, &memoryError) && !memoryError.isEmpty()) {
        qWarning() << "[ChatService] agent memory init failed:"
                   << agentIdentity->id()
                   << memoryError;
    }
}

void ChatService::saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId)
{
    m_configService->saveTabState(openAgentIds, activeIdentityId);
}

ChatService::TabState ChatService::loadTabState() const
{
    const ConfigService::TabState cs = m_configService->loadTabState();
    TabState state;
    state.openAgentIds = cs.openAgentIds;
    state.activeIdentityId = cs.activeIdentityId;
    return state;
}
