#ifndef MEMORYMANAGER_H
#define MEMORYMANAGER_H

#include "TurnManager.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>

class ChatPersistenceService;
class Identity;

class MemoryManager {
public:
    explicit MemoryManager(ChatPersistenceService* persistence = nullptr);

    void setPersistence(ChatPersistenceService* persistence);

    bool ensureUserMemoryDocument(QString* error = nullptr) const;
    bool initializeForAgent(const Identity* agent, QString* error = nullptr) const;
    bool removeAgentMemory(const QString& agentId, QString* error = nullptr) const;

    QString composeMemoryContext(const QString& agentId, int maxChars = 6000) const;
    QString memoryIndexPath(const QString& agentId) const;
    bool rebuildSearchIndex(const QString& agentId, QJsonObject* metadata = nullptr, QString* error = nullptr) const;
    bool reflectionEnabled() const;
    int reflectionIntervalTurns() const;
    bool reflectAndScore(const QString& agentId, const QString& sessionId, const QString& turnId, const QString& traceId, QString* summary = nullptr, QString* writtenPath = nullptr, QJsonObject* metadata = nullptr, QString* error = nullptr) const;
    bool retainTurn(const QString& agentId, const QString& sessionId, const TurnTask& turn, QString* summary = nullptr, QString* writtenPath = nullptr, QJsonObject* metadata = nullptr, QString* error = nullptr) const;
    bool rememberManual(const QString& agentId, const QString& sessionId, const QString& turnId, const QString& traceId, const QString& text, QString* summary = nullptr, QString* writtenPath = nullptr, QJsonObject* metadata = nullptr, QString* error = nullptr) const;
    bool rememberToolRequested(const QString& agentId, const QString& sessionId, const QString& turnId, const QString& traceId, const QString& text, const QString& reason = QString(), QString* summary = nullptr, QString* writtenPath = nullptr, QJsonObject* metadata = nullptr, QString* error = nullptr) const;

private:
    QJsonObject readPolicyObject() const;
    QString dataRootPath() const;
    QString agentsRootPath() const;
    QString agentDirPath(const QString& agentId) const;
    QString dailyMemoryDirPath(const QString& agentId) const;

    QString userDocPath() const;
    QString sharedWorkDocPath() const;
    QString policyPath() const;
    QString soulDocPath(const QString& agentId) const;
    QString identityDocPath(const QString& agentId) const;
    QString longTermMemoryDocPath(const QString& agentId) const;
    QString userViewDocPath(const QString& agentId) const;
    QString dailyMemoryPath(const QString& agentId) const;
    QStringList latestDailyMemoryPaths(const QString& agentId, int maxFiles) const;

    QString buildUserTemplate() const;
    QString buildSharedWorkTemplate() const;
    QString buildPolicyTemplate() const;
    QString buildSoulTemplate(const Identity* agent) const;
    QString buildIdentityTemplate(const Identity* agent) const;
    QString buildLongTermMemoryTemplate() const;
    QString buildUserViewTemplate(const Identity* agent) const;

    bool ensureAgentMemoryDirs(const QString& agentId, QString* error = nullptr) const;
    bool writeIfMissing(const QString& filePath, const QString& content, QString* error = nullptr) const;
    bool writeIfChanged(const QString& filePath, const QString& content, QString* error = nullptr) const;
    QString memoryStewardAgentId() const;
    bool shouldUpdateSharedWork(const QString& agentId) const;

    QString sanitizeSingleLine(const QString& text, int maxChars) const;

    ChatPersistenceService* m_persistence = nullptr;
};

#endif // MEMORYMANAGER_H

