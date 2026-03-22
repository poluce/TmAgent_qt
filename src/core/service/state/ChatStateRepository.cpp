#include "ChatStateRepository.h"

#include "core/agent/ToolDispatcher.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Message.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include "core/persistence/DatabaseManager.h"
#include "core/tools/AgentToolNames.h"
#include "llm/LLMTypes.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

ChatStateRepository::ChatStateRepository() = default;

void ChatStateRepository::setDependencies(IdentityManager* identityManager, SessionManager* sessionManager, ToolDispatcher* toolDispatcher, ChatPersistenceService* persistence)
{
    m_identityManager = identityManager;
    m_sessionManager = sessionManager;
    m_toolDispatcher = toolDispatcher;
    m_persistence = persistence;
}

bool ChatStateRepository::isReady() const
{
    return m_identityManager && m_sessionManager && m_persistence;
}

QStringList ChatStateRepository::collectToolNamesFrom(ToolDispatcher* dispatcher)
{
    QStringList names;
    if (!dispatcher)
        return names;

    const QList<Tool> tools = dispatcher->getAllToolSchemas();
    for (const Tool& tool : tools) {
        const QString name = tool.name.trimmed();
        if (!name.isEmpty())
            names.append(name);
    }
    names.removeDuplicates();

    return names;
}

QStringList ChatStateRepository::collectToolNames() const
{
    return collectToolNamesFrom(m_toolDispatcher);
}

QString ChatStateRepository::remapIdentityId(const QString& oldId, const QHash<QString, QString>& identityIdMap)
{
    const QString trimmed = oldId.trimmed();
    if (trimmed.isEmpty())
        return QString();
    return identityIdMap.value(trimmed, trimmed);
}

namespace {

QString remapIdentityIdLocal(const QString& oldId, const QHash<QString, QString>& identityIdMap)
{
    const QString trimmed = oldId.trimmed();
    if (trimmed.isEmpty())
        return QString();
    return identityIdMap.value(trimmed, trimmed);
}

QString legacyAgentProfileFilePath(const ChatPersistenceService* persistence, const QString& agentId)
{
    return QDir(QDir(persistence->agentsDirPath()).filePath(agentId))
        .filePath(QStringLiteral("profile.json"));
}

QString legacySessionDataDirPath(const ChatPersistenceService* persistence, const QString& sessionId)
{
    return QDir(QDir(persistence->sessionsDirPath()).filePath(QStringLiteral("data")))
        .filePath(sessionId);
}

QString legacySessionFilePath(const ChatPersistenceService* persistence,
                              const QString& sessionId,
                              const QString& fileName)
{
    return QDir(legacySessionDataDirPath(persistence, sessionId)).filePath(fileName);
}

void clearLoadedState(SessionManager* sessionManager, IdentityManager* identityManager)
{
    if (sessionManager) {
        for (Session* session : sessionManager->allSessions())
            sessionManager->removeSession(session->id());
    }

    if (identityManager) {
        const QList<Identity*> existingAgents = identityManager->allAgents();
        for (Identity* agent : existingAgents) {
            if (agent)
                identityManager->removeAgent(agent->id());
        }
    }
}

QStringList mergedAllowedTools(IdentityProfile* profile, const QStringList& currentTools)
{
    QStringList allowedTools = profile ? profile->allowedTools() : QStringList();
    const bool delegateEnabled = profile && profile->delegateEnabled();

    if (allowedTools.isEmpty()) {
        allowedTools = currentTools;
    } else {
        for (const QString& toolName : currentTools) {
            if (!allowedTools.contains(toolName))
                allowedTools.append(toolName);
        }
    }

    if (!delegateEnabled) {
        for (const QString& toolName : AgentToolNames::all())
            allowedTools.removeAll(toolName);
    }

    return allowedTools;
}

QString restoreUserIdentity(IdentityManager* identityManager,
                            const QJsonObject& userObj,
                            QHash<QString, QString>* identityIdMap)
{
    if (!identityManager || !identityIdMap)
        return QString();

    const QString persistedUserId = userObj.value(QStringLiteral("id")).toString().trimmed();
    Identity* userIdentity = identityManager->userIdentity(persistedUserId);
    const QString userId = userIdentity ? userIdentity->id() : QString();
    if (userIdentity)
        userIdentity->setName(QStringLiteral("Me"));

    if (!userId.isEmpty())
        identityIdMap->insert(userId, userId);

    if (!userObj.isEmpty() && userIdentity) {
        const QString oldUserId = userObj.value(QStringLiteral("id")).toString().trimmed();
        if (!oldUserId.isEmpty())
            identityIdMap->insert(oldUserId, userId);

        const QString userName = userObj.value(QStringLiteral("name")).toString().trimmed();
        if (!userName.isEmpty())
            userIdentity->setName(userName);

        const QString userAvatar = userObj.value(QStringLiteral("avatar")).toString().trimmed();
        if (!userAvatar.isEmpty())
            userIdentity->setAvatar(userAvatar);
    }

    return userId;
}

void restoreAgentIdentity(IdentityManager* identityManager,
                          ChatPersistenceService* persistence,
                          const QJsonObject& agentObj,
                          const LLMConfig& defaultConfig,
                          const QStringList& currentTools,
                          QHash<QString, QString>* identityIdMap,
                          const QString& fallbackPreferredId = QString())
{
    if (!identityManager || !persistence || !identityIdMap)
        return;
    if (agentObj.value(QStringLiteral("type")).toString().trimmed() != QLatin1String("agent"))
        return;

    const QString oldAgentId = agentObj.value(QStringLiteral("id")).toString().trimmed();
    const QString agentName = agentObj.value(QStringLiteral("name")).toString().trimmed();
    const QString avatar = agentObj.value(QStringLiteral("avatar")).toString().trimmed();

    IdentityProfile* profile = persistence->identityProfileFromJson(
        agentObj.value(QStringLiteral("profile")).toObject(), defaultConfig);
    profile->setAllowedTools(mergedAllowedTools(profile, currentTools));

    const QString preferredAgentId = !oldAgentId.isEmpty() ? oldAgentId : fallbackPreferredId.trimmed();
    Identity* agent = identityManager->createAgent(
        agentName.isEmpty() ? QStringLiteral("TM Agent") : agentName,
        profile,
        preferredAgentId);
    if (!avatar.isEmpty())
        agent->setAvatar(avatar);
    if (!oldAgentId.isEmpty())
        identityIdMap->insert(oldAgentId, agent->id());
    if (!fallbackPreferredId.trimmed().isEmpty())
        identityIdMap->insert(fallbackPreferredId.trimmed(), agent->id());
}

Session* restoreSessionShell(SessionManager* sessionManager,
                             IdentityManager* identityManager,
                             ChatPersistenceService* persistence,
                             const QJsonObject& sessionObj,
                             const QHash<QString, QString>& identityIdMap,
                             const QString& userId,
                             const LLMConfig& defaultConfig,
                             const QStringList& currentTools)
{
    if (!sessionManager || !identityManager || !persistence)
        return nullptr;

    const QString sessionId = sessionObj.value(QStringLiteral("id")).toString().trimmed();
    if (sessionId.isEmpty())
        return nullptr;

    const QString type = sessionObj.value(QStringLiteral("type")).toString().trimmed();
    const QString title = sessionObj.value(QStringLiteral("title")).toString();

    QString ownerId = remapIdentityIdLocal(
        sessionObj.value(QStringLiteral("ownerId")).toString(), identityIdMap);
    QStringList participants = persistence->stringListFromJson(sessionObj.value(QStringLiteral("participants")));
    for (QString& pid : participants)
        pid = remapIdentityIdLocal(pid, identityIdMap);
    participants.removeAll(QString());
    participants.removeDuplicates();

    Session* session = nullptr;
    if (type == QLatin1String("group")) {
        if (ownerId.isEmpty() && !participants.isEmpty())
            ownerId = participants.first();
        if (ownerId.isEmpty())
            ownerId = userId;
        if (!participants.contains(ownerId))
            participants.prepend(ownerId);
        session = sessionManager->createGroupSession(ownerId, participants, title);
    } else {
        QString pA;
        QString pB;
        if (!ownerId.isEmpty() && participants.contains(ownerId)) {
            pA = ownerId;
            for (const QString& pid : participants) {
                if (pid != ownerId) {
                    pB = pid;
                    break;
                }
            }
        } else {
            pA = participants.value(0);
            pB = participants.value(1);
        }

        if (pA.isEmpty())
            pA = userId;
        if (pB.isEmpty()) {
            auto* profile = new IdentityProfile();
            profile->setLlmConfig(defaultConfig);
            profile->setSystemPrompt(defaultConfig.systemPrompt);
            profile->setAllowedTools(currentTools);
            Identity* agent = identityManager->createAgent(
                title.isEmpty() ? QStringLiteral("TM Agent") : title,
                profile);
            pB = agent->id();
        }

        session = sessionManager->createPrivateSession(pA, pB);
        session->setTitle(title);
    }

    if (!session)
        return nullptr;

    const QString generatedId = session->id();
    session->setId(sessionId);
    sessionManager->replaceSessionId(generatedId, sessionId);
    if (!title.isEmpty())
        session->setTitle(title);
    return session;
}

void restoreSessionMessages(Session* session,
                            IdentityManager* identityManager,
                            const QList<Message>& messages,
                            const QHash<QString, QString>& identityIdMap)
{
    if (!session || !identityManager)
        return;

    QString userParticipantId;
    QString agentParticipantId;
    if (session->type() == Session::SessionType::Private) {
        const QStringList pids = session->participantIds();
        for (const QString& pid : pids) {
            Identity* identity = identityManager->findById(pid);
            if (!identity)
                continue;
            if (identity->isUser()) {
                if (userParticipantId.isEmpty())
                    userParticipantId = pid;
            } else {
                if (agentParticipantId.isEmpty())
                    agentParticipantId = pid;
            }
        }
    }

    QHash<QString, QString> inferredSenderMap;
    QHash<QString, QString> firstRawSenderByTurn;

    for (Message msg : messages) {
        const QString rawSenderId = msg.senderId;
        msg.sessionId = session->id();
        msg.senderId = remapIdentityIdLocal(msg.senderId, identityIdMap);

        if (!msg.senderId.isEmpty()
            && msg.senderId != QLatin1String("system")
            && !identityManager->findById(msg.senderId)
            && session->type() == Session::SessionType::Private) {
            const QString normalizedRaw = rawSenderId.trimmed();
            if (!normalizedRaw.isEmpty()) {
                QString inferred = inferredSenderMap.value(normalizedRaw);
                if (inferred.isEmpty()
                    && !userParticipantId.isEmpty()
                    && !agentParticipantId.isEmpty()) {
                    const QString turnId = msg.turnId.trimmed();
                    if (!turnId.isEmpty()) {
                        const QString firstRaw = firstRawSenderByTurn.value(turnId);
                        if (firstRaw.isEmpty()) {
                            firstRawSenderByTurn.insert(turnId, normalizedRaw);
                            inferred = userParticipantId;
                        } else if (firstRaw != normalizedRaw) {
                            inferred = agentParticipantId;
                        }
                    }
                }
                if (!inferred.isEmpty()) {
                    inferredSenderMap.insert(normalizedRaw, inferred);
                    msg.senderId = inferred;
                }
            }
        }

        for (QString& mention : msg.mentions)
            mention = remapIdentityIdLocal(mention, identityIdMap);
        msg.mentions.removeAll(QString());
        msg.mentions.removeDuplicates();

        if (msg.isValid())
            session->addMessage(msg);
    }
}

QList<TurnTask> restorePendingTurns(const QJsonArray& pendingTurns, const QString& userId)
{
    QList<TurnTask> restoredTurns;
    for (const QJsonValue& item : pendingTurns) {
        const QJsonObject turnObj = item.toObject();
        const QString state = turnObj.value(QStringLiteral("state")).toString().trimmed();
        if (state == QLatin1String("running"))
            continue;

        const QString userContent = turnObj.value(QStringLiteral("user")).toString().trimmed();
        if (userContent.isEmpty())
            continue;

        TurnTask turn;
        turn.requestTraceId = turnObj.value(QStringLiteral("requestTraceId")).toString().trimmed();
        if (turn.requestTraceId.isEmpty())
            turn.requestTraceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        turn.turnId = turnObj.value(QStringLiteral("turnId")).toString().trimmed();
        if (turn.turnId.isEmpty())
            turn.turnId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        turn.runId = turnObj.value(QStringLiteral("runId")).toString().trimmed();
        if (turn.runId.isEmpty())
            turn.runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        turn.actorIdentityId = turnObj.value(QStringLiteral("actorIdentityId")).toString().trimmed();
        if (turn.actorIdentityId.isEmpty())
            turn.actorIdentityId = userId;
        turn.enqueuedAtMs = static_cast<qint64>(turnObj.value(QStringLiteral("enqueuedAtMs")).toDouble(0));
        turn.mergedMessageCount = qMax(1, turnObj.value(QStringLiteral("mergedMessageCount")).toInt(1));
        turn.clientMessageId = turnObj.value(QStringLiteral("clientMessageId")).toString().trimmed();
        turn.userContent = userContent;
        restoredTurns.append(turn);
    }
    return restoredTurns;
}

bool isSupportedSessionType(const QString& type)
{
    return type == QLatin1String("private") || type == QLatin1String("group");
}

void restoreSessionRecord(SessionManager* sessionManager,
                          IdentityManager* identityManager,
                          ChatPersistenceService* persistence,
                          const QJsonObject& sessionObj,
                          const QList<Message>& messages,
                          const QJsonArray& pendingTurns,
                          const QHash<QString, QString>& identityIdMap,
                          const QString& userId,
                          const LLMConfig& defaultConfig,
                          const QStringList& currentTools,
                          ChatStateRepository::LoadResult* result)
{
    if (!result)
        return;

    const QString sessionId = sessionObj.value(QStringLiteral("id")).toString().trimmed();
    if (sessionId.isEmpty())
        return;

    const QString type = sessionObj.value(QStringLiteral("type")).toString().trimmed();
    if (!isSupportedSessionType(type))
        return;

    Session* session = restoreSessionShell(
        sessionManager,
        identityManager,
        persistence,
        sessionObj,
        identityIdMap,
        userId,
        defaultConfig,
        currentTools);
    if (!session)
        return;

    restoreSessionMessages(session, identityManager, messages, identityIdMap);
    result->savedMessageCounts.insert(session->id(), session->messageCount());

    const QList<TurnTask> restoredTurns = restorePendingTurns(pendingTurns, userId);
    if (!restoredTurns.isEmpty())
        result->pendingTurnsBySession.insert(session->id(), restoredTurns);
}

}

void ChatStateRepository::saveDirectoryStructure() const
{
    const bool useDbAsPrimary = DatabaseManager::instance()->isReady();
    QDir().mkpath(m_persistence->dataRootPath());
    QDir().mkpath(m_persistence->configDirPath());
    QDir().mkpath(m_persistence->identitiesDirPath());
    QDir().mkpath(m_persistence->agentsDirPath());
    if (!useDbAsPrimary) {
        QDir().mkpath(QDir(m_persistence->dataRootPath()).filePath(QStringLiteral("logs")));
        QDir().mkpath(QDir(m_persistence->sessionsDirPath()).filePath(QStringLiteral("data")));
    }
}

void ChatStateRepository::saveManifestAndAppState(const QString& currentSessionId) const
{
    if (DatabaseManager::instance()->isReady()) {
        m_persistence->setAppState(QStringLiteral("currentSessionId"), currentSessionId.trimmed());
        m_persistence->setAppState(QStringLiteral("storageBackend"), QStringLiteral("sqlite"));
    }
}

QStringList ChatStateRepository::saveIdentities() const
{
    QStringList activeAgentIds;
    QStringList activeIdentityIds;

    Identity* user = m_identityManager->userIdentity();
    if (user) {
        m_persistence->saveIdentityToDb(user->id(), QStringLiteral("user"), user->name(), user->avatar(), QString());
        activeIdentityIds.append(user->id());
    }

    const QList<Identity*> identities = m_identityManager->allIdentities();
    for (Identity* identity : identities) {
        if (!identity || !identity->isAgent())
            continue;
        activeAgentIds.append(identity->id());

        const QString profileJson = QString::fromUtf8(
            QJsonDocument(m_persistence->identityProfileToJson(identity->profile())).toJson(QJsonDocument::Compact));
        m_persistence->saveIdentityToDb(
            identity->id(), QStringLiteral("agent"), identity->name(), identity->avatar(), profileJson);
        activeIdentityIds.append(identity->id());
    }

    activeAgentIds.removeDuplicates();
    activeIdentityIds.removeDuplicates();

    if (DatabaseManager::instance()->isReady()) {
        const QJsonArray persistedIdentities = m_persistence->loadIdentitiesFromDb();
        for (const QJsonValue& value : persistedIdentities) {
            const QString identityId = value.toObject().value(QStringLiteral("id")).toString().trimmed();
            if (!identityId.isEmpty() && !activeIdentityIds.contains(identityId))
                m_persistence->removeIdentityFromDb(identityId);
        }
    }

    return activeAgentIds;
}

void ChatStateRepository::cleanupStaleAgentDirs(const QStringList& activeAgentIds) const
{
    QDir agentsDir(m_persistence->agentsDirPath());
    const QStringList persistedAgentDirs = agentsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& dirName : persistedAgentDirs) {
        if (!activeAgentIds.contains(dirName))
            QDir(agentsDir.filePath(dirName)).removeRecursively();
    }
}

static QJsonObject turnTaskToJson(const TurnTask& turn, const QString& state)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("state"), state);
    obj.insert(QStringLiteral("requestTraceId"), turn.requestTraceId);
    obj.insert(QStringLiteral("turnId"), turn.turnId);
    obj.insert(QStringLiteral("runId"), turn.runId);
    obj.insert(QStringLiteral("actorIdentityId"), turn.actorIdentityId);
    obj.insert(QStringLiteral("enqueuedAtMs"), static_cast<double>(turn.enqueuedAtMs));
    obj.insert(QStringLiteral("mergedMessageCount"), qMax(1, turn.mergedMessageCount));
    obj.insert(QStringLiteral("clientMessageId"), turn.clientMessageId);
    obj.insert(QStringLiteral("user"), turn.userContent);
    return obj;
}

QJsonObject ChatStateRepository::serializePendingTurns(const SessionPipeline* pipeline) const
{
    if (!pipeline)
        return QJsonObject();

    QJsonArray pendingTurns;
    if (pipeline->hasActiveTurn)
        pendingTurns.append(turnTaskToJson(pipeline->activeTurn, QStringLiteral("running")));
    for (const TurnTask& turn : pipeline->queue)
        pendingTurns.append(turnTaskToJson(turn, QStringLiteral("queued")));

    if (pendingTurns.isEmpty())
        return QJsonObject();

    QJsonObject obj;
    obj.insert(QStringLiteral("turns"), pendingTurns);
    return obj;
}

QHash<QString, int> ChatStateRepository::saveState(
    const QString& currentSessionId,
    const QHash<QString, int>& previousMessageCounts,
    const std::function<const SessionPipeline*(const QString&)>& pipelineLookup) const
{
    QHash<QString, int> savedCounts = previousMessageCounts;
    if (!isReady())
        return savedCounts;
    if (!DatabaseManager::instance()->isReady())
        return savedCounts;

    saveDirectoryStructure();
    saveManifestAndAppState(currentSessionId);

    const QStringList activeAgentIds = saveIdentities();
    cleanupStaleAgentDirs(activeAgentIds);

    QStringList activeSessionIds;
    const QList<Session*> sessions = m_sessionManager->allSessions();
    for (Session* session : sessions) {
        if (!session)
            continue;
        activeSessionIds.append(session->id());

        const QString sessionType = session->type() == Session::SessionType::Group
            ? QStringLiteral("group")
            : QStringLiteral("private");

        // DB 双写：会话元数据
        m_persistence->saveSessionToDb(
            session->id(), sessionType, session->title(), session->ownerId(),
            session->participantIds(),
            session->createdAt().toString(Qt::ISODateWithMs),
            session->lastActiveAt().toString(Qt::ISODateWithMs));

        const QString sid = session->id();
        const int currentMessageCount = session->messageCount();
        const bool needsMessageRewrite = savedCounts.value(sid, -1) != currentMessageCount;
        if (needsMessageRewrite) {
            savedCounts.insert(sid, currentMessageCount);

            const QList<Message> messages = session->allMessages();
            for (const Message& msg : messages)
                m_persistence->insertMessageToDb(msg);
        }

        const SessionPipeline* pipeline = pipelineLookup ? pipelineLookup(session->id()) : nullptr;
        const QJsonObject pendingObj = serializePendingTurns(pipeline);
        const QJsonArray pendingTurns = pendingObj.value(QStringLiteral("turns")).toArray();
        m_persistence->savePendingTurnsToDb(session->id(), pendingTurns);
    }

    activeSessionIds.removeDuplicates();
    const QJsonArray persistedSessions = m_persistence->loadSessionsFromDb();
    for (const QJsonValue& value : persistedSessions) {
        const QString sessionId = value.toObject().value(QStringLiteral("id")).toString().trimmed();
        if (!sessionId.isEmpty() && !activeSessionIds.contains(sessionId))
            m_persistence->removeSessionFromDb(sessionId);
    }

    for (auto it = savedCounts.begin(); it != savedCounts.end();) {
        if (activeSessionIds.contains(it.key()))
            ++it;
        else
            it = savedCounts.erase(it);
    }

    return savedCounts;
}

ChatStateRepository::LoadResult ChatStateRepository::loadState(const LLMConfig& defaultConfig) const
{
    if (!isReady())
        return LoadResult();

    if (DatabaseManager::instance()->isReady()) {
        const LoadResult dbLoaded = loadStateFromDb(defaultConfig);
        if (dbLoaded.success)
            return dbLoaded;
    }

    return loadStateFromLegacyFiles(defaultConfig);
}

ChatStateRepository::LoadResult ChatStateRepository::loadStateFromDb(const LLMConfig& defaultConfig) const
{
    LoadResult result;

    const QJsonArray dbIdentities = m_persistence->loadIdentitiesFromDb();
    const QJsonArray dbSessions = m_persistence->loadSessionsFromDb();
    const QString dbCurrentSessionId = m_persistence->getAppState(QStringLiteral("currentSessionId"));
    if (dbIdentities.isEmpty() && dbSessions.isEmpty() && dbCurrentSessionId.trimmed().isEmpty())
        return result;

    clearLoadedState(m_sessionManager, m_identityManager);

    QJsonObject userObj;
    for (const QJsonValue& value : dbIdentities) {
        const QJsonObject obj = value.toObject();
        if (obj.value(QStringLiteral("type")).toString().trimmed() == QLatin1String("user")) {
            userObj = obj;
            break;
        }
    }

    const QStringList currentTools = collectToolNames();
    QHash<QString, QString> identityIdMap;
    const QString userId = restoreUserIdentity(m_identityManager, userObj, &identityIdMap);

    for (const QJsonValue& value : dbIdentities) {
        restoreAgentIdentity(
            m_identityManager,
            m_persistence,
            value.toObject(),
            defaultConfig,
            currentTools,
            &identityIdMap);
    }

    for (const QJsonValue& value : dbSessions) {
        const QJsonObject sessionObj = value.toObject();
        const QString sessionId = sessionObj.value(QStringLiteral("id")).toString().trimmed();
        if (sessionId.isEmpty())
            continue;
        restoreSessionRecord(
            m_sessionManager,
            m_identityManager,
            m_persistence,
            sessionObj,
            m_persistence->loadMessagesFromDb(sessionId),
            m_persistence->loadPendingTurnsFromDb(sessionId),
            identityIdMap,
            userId,
            defaultConfig,
            currentTools,
            &result);
    }

    if (!dbCurrentSessionId.trimmed().isEmpty() && m_sessionManager->findById(dbCurrentSessionId.trimmed())) {
        result.currentSessionId = dbCurrentSessionId.trimmed();
    } else {
        const QList<Session*> sessions = m_sessionManager->allSessions();
        result.currentSessionId = sessions.isEmpty() ? QString() : sessions.first()->id();
    }

    result.success = true;
    result.loadedFromLegacyFiles = false;
    return result;
}

ChatStateRepository::LoadResult ChatStateRepository::loadStateFromLegacyFiles(const LLMConfig& defaultConfig) const
{
    LoadResult result;

    // Migration-only fallback for pre-SQLite state directories.
    bool manifestOk = false;
    const QJsonObject manifest = m_persistence->readJsonObject(
        QDir(m_persistence->dataRootPath()).filePath(QStringLiteral("manifest.json")),
        &manifestOk);
    if (!manifestOk)
        return result;

    const int schemaVersion = manifest.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion != 3) {
        qWarning() << "[ChatStateRepository] 不支持的数据目录版本，已跳过加载。expected=3 actual="
                   << schemaVersion;
        return result;
    }

    const QJsonObject appState = m_persistence->readJsonObject(
        QDir(m_persistence->configDirPath()).filePath(QStringLiteral("app_state.json")));

    clearLoadedState(m_sessionManager, m_identityManager);

    bool userOk = false;
    const QJsonObject userObj = m_persistence->readJsonObject(
        QDir(m_persistence->identitiesDirPath()).filePath(QStringLiteral("user.json")),
        &userOk);
    const QStringList currentTools = collectToolNames();
    QHash<QString, QString> identityIdMap;
    const QString userId = restoreUserIdentity(m_identityManager, userOk ? userObj : QJsonObject(), &identityIdMap);

    QDir agentsDir(m_persistence->agentsDirPath());
    const QStringList agentDirs = agentsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& agentDirName : agentDirs) {
        bool profileOk = false;
        const QJsonObject item = m_persistence->readJsonObject(
            legacyAgentProfileFilePath(m_persistence, agentDirName),
            &profileOk);
        if (!profileOk)
            continue;
        restoreAgentIdentity(
            m_identityManager,
            m_persistence,
            item,
            defaultConfig,
            currentTools,
            &identityIdMap,
            agentDirName.trimmed());
    }

    bool sessionsIndexOk = false;
    const QJsonObject sessionsIndex = m_persistence->readJsonObject(
        QDir(m_persistence->sessionsDirPath()).filePath(QStringLiteral("index.json")),
        &sessionsIndexOk);
    QJsonArray sessionsArr;
    if (sessionsIndexOk)
        sessionsArr = sessionsIndex.value(QStringLiteral("sessions")).toArray();

    for (const QJsonValue& value : sessionsArr) {
        const QJsonObject indexItem = value.toObject();
        const QString sessionId = indexItem.value(QStringLiteral("id")).toString().trimmed();
        if (sessionId.isEmpty()) {
            qWarning() << "[ChatStateRepository] 跳过无效会话项：缺少 id。";
            continue;
        }

        bool metaOk = false;
        const QJsonObject metaObj = m_persistence->readJsonObject(
            legacySessionFilePath(m_persistence, sessionId, QStringLiteral("meta.json")),
            &metaOk);
        const QJsonObject sessionObj = metaOk ? metaObj : indexItem;

        const QString type = sessionObj.value(QStringLiteral("type")).toString().trimmed();
        if (type != QLatin1String("private") && type != QLatin1String("group")) {
            qWarning() << "[ChatStateRepository] 跳过无效会话项：未知 type =" << type;
            continue;
        }

        bool messagesOk = false;
        const QJsonArray messagesArr = m_persistence->readJsonLines(
            legacySessionFilePath(m_persistence, sessionId, QStringLiteral("messages.jsonl")),
            &messagesOk);
        if (!messagesOk)
            qWarning() << "[ChatStateRepository] 会话消息读取失败，sessionId=" << sessionId;

        QList<Message> messages;
        for (const QJsonValue& mv : messagesArr) {
            const Message msg = m_persistence->messageFromJson(mv.toObject(), sessionId);
            if (msg.isValid())
                messages.append(msg);
        }

        bool pendingOk = false;
        const QJsonObject pendingObj = m_persistence->readJsonObject(
            legacySessionFilePath(m_persistence, sessionId, QStringLiteral("pending_turns.json")),
            &pendingOk);
        const QJsonArray pendingTurns = pendingOk ? pendingObj.value(QStringLiteral("turns")).toArray() : QJsonArray();
        restoreSessionRecord(
            m_sessionManager,
            m_identityManager,
            m_persistence,
            sessionObj,
            messages,
            pendingTurns,
            identityIdMap,
            userId,
            defaultConfig,
            currentTools,
            &result);
    }

    QString savedSessionId = appState.value(QStringLiteral("currentSessionId")).toString().trimmed();
    if (!savedSessionId.isEmpty() && m_sessionManager->findById(savedSessionId)) {
        result.currentSessionId = savedSessionId;
    } else {
        const QList<Session*> sessions = m_sessionManager->allSessions();
        result.currentSessionId = sessions.isEmpty() ? QString() : sessions.first()->id();
    }

    result.success = true;
    result.loadedFromLegacyFiles = DatabaseManager::instance()->isReady();
    return result;
}
