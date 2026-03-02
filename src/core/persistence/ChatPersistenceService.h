#ifndef CHATPERSISTENCESERVICE_H
#define CHATPERSISTENCESERVICE_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
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

private:
    bool ensureParentDir(const QString& filePath) const;
    bool writeJsonDocument(const QString& filePath, const QJsonDocument& doc) const;
    void rotateEventLogIfNeeded() const;
};

#endif // CHATPERSISTENCESERVICE_H
