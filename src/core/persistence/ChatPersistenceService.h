#ifndef CHATPERSISTENCESERVICE_H
#define CHATPERSISTENCESERVICE_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

class IdentityProfile;
struct LLMConfig;
struct Message;

class ChatPersistenceService {
public:
    struct TabState {
        QStringList openAgentIds;
        QString activeIdentityId;
    };

    QString dataRootPath() const;
    QString configDirPath() const;
    QString appStatePath() const;
    QString manifestPath() const;
    QString identitiesDirPath() const;
    QString agentsDirPath() const;
    QString userIdentityPath() const;
    QString agentProfilePath(const QString& agentId) const;
    QString sessionsDirPath() const;
    QString sessionsIndexPath() const;
    QString logsDirPath() const;
    QString eventsCurrentLogPath() const;
    QString sessionDataDirPath(const QString& sessionId) const;
    QString sessionMetaPath(const QString& sessionId) const;
    QString sessionMessagesPath(const QString& sessionId) const;
    QString sessionPendingTurnsPath(const QString& sessionId) const;

    QString mcpConfigPath() const;
    QString modelConfigPath() const;
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

    /// 从 SQLite 加载指定会话的全部消息
    QList<Message> loadMessagesFromDb(const QString& sessionId) const;

    /// 从 SQLite 加载指定会话中 rowid > lastRowId 的增量消息
    QList<Message> loadNewMessagesFromDb(const QString& sessionId, qint64 lastRowId) const;

    /// 获取指定会话中最大的 rowid
    qint64 maxMessageRowId(const QString& sessionId) const;

    /// 保存身份到 SQLite（INSERT OR REPLACE）
    bool saveIdentityToDb(const QString& id, const QString& type, const QString& name, const QString& avatar, const QString& profileJson) const;

    /// 从 SQLite 加载所有身份（返回 JSON 对象数组）
    QJsonArray loadIdentitiesFromDb() const;

    /// 保存会话元数据到 SQLite
    bool saveSessionToDb(const QString& id, const QString& type, const QString& title, const QString& ownerId, const QStringList& participants, const QString& createdAt, const QString& lastActiveAt) const;

    /// 从 SQLite 加载所有会话元数据
    QJsonArray loadSessionsFromDb() const;

    /// 从 SQLite 删除会话及其关联数据
    bool removeSessionFromDb(const QString& sessionId) const;

    /// 保存应用状态 KV
    bool setAppState(const QString& key, const QString& value) const;

    /// 读取应用状态 KV
    QString getAppState(const QString& key, const QString& defaultValue = QString()) const;

private:
    bool ensureParentDir(const QString& filePath) const;
    bool writeJsonDocument(const QString& filePath, const QJsonDocument& doc) const;
    void rotateEventLogIfNeeded() const;
};

#endif // CHATPERSISTENCESERVICE_H
