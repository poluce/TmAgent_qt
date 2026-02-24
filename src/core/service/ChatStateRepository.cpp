#include "ChatStateRepository.h"

#include "core/agent/ToolDispatcher.h"
#include "core/manager/IdentityManager.h"
#include "core/manager/SessionManager.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/model/Message.h"
#include "core/model/Session.h"
#include "core/persistence/ChatPersistenceService.h"
#include "newCore/LLMTypes.h"
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

    // delegate 系列工具为动态注册能力，不能仅依赖 dispatcher 当前快照。
    // 默认加入身份白名单，实际可见性由 LLMAgent 根据 recursionDepth 再次校验。
    const QStringList delegateTools = {
        QStringLiteral("delegate_task"),
        QStringLiteral("delegate_status"),
        QStringLiteral("delegate_cancel"),
        QStringLiteral("delegate_list_active")
    };
    for (const QString& toolName : delegateTools) {
        if (!names.contains(toolName))
            names.append(toolName);
    }

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

void ChatStateRepository::saveDirectoryStructure() const
{
    QDir().mkpath(m_persistence->dataRootPath());
    QDir().mkpath(m_persistence->configDirPath());
    QDir().mkpath(m_persistence->identitiesDirPath());
    QDir().mkpath(m_persistence->agentsDirPath());
    QDir().mkpath(m_persistence->logsDirPath());
    QDir().mkpath(QDir(m_persistence->sessionsDirPath()).filePath(QStringLiteral("data")));
}

void ChatStateRepository::saveManifestAndAppState(const QString& currentSessionId) const
{
    const QString nowIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    bool manifestOk = false;
    QJsonObject manifest = m_persistence->readJsonObject(m_persistence->manifestPath(), &manifestOk);
    if (!manifestOk)
        manifest = QJsonObject();
    manifest.insert(QStringLiteral("schemaVersion"), 3);
    if (!manifest.contains(QStringLiteral("createdAt")))
        manifest.insert(QStringLiteral("createdAt"), nowIso);
    manifest.insert(QStringLiteral("lastWrittenAt"), nowIso);
    m_persistence->writeJsonObject(m_persistence->manifestPath(), manifest);

    bool appStateOk = false;
    QJsonObject appState = m_persistence->readJsonObject(m_persistence->appStatePath(), &appStateOk);
    if (!appStateOk)
        appState = QJsonObject();
    appState.insert(QStringLiteral("schemaVersion"), 3);
    appState.insert(QStringLiteral("currentSessionId"), currentSessionId);
    m_persistence->writeJsonObject(m_persistence->appStatePath(), appState);
}

QStringList ChatStateRepository::saveIdentities() const
{
    QStringList activeAgentIds;

    Identity* user = m_identityManager->userIdentity();
    if (user) {
        QJsonObject userObj;
        userObj.insert(QStringLiteral("id"), user->id());
        userObj.insert(QStringLiteral("type"), QStringLiteral("user"));
        userObj.insert(QStringLiteral("name"), user->name());
        userObj.insert(QStringLiteral("avatar"), user->avatar());
        m_persistence->writeJsonObject(m_persistence->userIdentityPath(), userObj);
    }

    const QList<Identity*> identities = m_identityManager->allIdentities();
    for (Identity* identity : identities) {
        if (!identity || !identity->isAgent())
            continue;
        activeAgentIds.append(identity->id());

        QJsonObject profileObj;
        profileObj.insert(QStringLiteral("id"), identity->id());
        profileObj.insert(QStringLiteral("type"), QStringLiteral("agent"));
        profileObj.insert(QStringLiteral("name"), identity->name());
        profileObj.insert(QStringLiteral("avatar"), identity->avatar());
        profileObj.insert(QStringLiteral("profile"), m_persistence->identityProfileToJson(identity->profile()));
        m_persistence->writeJsonObject(m_persistence->agentProfilePath(identity->id()), profileObj);
    }

    activeAgentIds.removeDuplicates();
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

QJsonObject ChatStateRepository::buildSessionIndexItem(Session* session) const
{
    QJsonObject item;
    item.insert(QStringLiteral("id"), session->id());
    item.insert(QStringLiteral("type"), session->type() == Session::SessionType::Group ? QStringLiteral("group") : QStringLiteral("private"));
    item.insert(QStringLiteral("title"), session->title());
    item.insert(QStringLiteral("participants"), m_persistence->stringListToJson(session->participantIds()));
    item.insert(QStringLiteral("lastActiveAt"), session->lastActiveAt().toString(Qt::ISODateWithMs));
    item.insert(QStringLiteral("messageCount"), session->messageCount());
    return item;
}

QHash<QString, int> ChatStateRepository::saveState(
    const QString& currentSessionId,
    const QHash<QString, int>& previousMessageCounts,
    const std::function<const SessionPipeline*(const QString&)>& pipelineLookup) const
{
    QHash<QString, int> savedCounts = previousMessageCounts;
    if (!isReady())
        return savedCounts;

    saveDirectoryStructure();
    saveManifestAndAppState(currentSessionId);

    const QStringList activeAgentIds = saveIdentities();
    cleanupStaleAgentDirs(activeAgentIds);

    const QString sessionDataRoot = QDir(m_persistence->sessionsDirPath()).filePath(QStringLiteral("data"));
    QDir().mkpath(sessionDataRoot);

    QStringList activeSessionIds;
    QJsonArray indexSessions;
    const QList<Session*> sessions = m_sessionManager->allSessions();
    for (Session* session : sessions) {
        if (!session)
            continue;
        activeSessionIds.append(session->id());

        QJsonObject metaObj;
        metaObj.insert(QStringLiteral("id"), session->id());
        metaObj.insert(QStringLiteral("type"), session->type() == Session::SessionType::Group ? QStringLiteral("group") : QStringLiteral("private"));
        metaObj.insert(QStringLiteral("title"), session->title());
        metaObj.insert(QStringLiteral("ownerId"), session->ownerId());
        metaObj.insert(QStringLiteral("participants"), m_persistence->stringListToJson(session->participantIds()));
        metaObj.insert(QStringLiteral("createdAt"), session->createdAt().toString(Qt::ISODateWithMs));
        metaObj.insert(QStringLiteral("lastActiveAt"), session->lastActiveAt().toString(Qt::ISODateWithMs));
        metaObj.insert(QStringLiteral("messageCount"), session->messageCount());
        m_persistence->writeJsonObject(m_persistence->sessionMetaPath(session->id()), metaObj);

        const QString sid = session->id();
        const QString messagesPath = m_persistence->sessionMessagesPath(sid);
        const int currentMessageCount = session->messageCount();
        const bool needsMessageRewrite = !QFileInfo::exists(messagesPath) || savedCounts.value(sid, -1) != currentMessageCount;
        if (needsMessageRewrite) {
            QJsonArray messagesArr;
            const QList<Message> messages = session->allMessages();
            for (const Message& msg : messages)
                messagesArr.append(m_persistence->messageToJson(msg));
            m_persistence->writeJsonLines(messagesPath, messagesArr);
            savedCounts.insert(sid, currentMessageCount);
        }

        QFile::remove(QDir(m_persistence->sessionDataDirPath(sid)).filePath(QStringLiteral("io_history.json")));

        const SessionPipeline* pipeline = pipelineLookup ? pipelineLookup(session->id()) : nullptr;
        const QJsonObject pendingObj = serializePendingTurns(pipeline);
        if (!pendingObj.isEmpty()) {
            m_persistence->writeJsonObject(m_persistence->sessionPendingTurnsPath(session->id()), pendingObj);
        } else {
            QFile::remove(m_persistence->sessionPendingTurnsPath(session->id()));
        }

        indexSessions.append(buildSessionIndexItem(session));
    }

    activeSessionIds.removeDuplicates();
    QDir sessionDataDir(sessionDataRoot);
    const QStringList persistedSessionDirs = sessionDataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& dirName : persistedSessionDirs) {
        if (!activeSessionIds.contains(dirName))
            QDir(sessionDataDir.filePath(dirName)).removeRecursively();
    }

    for (auto it = savedCounts.begin(); it != savedCounts.end();) {
        if (activeSessionIds.contains(it.key()))
            ++it;
        else
            it = savedCounts.erase(it);
    }

    QJsonObject indexRoot;
    indexRoot.insert(QStringLiteral("version"), 1);
    indexRoot.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    indexRoot.insert(QStringLiteral("sessions"), indexSessions);
    m_persistence->writeJsonObject(m_persistence->sessionsIndexPath(), indexRoot);

    return savedCounts;
}

ChatStateRepository::LoadResult ChatStateRepository::loadState(const LLMConfig& defaultConfig) const
{
    LoadResult result;
    if (!isReady())
        return result;

    bool manifestOk = false;
    const QJsonObject manifest = m_persistence->readJsonObject(m_persistence->manifestPath(), &manifestOk);
    if (!manifestOk)
        return result;

    const int schemaVersion = manifest.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion != 3) {
        qWarning() << "[ChatStateRepository] 不支持的数据目录版本，已跳过加载。expected=3 actual="
                   << schemaVersion;
        return result;
    }

    const QJsonObject appState = m_persistence->readJsonObject(m_persistence->appStatePath());

    for (Session* session : m_sessionManager->allSessions())
        m_sessionManager->removeSession(session->id());

    const QList<Identity*> existingAgents = m_identityManager->allAgents();
    for (Identity* agent : existingAgents) {
        if (agent)
            m_identityManager->removeAgent(agent->id());
    }

    bool userOk = false;
    const QJsonObject userObj = m_persistence->readJsonObject(m_persistence->userIdentityPath(), &userOk);
    const QString persistedUserId = userOk
        ? userObj.value(QStringLiteral("id")).toString().trimmed()
        : QString();
    Identity* userIdentity = m_identityManager->userIdentity(persistedUserId);
    const QString userId = userIdentity ? userIdentity->id() : QString();
    if (userIdentity)
        userIdentity->setName(QStringLiteral("Me"));

    QHash<QString, QString> identityIdMap;
    if (!userId.isEmpty())
        identityIdMap.insert(userId, userId);
    if (userOk && userIdentity) {
        const QString oldUserId = userObj.value(QStringLiteral("id")).toString().trimmed();
        if (!oldUserId.isEmpty())
            identityIdMap.insert(oldUserId, userId);

        const QString userName = userObj.value(QStringLiteral("name")).toString().trimmed();
        if (!userName.isEmpty())
            userIdentity->setName(userName);

        const QString userAvatar = userObj.value(QStringLiteral("avatar")).toString().trimmed();
        if (!userAvatar.isEmpty())
            userIdentity->setAvatar(userAvatar);
    }

    QDir agentsDir(m_persistence->agentsDirPath());
    const QStringList agentDirs = agentsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& agentDirName : agentDirs) {
        bool profileOk = false;
        const QJsonObject item = m_persistence->readJsonObject(m_persistence->agentProfilePath(agentDirName), &profileOk);
        if (!profileOk)
            continue;
        if (item.value(QStringLiteral("type")).toString().trimmed() != QLatin1String("agent"))
            continue;

        const QString oldAgentId = item.value(QStringLiteral("id")).toString().trimmed();
        const QString agentName = item.value(QStringLiteral("name")).toString().trimmed();
        const QString avatar = item.value(QStringLiteral("avatar")).toString().trimmed();

        IdentityProfile* profile = m_persistence->identityProfileFromJson(
            item.value(QStringLiteral("profile")).toObject(), defaultConfig);
        QStringList allowedTools = profile->allowedTools();
        const bool delegateEnabled = profile->delegateEnabled();
        if (allowedTools.isEmpty()) {
            allowedTools = collectToolNames();
        } else {
            // 阶段 6 启动：为既有 Agent 平滑补齐记忆检索工具，避免旧配置漏掉新工具。
            if (!allowedTools.contains(QStringLiteral("memory_search")))
                allowedTools.append(QStringLiteral("memory_search"));
            if (!allowedTools.contains(QStringLiteral("memory_reindex")))
                allowedTools.append(QStringLiteral("memory_reindex"));
            if (!allowedTools.contains(QStringLiteral("session_search")))
                allowedTools.append(QStringLiteral("session_search"));
        }
        const QStringList delegateTools = {
            QStringLiteral("delegate_task"),
            QStringLiteral("delegate_status"),
            QStringLiteral("delegate_cancel"),
            QStringLiteral("delegate_list_active")
        };
        if (delegateEnabled) {
            for (const QString& toolName : delegateTools) {
                if (!allowedTools.contains(toolName))
                    allowedTools.append(toolName);
            }
        } else {
            for (const QString& toolName : delegateTools)
                allowedTools.removeAll(toolName);
        }
        profile->setAllowedTools(allowedTools);

        const QString preferredAgentId = !oldAgentId.isEmpty() ? oldAgentId : agentDirName.trimmed();
        Identity* agent = m_identityManager->createAgent(
            agentName.isEmpty() ? QStringLiteral("TM Agent") : agentName,
            profile,
            preferredAgentId);
        if (!avatar.isEmpty())
            agent->setAvatar(avatar);
        if (!oldAgentId.isEmpty())
            identityIdMap.insert(oldAgentId, agent->id());
        if (!agentDirName.trimmed().isEmpty())
            identityIdMap.insert(agentDirName.trimmed(), agent->id());
    }

    bool sessionsIndexOk = false;
    const QJsonObject sessionsIndex = m_persistence->readJsonObject(m_persistence->sessionsIndexPath(), &sessionsIndexOk);
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
        const QJsonObject metaObj = m_persistence->readJsonObject(m_persistence->sessionMetaPath(sessionId), &metaOk);
        const QJsonObject sessionObj = metaOk ? metaObj : indexItem;

        const QString type = sessionObj.value(QStringLiteral("type")).toString().trimmed();
        if (type != QLatin1String("private") && type != QLatin1String("group")) {
            qWarning() << "[ChatStateRepository] 跳过无效会话项：未知 type =" << type;
            continue;
        }
        const QString title = sessionObj.value(QStringLiteral("title")).toString();

        QString ownerId = remapIdentityId(sessionObj.value(QStringLiteral("ownerId")).toString(), identityIdMap);
        QStringList participants = m_persistence->stringListFromJson(sessionObj.value(QStringLiteral("participants")));
        for (QString& pid : participants)
            pid = remapIdentityId(pid, identityIdMap);
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
            session = m_sessionManager->createGroupSession(ownerId, participants, title);
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
                profile->setAllowedTools(collectToolNames());
                Identity* agent = m_identityManager->createAgent(
                    title.isEmpty() ? QStringLiteral("TM Agent") : title,
                    profile);
                pB = agent->id();
            }

            session = m_sessionManager->createPrivateSession(pA, pB);
            session->setTitle(title);
        }

        if (!session)
            continue;

        const QString generatedId = session->id();
        session->setId(sessionId);
        m_sessionManager->replaceSessionId(generatedId, sessionId);
        if (!title.isEmpty())
            session->setTitle(title);

        bool messagesOk = false;
        const QJsonArray messagesArr = m_persistence->readJsonLines(m_persistence->sessionMessagesPath(sessionId), &messagesOk);
        if (!messagesOk)
            qWarning() << "[ChatStateRepository] 会话消息读取失败，sessionId=" << sessionId;

        QString userParticipantId;
        QString agentParticipantId;
        if (session->type() == Session::SessionType::Private) {
            const QStringList pids = session->participantIds();
            for (const QString& pid : pids) {
                Identity* identity = m_identityManager->findById(pid);
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

        for (const QJsonValue& mv : messagesArr) {
            Message msg = m_persistence->messageFromJson(mv.toObject(), session->id());
            const QString rawSenderId = msg.senderId;
            msg.sessionId = session->id();
            msg.senderId = remapIdentityId(msg.senderId, identityIdMap);

            if (!msg.senderId.isEmpty()
                && msg.senderId != QLatin1String("system")
                && !m_identityManager->findById(msg.senderId)
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
                mention = remapIdentityId(mention, identityIdMap);
            msg.mentions.removeAll(QString());
            msg.mentions.removeDuplicates();

            if (msg.isValid())
                session->addMessage(msg);
        }
        result.savedMessageCounts.insert(session->id(), session->messageCount());

        bool pendingOk = false;
        const QJsonObject pendingObj = m_persistence->readJsonObject(m_persistence->sessionPendingTurnsPath(sessionId), &pendingOk);
        const QJsonArray pendingTurns = pendingOk ? pendingObj.value(QStringLiteral("turns")).toArray() : QJsonArray();
        if (!pendingTurns.isEmpty()) {
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

            if (!restoredTurns.isEmpty())
                result.pendingTurnsBySession.insert(session->id(), restoredTurns);
        }
    }

    QString savedSessionId = appState.value(QStringLiteral("currentSessionId")).toString().trimmed();
    if (!savedSessionId.isEmpty() && m_sessionManager->findById(savedSessionId)) {
        result.currentSessionId = savedSessionId;
    } else {
        const QList<Session*> sessions = m_sessionManager->allSessions();
        result.currentSessionId = sessions.isEmpty() ? QString() : sessions.first()->id();
    }

    result.success = true;
    return result;
}
