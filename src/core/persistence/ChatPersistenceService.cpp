#include "ChatPersistenceService.h"
#include "DatabaseManager.h"
#include "core/model/Message.h"

#include "core/model/IdentityProfile.h"
#include "core/model/Message.h"
#include "llm/LLMTypes.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStorageInfo>
#include <QUuid>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

bool syncFileToStorage(QFile& file)
{
#ifdef Q_OS_WIN
    const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    return FlushFileBuffers(handle) != 0;
#else
    return fsync(file.handle()) == 0;
#endif
}

QString messageTypeToString(MessageContent::Type type)
{
    switch (type) {
    case MessageContent::Type::Text:
        return QStringLiteral("text");
    case MessageContent::Type::ToolCall:
        return QStringLiteral("tool_call");
    case MessageContent::Type::ToolResult:
        return QStringLiteral("tool_result");
    case MessageContent::Type::System:
        return QStringLiteral("system");
    case MessageContent::Type::File:
        return QStringLiteral("file");
    }
    return QStringLiteral("text");
}

MessageContent::Type messageTypeFromString(const QString& type)
{
    if (type == QLatin1String("tool_call"))
        return MessageContent::Type::ToolCall;
    if (type == QLatin1String("tool_result"))
        return MessageContent::Type::ToolResult;
    if (type == QLatin1String("system"))
        return MessageContent::Type::System;
    if (type == QLatin1String("file"))
        return MessageContent::Type::File;
    return MessageContent::Type::Text;
}

QString messageStatusToString(Message::Status status)
{
    switch (status) {
    case Message::Status::Pending:
        return QStringLiteral("pending");
    case Message::Status::Streaming:
        return QStringLiteral("streaming");
    case Message::Status::Completed:
        return QStringLiteral("completed");
    case Message::Status::Cancelled:
        return QStringLiteral("cancelled");
    case Message::Status::Interrupted:
        return QStringLiteral("interrupted");
    case Message::Status::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

Message::Status messageStatusFromString(const QString& status)
{
    if (status == QLatin1String("pending"))
        return Message::Status::Pending;
    if (status == QLatin1String("streaming"))
        return Message::Status::Streaming;
    if (status == QLatin1String("completed"))
        return Message::Status::Completed;
    if (status == QLatin1String("cancelled"))
        return Message::Status::Cancelled;
    if (status == QLatin1String("interrupted"))
        return Message::Status::Interrupted;
    if (status == QLatin1String("error"))
        return Message::Status::Error;
    return Message::Status::Completed;
}

QString stringField(const QJsonObject& obj, const QString& key)
{
    return obj.value(key).toString().trimmed();
}

QString stringFieldAny(const QJsonObject& obj, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString value = stringField(obj, key);
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

qint64 numericToMs(const QJsonValue& value, bool* ok = nullptr)
{
    if (ok)
        *ok = false;
    if (!value.isDouble())
        return 0;

    const qint64 raw = static_cast<qint64>(value.toDouble());
    const qint64 ms = (qAbs(raw) < 100000000000LL) ? (raw * 1000LL) : raw;
    if (ok)
        *ok = true;
    return ms;
}

QDateTime parseEventTimestampUtc(const QJsonObject& event)
{
    bool ok = false;
    const qint64 directMs = numericToMs(event.value(QStringLiteral("timestamp_ms")), &ok);
    if (ok)
        return QDateTime::fromMSecsSinceEpoch(directMs, Qt::UTC);

    const QJsonValue tsValue = event.value(QStringLiteral("timestamp"));
    const qint64 tsMs = numericToMs(tsValue, &ok);
    if (ok)
        return QDateTime::fromMSecsSinceEpoch(tsMs, Qt::UTC);

    const QString timestampText = stringFieldAny(event, QStringList()
                                                          << QStringLiteral("timestamp")
                                                          << QStringLiteral("time")
                                                          << QStringLiteral("createdAt")
                                                          << QStringLiteral("created_at"));
    if (!timestampText.isEmpty()) {
        QDateTime dt = QDateTime::fromString(timestampText, Qt::ISODateWithMs);
        if (!dt.isValid())
            dt = QDateTime::fromString(timestampText, Qt::ISODate);
        if (dt.isValid()) {
            if (dt.timeSpec() == Qt::LocalTime)
                dt = dt.toUTC();
            return dt;
        }
    }

    return QDateTime::currentDateTimeUtc();
}

qint64 parseDurationMs(const QJsonObject& event)
{
    const QJsonValue duration = event.value(QStringLiteral("duration_ms"));
    if (duration.isDouble())
        return static_cast<qint64>(duration.toDouble());

    const QJsonValue durationCamel = event.value(QStringLiteral("durationMs"));
    if (durationCamel.isDouble())
        return static_cast<qint64>(durationCamel.toDouble());

    return -1;
}

int parseSuccessValue(const QJsonObject& event)
{
    if (event.value(QStringLiteral("success")).isBool())
        return event.value(QStringLiteral("success")).toBool() ? 1 : 0;

    const QJsonObject toolEventObj = event.value(QStringLiteral("toolEvent")).toObject();
    if (toolEventObj.value(QStringLiteral("success")).isBool())
        return toolEventObj.value(QStringLiteral("success")).toBool() ? 1 : 0;

    return -1;
}

QString extractEventType(const QJsonObject& event)
{
    const QString directType = stringField(event, QStringLiteral("type"));
    if (!directType.isEmpty())
        return directType;
    return stringField(event.value(QStringLiteral("event")).toObject(), QStringLiteral("type"));
}

QString extractToolName(const QJsonObject& event)
{
    QString value = stringFieldAny(event, QStringList()
                                            << QStringLiteral("tool_name")
                                            << QStringLiteral("toolName"));
    if (!value.isEmpty())
        return value;

    const QJsonObject toolEventObj = event.value(QStringLiteral("toolEvent")).toObject();
    value = stringFieldAny(toolEventObj, QStringList()
                                             << QStringLiteral("toolName")
                                             << QStringLiteral("tool_name"));
    if (!value.isEmpty())
        return value;

    return stringField(toolEventObj.value(QStringLiteral("data")).toObject(), QStringLiteral("tool_name"));
}

QString extractToolCallId(const QJsonObject& event)
{
    QString value = stringFieldAny(event, QStringList()
                                            << QStringLiteral("tool_call_id")
                                            << QStringLiteral("toolId"));
    if (!value.isEmpty())
        return value;

    const QJsonObject toolEventObj = event.value(QStringLiteral("toolEvent")).toObject();
    value = stringField(toolEventObj, QStringLiteral("toolId"));
    if (!value.isEmpty())
        return value;

    return stringField(toolEventObj.value(QStringLiteral("data")).toObject(), QStringLiteral("tool_call_id"));
}

QString extractRequestId(const QJsonObject& event)
{
    QString value = stringFieldAny(event, QStringList()
                                            << QStringLiteral("request_id")
                                            << QStringLiteral("child_request_id"));
    if (!value.isEmpty())
        return value;

    const QJsonObject toolDataObj = event.value(QStringLiteral("toolEvent")).toObject().value(QStringLiteral("data")).toObject();
    return stringField(toolDataObj, QStringLiteral("child_request_id"));
}

QString extractActorId(const QJsonObject& event)
{
    QString value = stringFieldAny(event, QStringList()
                                            << QStringLiteral("actor_id")
                                            << QStringLiteral("actorIdentityId"));
    if (!value.isEmpty())
        return value;

    const QJsonObject toolDataObj = event.value(QStringLiteral("toolEvent")).toObject().value(QStringLiteral("data")).toObject();
    return stringField(toolDataObj, QStringLiteral("_agent_id"));
}

QString extractLevel(const QJsonObject& event)
{
    QString value = stringFieldAny(event, QStringList()
                                            << QStringLiteral("level")
                                            << QStringLiteral("severity")
                                            << QStringLiteral("log_level"));
    if (!value.isEmpty())
        return value.toLower();

    const QString eventType = extractEventType(event).toLower();
    if (eventType.contains(QStringLiteral("error")) || eventType.contains(QStringLiteral("failed")))
        return QStringLiteral("error");
    if (eventType.contains(QStringLiteral("warning")))
        return QStringLiteral("warning");
    return QStringLiteral("info");
}

QString buildEventSummary(const QJsonObject& event)
{
    const QString explicitSummary = stringField(event, QStringLiteral("summary"));
    if (!explicitSummary.isEmpty())
        return explicitSummary;

    QString summary = extractEventType(event);
    const QString toolName = extractToolName(event);
    if (!toolName.isEmpty()) {
        if (!summary.isEmpty())
            summary += QStringLiteral(" ");
        summary += QStringLiteral("tool=%1").arg(toolName);
    }

    const QString error = stringField(event, QStringLiteral("error"));
    if (!error.isEmpty()) {
        if (!summary.isEmpty())
            summary += QStringLiteral(" ");
        summary += QStringLiteral("error=%1").arg(error);
    }

    return summary.simplified();
}

} // namespace

QString ChatPersistenceService::dataRootPath() const
{
    return defaultDataRootPath();
}

QString ChatPersistenceService::defaultDataRootPath()
{
    return QDir::home().filePath(QStringLiteral(".tmagent"));
}

QString ChatPersistenceService::defaultConfigDirPath()
{
    return QDir(defaultDataRootPath()).filePath(QStringLiteral("config"));
}

QString ChatPersistenceService::defaultModelConfigPath()
{
    return QDir(defaultConfigDirPath()).filePath(QStringLiteral("models.yaml"));
}

QString ChatPersistenceService::configDirPath() const
{
    return defaultConfigDirPath();
}

QString ChatPersistenceService::appStatePath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("app_state.json"));
}

QString ChatPersistenceService::manifestPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("manifest.json"));
}

QString ChatPersistenceService::identitiesDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("identities"));
}

QString ChatPersistenceService::agentsDirPath() const
{
    return QDir(identitiesDirPath()).filePath(QStringLiteral("agents"));
}

QString ChatPersistenceService::userIdentityPath() const
{
    return QDir(identitiesDirPath()).filePath(QStringLiteral("user.json"));
}

QString ChatPersistenceService::agentProfilePath(const QString& agentId) const
{
    return QDir(QDir(agentsDirPath()).filePath(agentId)).filePath(QStringLiteral("profile.json"));
}

QString ChatPersistenceService::sessionsDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("sessions"));
}

QString ChatPersistenceService::sessionsIndexPath() const
{
    return QDir(sessionsDirPath()).filePath(QStringLiteral("index.json"));
}

QString ChatPersistenceService::logsDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("logs"));
}

QString ChatPersistenceService::eventsCurrentLogPath() const
{
    return QDir(logsDirPath()).filePath(QStringLiteral("events-current.jsonl"));
}

QString ChatPersistenceService::sessionDataDirPath(const QString& sessionId) const
{
    return QDir(QDir(sessionsDirPath()).filePath(QStringLiteral("data"))).filePath(sessionId);
}

QString ChatPersistenceService::sessionMetaPath(const QString& sessionId) const
{
    return QDir(sessionDataDirPath(sessionId)).filePath(QStringLiteral("meta.json"));
}

QString ChatPersistenceService::sessionMessagesPath(const QString& sessionId) const
{
    return QDir(sessionDataDirPath(sessionId)).filePath(QStringLiteral("messages.jsonl"));
}

QString ChatPersistenceService::sessionPendingTurnsPath(const QString& sessionId) const
{
    return QDir(sessionDataDirPath(sessionId)).filePath(QStringLiteral("pending_turns.json"));
}

QString ChatPersistenceService::sessionTaskStatePath(const QString& sessionId) const
{
    return QDir(sessionDataDirPath(sessionId)).filePath(QStringLiteral("task_state.json"));
}

QString ChatPersistenceService::mcpConfigPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("mcp_servers.json"));
}

QString ChatPersistenceService::modelConfigPath() const
{
    if (!m_modelConfigPathOverride.trimmed().isEmpty())
        return m_modelConfigPathOverride;
    return defaultModelConfigPath();
}

void ChatPersistenceService::setModelConfigPathOverride(const QString& filePath)
{
    m_modelConfigPathOverride = filePath.trimmed();
}

QString ChatPersistenceService::memoryPolicyPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("memory_policy.json"));
}

QString ChatPersistenceService::scheduledJobsPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("scheduled_jobs.json"));
}

QString ChatPersistenceService::agentHeartbeatConfigPath(const QString& agentId) const
{
    return QDir(QDir(agentsDirPath()).filePath(agentId.trimmed()))
        .filePath(QStringLiteral("heartbeat_config.json"));
}

QString ChatPersistenceService::agentHeartbeatInstructionPath(const QString& agentId) const
{
    return QDir(QDir(agentsDirPath()).filePath(agentId.trimmed()))
        .filePath(QStringLiteral("HEARTBEAT.md"));
}

bool ChatPersistenceService::ensureParentDir(const QString& filePath) const
{
    return QDir().mkpath(QFileInfo(filePath).absolutePath());
}

bool ChatPersistenceService::writeJsonDocument(const QString& filePath, const QJsonDocument& doc) const
{
    if (!ensureParentDir(filePath))
        return false;

    QSaveFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;

    if (file.write(doc.toJson(QJsonDocument::Indented)) < 0)
        return false;
    return file.commit();
}

bool ChatPersistenceService::writeJsonObject(const QString& filePath, const QJsonObject& obj) const
{
    return writeJsonDocument(filePath, QJsonDocument(obj));
}

bool ChatPersistenceService::writeJsonLines(const QString& filePath, const QJsonArray& lines) const
{
    if (!ensureParentDir(filePath))
        return false;

    QSaveFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text))
        return false;

    for (const QJsonValue& value : lines) {
        if (!value.isObject())
            continue;
        const QByteArray line = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
        if (file.write(line) < 0)
            return false;
        if (file.write("\n") < 0)
            return false;
    }
    return file.commit();
}

bool ChatPersistenceService::appendJsonLine(const QString& filePath, const QJsonObject& line) const
{
    if (!ensureParentDir(filePath))
        return false;

    QFile file(filePath);
    if (!file.open(QFile::WriteOnly | QFile::Append | QFile::Text))
        return false;

    const QByteArray payload = QJsonDocument(line).toJson(QJsonDocument::Compact);
    if (file.write(payload) < 0)
        return false;
    if (file.write("\n") < 0)
        return false;
    file.flush();
    syncFileToStorage(file);
    return true;
}

QJsonObject ChatPersistenceService::readJsonObject(const QString& filePath, bool* ok) const
{
    if (ok)
        *ok = false;
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QJsonObject();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    if (ok)
        *ok = true;
    return doc.object();
}

QJsonArray ChatPersistenceService::readJsonLines(const QString& filePath, bool* ok) const
{
    if (ok)
        *ok = false;

    QJsonArray result;
    QFile file(filePath);
    if (!file.exists()) {
        if (ok)
            *ok = true;
        return result;
    }
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return result;

    const QByteArray raw = file.readAll();
    file.close();
    const QList<QByteArray> lines = raw.split('\n');

    bool parsedAny = false;
    bool hasNonEmptyLine = false;
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        hasNonEmptyLine = true;

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "[ChatPersistenceService] 跳过无效 JSONL 行" << filePath << "error="
                       << err.errorString();
            continue;
        }
        result.append(doc.object());
        parsedAny = true;
    }

    if (ok)
        *ok = !hasNonEmptyLine || parsedAny;
    return result;
}

QJsonArray ChatPersistenceService::stringListToJson(const QStringList& values) const
{
    QJsonArray arr;
    for (const QString& value : values) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty())
            arr.append(trimmed);
    }
    return arr;
}

QStringList ChatPersistenceService::stringListFromJson(const QJsonValue& value) const
{
    QStringList result;
    const QJsonArray arr = value.toArray();
    for (const QJsonValue& item : arr) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty())
            result.append(text);
    }
    result.removeDuplicates();
    return result;
}

QJsonObject ChatPersistenceService::messageToJson(const Message& msg) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), msg.id);
    obj.insert(QStringLiteral("sessionId"), msg.sessionId);
    if (!msg.traceId.isEmpty())
        obj.insert(QStringLiteral("traceId"), msg.traceId);
    if (!msg.turnId.isEmpty())
        obj.insert(QStringLiteral("turnId"), msg.turnId);
    if (msg.seq > 0)
        obj.insert(QStringLiteral("seq"), static_cast<qint64>(msg.seq));
    obj.insert(QStringLiteral("senderId"), msg.senderId);

    QJsonArray mentions;
    for (const QString& mention : msg.mentions)
        mentions.append(mention);
    obj.insert(QStringLiteral("mentions"), mentions);

    QJsonObject content;
    content.insert(QStringLiteral("type"), messageTypeToString(msg.content.type));
    content.insert(QStringLiteral("text"), msg.content.text);
    content.insert(QStringLiteral("payload"), msg.content.payload);
    obj.insert(QStringLiteral("content"), content);

    obj.insert(QStringLiteral("timestamp"), msg.timestamp.toString(Qt::ISODateWithMs));
    obj.insert(QStringLiteral("status"), messageStatusToString(msg.status));
    return obj;
}

Message ChatPersistenceService::messageFromJson(const QJsonObject& obj, const QString& fallbackSessionId) const
{
    Message msg;
    msg.id = obj.value(QStringLiteral("id")).toString().trimmed();
    if (msg.id.isEmpty())
        msg.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    msg.sessionId = obj.value(QStringLiteral("sessionId")).toString().trimmed();
    if (msg.sessionId.isEmpty())
        msg.sessionId = fallbackSessionId;
    msg.traceId = obj.value(QStringLiteral("traceId")).toString().trimmed();
    msg.turnId = obj.value(QStringLiteral("turnId")).toString().trimmed();
    msg.seq = static_cast<qint64>(obj.value(QStringLiteral("seq")).toDouble(0));

    msg.senderId = obj.value(QStringLiteral("senderId")).toString().trimmed();
    const QJsonArray mentions = obj.value(QStringLiteral("mentions")).toArray();
    for (const QJsonValue& v : mentions) {
        const QString mentionId = v.toString().trimmed();
        if (!mentionId.isEmpty())
            msg.mentions.append(mentionId);
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    msg.content.type = messageTypeFromString(contentObj.value(QStringLiteral("type")).toString().trimmed());
    msg.content.text = contentObj.value(QStringLiteral("text")).toString();
    msg.content.payload = contentObj.value(QStringLiteral("payload")).toObject();

    msg.timestamp = QDateTime::fromString(
        obj.value(QStringLiteral("timestamp")).toString().trimmed(),
        Qt::ISODateWithMs);
    if (!msg.timestamp.isValid())
        msg.timestamp = QDateTime::currentDateTime();

    msg.status = messageStatusFromString(obj.value(QStringLiteral("status")).toString().trimmed());

    return msg;
}

QJsonObject ChatPersistenceService::identityProfileToJson(const IdentityProfile* profile) const
{
    QJsonObject obj;
    if (!profile)
        return obj;

    obj.insert(QStringLiteral("description"), profile->description());
    obj.insert(QStringLiteral("systemPrompt"), profile->systemPrompt());
    obj.insert(QStringLiteral("allowedTools"), stringListToJson(profile->allowedTools()));
    obj.insert(QStringLiteral("delegateEnabled"), profile->delegateEnabled());
    obj.insert(QStringLiteral("recursionDepth"), profile->recursionDepth());

    const LLMConfig cfg = profile->llmConfig();
    obj.insert(QStringLiteral("providerInstanceId"), cfg.providerInstanceId.trimmed());
    obj.insert(QStringLiteral("selectedModelId"), cfg.selectedModelId.trimmed());
    obj.insert(QStringLiteral("configId"), cfg.configId.trimmed()); // 兼容旧版读取
    return obj;
}

IdentityProfile* ChatPersistenceService::identityProfileFromJson(const QJsonObject& obj, const LLMConfig& fallbackConfig) const
{
    auto* profile = new IdentityProfile();

    LLMConfig cfg = fallbackConfig;
    cfg.providerInstanceId = obj.value(QStringLiteral("providerInstanceId")).toString().trimmed();
    cfg.selectedModelId = obj.value(QStringLiteral("selectedModelId")).toString().trimmed();
    const QString configId = obj.value(QStringLiteral("configId")).toString().trimmed();
    // 兼容旧数据：如果新字段为空但 configId 不为空
    if (cfg.providerInstanceId.isEmpty() && !configId.isEmpty())
        cfg.configId = configId;
    else if (!cfg.providerInstanceId.isEmpty())
        cfg.configId = cfg.providerInstanceId; // 保持 configId 同步

    QString systemPrompt = obj.value(QStringLiteral("systemPrompt")).toString().trimmed();
    if (systemPrompt.isEmpty())
        systemPrompt = fallbackConfig.systemPrompt;
    cfg.systemPrompt = systemPrompt;

    profile->setLlmConfig(cfg);
    if (!systemPrompt.isEmpty())
        profile->setSystemPrompt(systemPrompt);

    profile->setDescription(obj.value(QStringLiteral("description")).toString().trimmed());
    profile->setAllowedTools(stringListFromJson(obj.value(QStringLiteral("allowedTools"))));
    profile->setDelegateEnabled(obj.value(QStringLiteral("delegateEnabled")).toBool(true));
    profile->setRecursionDepth(obj.value(QStringLiteral("recursionDepth")).toInt(3));
    return profile;
}

QStringList ChatPersistenceService::loadMcpConfigSpecs() const
{
    QStringList specs;
    QFile f(mcpConfigPath());
    if (!f.open(QFile::ReadOnly | QFile::Text))
        return specs;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return specs;

    QJsonArray arr = doc.object().value(QStringLiteral("servers")).toArray();
    for (const QJsonValue& v : arr) {
        const QString spec = v.toString().trimmed();
        if (!spec.isEmpty())
            specs.append(spec);
    }
    return specs;
}

bool ChatPersistenceService::saveMcpConfigSpecs(const QStringList& specs) const
{
    QJsonArray arr;
    for (const QString& spec : specs) {
        if (!spec.trimmed().isEmpty())
            arr.append(spec.trimmed());
    }
    QJsonObject root;
    root.insert(QStringLiteral("servers"), arr);
    return writeJsonObject(mcpConfigPath(), root);
}

bool ChatPersistenceService::appendSessionMessage(const QString& sessionId, const QJsonObject& messageObj) const
{
    if (sessionId.trimmed().isEmpty())
        return false;

    // 双写过渡期：同时写入 DB 和 JSONL
    // JSONL 写入（保持兼容）
    appendJsonLine(sessionMessagesPath(sessionId), messageObj);

    // DB 写入
    if (DatabaseManager::instance()->isReady()) {
        Message msg = messageFromJson(messageObj, sessionId);
        insertMessageToDb(msg);
    }

    return true;
}

void ChatPersistenceService::rotateEventLogIfNeeded() const
{
    static const qint64 kMaxEventLogBytes = 50LL * 1024 * 1024;
    static const int kMaxRetentionDays = 14;
    static const qint64 kCheckIntervalMs = 30000;
    static const qint64 kMaxTotalLogBytes = 500LL * 1024 * 1024;
    static const qint64 kTotalLogTargetBytes = 400LL * 1024 * 1024;

    static QMutex mutex;
    QMutexLocker locker(&mutex);

    static qint64 s_lastCheckMs = 0;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (s_lastCheckMs > 0 && (nowMs - s_lastCheckMs) < kCheckIntervalMs)
        return;
    s_lastCheckMs = nowMs;

    const QString currentPath = eventsCurrentLogPath();
    QFileInfo info(currentPath);
    if (info.exists() && info.size() >= kMaxEventLogBytes) {
        const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
        const QString archivedPath = QDir(logsDirPath()).filePath(QStringLiteral("events-%1.jsonl").arg(stamp));
        QFile::rename(currentPath, archivedPath);
    }

    QDir dir(logsDirPath());
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("events-*.jsonl"), QDir::Files, QDir::Time);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    for (const QFileInfo& file : files) {
        const qint64 ageDays = file.lastModified().toUTC().daysTo(nowUtc);
        if (ageDays > kMaxRetentionDays)
            QFile::remove(file.absoluteFilePath());
    }

    // 总目录大小检查：超过 500MB 时按修改时间从旧到新删除归档文件直到低于 400MB
    const QFileInfoList allLogFiles = dir.entryInfoList(
        QStringList() << QStringLiteral("events-*.jsonl"), QDir::Files, QDir::Time | QDir::Reversed);
    qint64 totalSize = 0;
    for (const QFileInfo& f : allLogFiles)
        totalSize += f.size();

    if (totalSize > kMaxTotalLogBytes) {
        for (const QFileInfo& f : allLogFiles) {
            if (f.fileName() == QStringLiteral("events-current.jsonl"))
                continue;
            QFile::remove(f.absoluteFilePath());
            totalSize -= f.size();
            if (totalSize <= kTotalLogTargetBytes)
                break;
        }
    }
}

bool ChatPersistenceService::appendEventLog(const QJsonObject& event) const
{
    rotateEventLogIfNeeded();

    if (DatabaseManager::instance()->isReady() && !insertEventToDb(event)) {
        qWarning() << "[ChatPersistenceService] insertEventToDb 失败，继续写 JSONL";
    }

    static const qint64 kMinDiskSpaceBytes = 100LL * 1024 * 1024;
    const QString logPath = eventsCurrentLogPath();
    const QStorageInfo storage(QFileInfo(logPath).absolutePath());
    if (storage.isValid() && storage.bytesAvailable() < kMinDiskSpaceBytes) {
        qWarning() << "[ChatPersistenceService] 磁盘剩余空间不足 100MB，跳过事件日志写入"
                   << "available=" << storage.bytesAvailable();
        return false;
    }

    return appendJsonLine(logPath, event);
}

void ChatPersistenceService::saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId) const
{
    bool appStateOk = false;
    QJsonObject appState = readJsonObject(appStatePath(), &appStateOk);
    if (!appStateOk)
        appState = QJsonObject();

    QJsonObject tabState;
    QJsonArray agentIds;
    QStringList normalizedAgentIds;
    for (const QString& id : openAgentIds) {
        const QString trimmed = id.trimmed();
        if (trimmed.isEmpty() || normalizedAgentIds.contains(trimmed))
            continue;
        normalizedAgentIds.append(trimmed);
    }
    for (const QString& id : normalizedAgentIds)
        agentIds.append(id);
    tabState.insert(QStringLiteral("openAgentIds"), agentIds);
    tabState.insert(QStringLiteral("activeIdentityId"), activeIdentityId);
    appState.insert(QStringLiteral("schemaVersion"), 3);
    appState.insert(QStringLiteral("tabState"), tabState);
    writeJsonObject(appStatePath(), appState);
}

ChatPersistenceService::TabState ChatPersistenceService::loadTabState() const
{
    TabState state;
    bool appStateOk = false;
    const QJsonObject appState = readJsonObject(appStatePath(), &appStateOk);
    if (!appStateOk)
        return state;

    const QJsonObject tabObj = appState.value(QStringLiteral("tabState")).toObject();
    const QJsonArray arr = tabObj.value(QStringLiteral("openAgentIds")).toArray();
    for (const QJsonValue& v : arr) {
        const QString id = v.toString().trimmed();
        if (!id.isEmpty())
            state.openAgentIds.append(id);
    }
    state.openAgentIds.removeDuplicates();
    state.activeIdentityId = tabObj.value(QStringLiteral("activeIdentityId")).toString().trimmed();
    return state;
}

// ─── SQLite 数据库操作实现 ───

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

bool ChatPersistenceService::insertMessageToDb(const Message& msg, const QString& source) const
{
    if (!DatabaseManager::instance()->isReady() || msg.id.isEmpty())
        return false;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO messages "
        "(id, session_id, trace_id, turn_id, seq, sender_id, "
        " content_type, content_text, content_payload, timestamp, status, source) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(msg.id);
    q.addBindValue(msg.sessionId);
    q.addBindValue(msg.traceId);
    q.addBindValue(msg.turnId);
    q.addBindValue(static_cast<qint64>(msg.seq));
    q.addBindValue(msg.senderId);

    // content
    q.addBindValue(messageTypeToString(msg.content.type));
    q.addBindValue(msg.content.text);
    q.addBindValue(QString::fromUtf8(QJsonDocument(msg.content.payload).toJson(QJsonDocument::Compact)));
    q.addBindValue(msg.timestamp.toString(Qt::ISODateWithMs));
    q.addBindValue(messageStatusToString(msg.status));
    q.addBindValue(source);

    if (!q.exec()) {
        qWarning() << "[ChatPersistenceService] insertMessageToDb 失败:" << q.lastError().text()
                   << "msgId=" << msg.id;
        return false;
    }
    return true;
}

bool ChatPersistenceService::insertEventToDb(const QJsonObject& event) const
{
    if (!DatabaseManager::instance()->isReady())
        return false;

    const QDateTime timestampUtc = parseEventTimestampUtc(event);
    const qint64 timestampMs = timestampUtc.toMSecsSinceEpoch();

    const QString sessionId = stringFieldAny(event, QStringList()
                                                      << QStringLiteral("session_id")
                                                      << QStringLiteral("sessionId"));
    const QString traceId = stringField(event, QStringLiteral("trace_id"));
    const QString turnId = stringFieldAny(event, QStringList()
                                                   << QStringLiteral("turn_id")
                                                   << QStringLiteral("turnId"));
    const QString runId = stringFieldAny(event, QStringList()
                                                  << QStringLiteral("run_id")
                                                  << QStringLiteral("runId"));
    const QString requestId = extractRequestId(event);
    const QString toolCallId = extractToolCallId(event);
    const QString actorId = extractActorId(event);
    const QString toolName = extractToolName(event);
    const QString eventType = extractEventType(event);
    const QString level = extractLevel(event);
    const qint64 durationMs = parseDurationMs(event);
    const int successValue = parseSuccessValue(event);
    const QString summary = buildEventSummary(event);
    const QString rawJson = QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact));

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO events "
        "(timestamp, timestamp_ms, session_id, trace_id, turn_id, run_id, request_id, "
        " tool_call_id, actor_id, tool_name, event_type, level, duration_ms, success, summary, raw_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    q.addBindValue(timestampUtc.toString(Qt::ISODateWithMs));
    q.addBindValue(timestampMs);
    q.addBindValue(sessionId);
    q.addBindValue(traceId);
    q.addBindValue(turnId);
    q.addBindValue(runId);
    q.addBindValue(requestId);
    q.addBindValue(toolCallId);
    q.addBindValue(actorId);
    q.addBindValue(toolName);
    q.addBindValue(eventType);
    q.addBindValue(level);
    if (durationMs >= 0)
        q.addBindValue(durationMs);
    else
        q.addBindValue(QVariant(QVariant::LongLong));
    if (successValue >= 0)
        q.addBindValue(successValue);
    else
        q.addBindValue(QVariant(QVariant::Int));
    q.addBindValue(summary);
    q.addBindValue(rawJson);

    if (!q.exec()) {
        qWarning() << "[ChatPersistenceService] insertEventToDb 失败:" << q.lastError().text()
                   << "eventType=" << eventType;
        return false;
    }

    return true;
}

static Message messageFromDbRow(QSqlQuery& q, const QString& fallbackSessionId)
{
    Message msg;
    msg.id = q.value(0).toString();
    msg.sessionId = q.value(1).toString();
    if (msg.sessionId.isEmpty())
        msg.sessionId = fallbackSessionId;
    msg.traceId = q.value(2).toString();
    msg.turnId = q.value(3).toString();
    msg.seq = q.value(4).toLongLong();
    msg.senderId = q.value(5).toString();
    msg.content.type = messageTypeFromString(q.value(6).toString());
    msg.content.text = q.value(7).toString();

    QJsonParseError err;
    QJsonDocument payloadDoc = QJsonDocument::fromJson(q.value(8).toString().toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && payloadDoc.isObject())
        msg.content.payload = payloadDoc.object();

    msg.timestamp = QDateTime::fromString(q.value(9).toString(), Qt::ISODateWithMs);
    if (!msg.timestamp.isValid())
        msg.timestamp = QDateTime::currentDateTime();
    msg.status = messageStatusFromString(q.value(10).toString());
    return msg;
}

QList<Message> ChatPersistenceService::loadMessagesFromDb(const QString& sessionId) const
{
    QList<Message> result;
    if (!DatabaseManager::instance()->isReady() || sessionId.isEmpty())
        return result;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, session_id, trace_id, turn_id, seq, sender_id, "
        "content_type, content_text, content_payload, timestamp, status "
        "FROM messages WHERE session_id = ? ORDER BY rowid"));
    q.addBindValue(sessionId);
    if (!q.exec()) {
        qWarning() << "[ChatPersistenceService] loadMessagesFromDb 失败:" << q.lastError().text();
        return result;
    }
    while (q.next())
        result.append(messageFromDbRow(q, sessionId));
    return result;
}

QList<Message> ChatPersistenceService::loadNewMessagesFromDb(const QString& sessionId, qint64 lastRowId) const
{
    QList<Message> result;
    if (!DatabaseManager::instance()->isReady() || sessionId.isEmpty())
        return result;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, session_id, trace_id, turn_id, seq, sender_id, "
        "content_type, content_text, content_payload, timestamp, status "
        "FROM messages WHERE session_id = ? AND rowid > ? ORDER BY rowid"));
    q.addBindValue(sessionId);
    q.addBindValue(lastRowId);
    if (!q.exec()) {
        qWarning() << "[ChatPersistenceService] loadNewMessagesFromDb 失败:" << q.lastError().text();
        return result;
    }
    while (q.next())
        result.append(messageFromDbRow(q, sessionId));
    return result;
}

qint64 ChatPersistenceService::maxMessageRowId(const QString& sessionId) const
{
    if (!DatabaseManager::instance()->isReady() || sessionId.isEmpty())
        return 0;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT MAX(rowid) FROM messages WHERE session_id = ?"));
    q.addBindValue(sessionId);
    if (q.exec() && q.next())
        return q.value(0).toLongLong();
    return 0;
}

bool ChatPersistenceService::saveIdentityToDb(const QString& id, const QString& type, const QString& name, const QString& avatar, const QString& profileJson) const
{
    if (!DatabaseManager::instance()->isReady() || id.isEmpty())
        return false;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO identities (id, type, name, avatar, profile) "
        "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(type);
    q.addBindValue(name);
    q.addBindValue(avatar);
    q.addBindValue(profileJson);
    if (!q.exec()) {
        qWarning() << "[ChatPersistenceService] saveIdentityToDb 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

QJsonArray ChatPersistenceService::loadIdentitiesFromDb() const
{
    QJsonArray result;
    if (!DatabaseManager::instance()->isReady())
        return result;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT id, type, name, avatar, profile FROM identities"))) {
        qWarning() << "[ChatPersistenceService] loadIdentitiesFromDb 失败:" << q.lastError().text();
        return result;
    }
    while (q.next()) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), q.value(0).toString());
        obj.insert(QStringLiteral("type"), q.value(1).toString());
        obj.insert(QStringLiteral("name"), q.value(2).toString());
        obj.insert(QStringLiteral("avatar"), q.value(3).toString());

        const QString profileStr = q.value(4).toString();
        if (!profileStr.isEmpty()) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(profileStr.toUtf8(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject())
                obj.insert(QStringLiteral("profile"), doc.object());
        }
        result.append(obj);
    }
    return result;
}

bool ChatPersistenceService::saveSessionToDb(const QString& id, const QString& type, const QString& title, const QString& ownerId, const QStringList& participants, const QString& createdAt, const QString& lastActiveAt) const
{
    if (!DatabaseManager::instance()->isReady() || id.isEmpty())
        return false;

    QSqlDatabase db = DatabaseManager::instance()->connection();

    // 事务保证原子性
    db.transaction();

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO sessions (id, type, title, owner_id, created_at, last_active_at) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(id);
    q.addBindValue(type);
    q.addBindValue(title);
    q.addBindValue(ownerId);
    q.addBindValue(createdAt);
    q.addBindValue(lastActiveAt);
    if (!q.exec()) {
        qWarning() << "[ChatPersistenceService] saveSessionToDb 失败:" << q.lastError().text();
        db.rollback();
        return false;
    }

    // 清除旧参与者后重新插入
    QSqlQuery delPart(db);
    delPart.prepare(QStringLiteral("DELETE FROM session_participants WHERE session_id = ?"));
    delPart.addBindValue(id);
    delPart.exec();

    QSqlQuery insertPart(db);
    insertPart.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO session_participants (session_id, identity_id) VALUES (?, ?)"));
    for (const QString& pid : participants) {
        if (pid.trimmed().isEmpty())
            continue;
        insertPart.addBindValue(id);
        insertPart.addBindValue(pid.trimmed());
        insertPart.exec();
    }

    db.commit();
    return true;
}

QJsonArray ChatPersistenceService::loadSessionsFromDb() const
{
    QJsonArray result;
    if (!DatabaseManager::instance()->isReady())
        return result;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT id, type, title, owner_id, created_at, last_active_at FROM sessions"))) {
        qWarning() << "[ChatPersistenceService] loadSessionsFromDb 失败:" << q.lastError().text();
        return result;
    }
    while (q.next()) {
        QJsonObject obj;
        const QString sid = q.value(0).toString();
        obj.insert(QStringLiteral("id"), sid);
        obj.insert(QStringLiteral("type"), q.value(1).toString());
        obj.insert(QStringLiteral("title"), q.value(2).toString());
        obj.insert(QStringLiteral("ownerId"), q.value(3).toString());
        obj.insert(QStringLiteral("createdAt"), q.value(4).toString());
        obj.insert(QStringLiteral("lastActiveAt"), q.value(5).toString());

        // 加载参与者
        QSqlQuery pq(db);
        pq.prepare(QStringLiteral(
            "SELECT identity_id FROM session_participants WHERE session_id = ?"));
        pq.addBindValue(sid);
        QJsonArray parts;
        if (pq.exec()) {
            while (pq.next())
                parts.append(pq.value(0).toString());
        }
        obj.insert(QStringLiteral("participants"), parts);
        result.append(obj);
    }
    return result;
}

bool ChatPersistenceService::removeSessionFromDb(const QString& sessionId) const
{
    if (!DatabaseManager::instance()->isReady() || sessionId.isEmpty())
        return false;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    db.transaction();

    QSqlQuery q1(db);
    q1.prepare(QStringLiteral("DELETE FROM messages WHERE session_id = ?"));
    q1.addBindValue(sessionId);
    q1.exec();

    QSqlQuery q2(db);
    q2.prepare(QStringLiteral("DELETE FROM session_participants WHERE session_id = ?"));
    q2.addBindValue(sessionId);
    q2.exec();

    QSqlQuery q3(db);
    q3.prepare(QStringLiteral("DELETE FROM pending_turns WHERE session_id = ?"));
    q3.addBindValue(sessionId);
    q3.exec();

    QSqlQuery q4(db);
    q4.prepare(QStringLiteral("DELETE FROM sessions WHERE id = ?"));
    q4.addBindValue(sessionId);
    q4.exec();

    db.commit();
    return true;
}

bool ChatPersistenceService::setAppState(const QString& key, const QString& value) const
{
    if (!DatabaseManager::instance()->isReady() || key.isEmpty())
        return false;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO app_state (key, value) VALUES (?, ?)"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}

QString ChatPersistenceService::getAppState(const QString& key, const QString& defaultValue) const
{
    if (!DatabaseManager::instance()->isReady() || key.isEmpty())
        return defaultValue;

    QSqlDatabase db = DatabaseManager::instance()->connection();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT value FROM app_state WHERE key = ?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return defaultValue;
}
