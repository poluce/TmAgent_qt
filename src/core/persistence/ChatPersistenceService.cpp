#include "ChatPersistenceService.h"

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

} // namespace

QString ChatPersistenceService::dataRootPath() const
{
    return QDir::home().filePath(QStringLiteral(".tmagent"));
}

QString ChatPersistenceService::configDirPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("config"));
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

QString ChatPersistenceService::mcpConfigPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("mcp_servers.json"));
}

QString ChatPersistenceService::modelConfigPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("models.yaml"));
}

QString ChatPersistenceService::memoryPolicyPath() const
{
    return QDir(configDirPath()).filePath(QStringLiteral("memory_policy.json"));
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
    return appendJsonLine(sessionMessagesPath(sessionId), messageObj);
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
