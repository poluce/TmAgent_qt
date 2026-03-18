#ifndef CHATPERSISTENCESERVICE_H
#define CHATPERSISTENCESERVICE_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/service/include/ConversationContextTypes.h"

class IdentityProfile;
class ChatStateRepository;
struct LLMConfig;
struct Message;

class ChatPersistenceService {
public:
    struct TabState {
        QStringList openAgentIds;
        QString activeIdentityId;
    };

    static QString defaultDataRootPath();
    static QString defaultConfigDirPath();
    static QString defaultModelConfigPath();

    QString dataRootPath() const;
    QString configDirPath() const;
    QString identitiesDirPath() const;
    QString agentsDirPath() const;
    QString sessionsDirPath() const;
    QString sessionDataDirPath(const QString& sessionId) const;

    QString mcpConfigPath() const;
    QString modelConfigPath() const;
    void setModelConfigPathOverride(const QString& filePath);
    QString memoryPolicyPath() const;
    QString scheduledJobsPath() const;
    QString agentHeartbeatConfigPath(const QString& agentId) const;
    QString agentHeartbeatInstructionPath(const QString& agentId) const;

    QJsonObject readJsonObject(const QString& filePath, bool* ok = nullptr) const;
    QJsonArray readJsonLines(const QString& filePath, bool* ok = nullptr) const;
    bool writeJsonObject(const QString& filePath, const QJsonObject& obj) const;
    bool writeJsonLines(const QString& filePath, const QJsonArray& lines) const;
    bool appendJsonLine(const QString& filePath, const QJsonObject& line) const;

    QJsonArray stringListToJson(const QStringList& values) const;
    QStringList stringListFromJson(const QJsonValue& value) const;
    QJsonObject messageToJson(const Message& msg) const;
    Message messageFromJson(const QJsonObject& obj, const QString& fallbackSessionId) const;
    QJsonObject identityProfileToJson(const IdentityProfile* profile) const;
    IdentityProfile* identityProfileFromJson(const QJsonObject& obj, const LLMConfig& fallbackConfig) const;

    QStringList loadMcpConfigSpecs() const;
    bool saveMcpConfigSpecs(const QStringList& specs) const;

    bool appendSessionMessage(const QString& sessionId, const QJsonObject& messageObj) const;
    bool appendEventLog(const QJsonObject& event) const;

    void saveTabState(const QStringList& openAgentIds, const QString& activeIdentityId) const;
    TabState loadTabState() const;

    // ─── SQLite 数据库操作（新） ───

    /// 将消息插入 SQLite（INSERT OR IGNORE）
    bool insertMessageToDb(const Message& msg, const QString& source = QStringLiteral("gui")) const;

    /// 写入事件到 SQLite（INSERT INTO events）
    bool insertEventToDb(const QJsonObject& event) const;

    /// 从 SQLite 加载指定会话的全部消息
    QList<Message> loadMessagesFromDb(const QString& sessionId) const;

    /// 从 SQLite 加载指定会话中 rowid > lastRowId 的增量消息
    QList<Message> loadNewMessagesFromDb(const QString& sessionId, qint64 lastRowId) const;

    /// 获取指定会话中最大的 rowid
    qint64 maxMessageRowId(const QString& sessionId) const;

    /// 保存身份到 SQLite（INSERT OR REPLACE）
    bool saveIdentityToDb(const QString& id, const QString& type, const QString& name, const QString& avatar, const QString& profileJson) const;
    bool removeIdentityFromDb(const QString& id) const;

    /// 从 SQLite 加载所有身份（返回 JSON 对象数组）
    QJsonArray loadIdentitiesFromDb() const;

    /// 保存会话元数据到 SQLite
    bool saveSessionToDb(const QString& id, const QString& type, const QString& title, const QString& ownerId, const QStringList& participants, const QString& createdAt, const QString& lastActiveAt) const;

    /// 从 SQLite 加载所有会话元数据
    QJsonArray loadSessionsFromDb() const;

    /// 从 SQLite 删除会话及其关联数据
    bool removeSessionFromDb(const QString& sessionId) const;

    /// 保存指定会话的 pending turns（覆盖写）
    bool savePendingTurnsToDb(const QString& sessionId, const QJsonArray& turns) const;

    /// 读取指定会话的 pending turns
    QJsonArray loadPendingTurnsFromDb(const QString& sessionId) const;

    /// 保存应用状态 KV
    bool setAppState(const QString& key, const QString& value) const;

    /// 读取应用状态 KV
    QString getAppState(const QString& key, const QString& defaultValue = QString()) const;

    /// 删除应用状态 KV
    bool removeAppState(const QString& key) const;

    /// 一次性导入 legacy JSONL 事件日志到 SQLite
    bool importLegacyEventLogsToDb(qint64* importedCount = nullptr) const;

    /// 获取 SQLite events 总数
    qint64 eventCountInDb() const;

    QString contextSnapshotPath(const QString& sessionId) const;
    QString contextCheckpointPath(const QString& sessionId) const;
    QString contextResumePacketPath(const QString& sessionId) const;

    bool saveTaskContextSnapshot(const QString& sessionId, const ConversationContext::TaskContextSnapshot& snapshot) const;
    ConversationContext::TaskContextSnapshot loadTaskContextSnapshot(const QString& sessionId, bool* ok = nullptr) const;
    bool saveContextCompressionCheckpoint(const QString& sessionId, const ConversationContext::ContextCompressionCheckpoint& checkpoint) const;
    ConversationContext::ContextCompressionCheckpoint loadContextCompressionCheckpoint(const QString& sessionId, bool* ok = nullptr) const;
    bool saveResumePacket(const QString& sessionId, const ConversationContext::ResumePacket& packet) const;
    ConversationContext::ResumePacket loadResumePacket(const QString& sessionId, bool* ok = nullptr) const;

private:
    bool ensureParentDir(const QString& filePath) const;
    bool writeJsonDocument(const QString& filePath, const QJsonDocument& doc) const;

    QString m_modelConfigPathOverride;
};

#endif // CHATPERSISTENCESERVICE_H
