#include "SessionManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

SessionManager* SessionManager::instance()
{
    static SessionManager s_instance;
    return &s_instance;
}

SessionManager::SessionManager(QObject* parent)
    : QObject(parent)
{
}

Session* SessionManager::createPrivateSession(const QString& participantA,
                                              const QString& participantB)
{
    auto* session = Session::createPrivate(participantA, participantB, this);
    m_sessions.insert(session->id(), session);
    m_sessionOrder.append(session->id());
    emit sessionCreated(session);
    return session;
}

Session* SessionManager::createGroupSession(const QString& ownerId,
                                            const QStringList& participantIds,
                                            const QString& title)
{
    auto* session = Session::createGroup(ownerId, participantIds, title, this);
    m_sessions.insert(session->id(), session);
    m_sessionOrder.append(session->id());
    emit sessionCreated(session);
    return session;
}

Session* SessionManager::findById(const QString& id) const
{
    return m_sessions.value(id, nullptr);
}

QList<Session*> SessionManager::sessionsForIdentity(const QString& identityId) const
{
    QList<Session*> result;
    for (const QString& id : m_sessionOrder) {
        Session* session = m_sessions.value(id, nullptr);
        if (session && session->hasParticipant(identityId))
            result.append(session);
    }
    return result;
}

QList<Session*> SessionManager::allSessions() const
{
    QList<Session*> result;
    for (const QString& id : m_sessionOrder) {
        if (Session* s = m_sessions.value(id, nullptr))
            result.append(s);
    }
    return result;
}

bool SessionManager::removeSession(const QString& id)
{
    Session* session = m_sessions.take(id);
    if (!session)
        return false;
    m_sessionOrder.removeAll(id);
    emit sessionRemoved(id);
    session->deleteLater();
    return true;
}

void SessionManager::postMessage(const QString& sessionId, const Message& msg)
{
    Session* session = m_sessions.value(sessionId, nullptr);
    if (!session)
        return;
    session->addMessage(msg);
    emit messagePosted(sessionId, msg);
}

int SessionManager::sessionCount() const
{
    return m_sessionOrder.size();
}

Session* SessionManager::sessionAt(int index) const
{
    if (index < 0 || index >= m_sessionOrder.size())
        return nullptr;
    return m_sessions.value(m_sessionOrder.at(index), nullptr);
}

int SessionManager::indexOf(const QString& sessionId) const
{
    return m_sessionOrder.indexOf(sessionId);
}

bool SessionManager::replaceSessionId(const QString& oldId, const QString& newId)
{
    if (oldId.isEmpty() || newId.isEmpty())
        return false;
    if (oldId == newId)
        return true;

    Session* session = m_sessions.take(oldId);
    if (!session)
        return false;
    m_sessions.insert(newId, session);

    for (int i = 0; i < m_sessionOrder.size(); ++i) {
        if (m_sessionOrder.at(i) == oldId) {
            m_sessionOrder[i] = newId;
            break;
        }
    }
    return true;
}

void SessionManager::saveAllToDisk(const QString& filePath)
{
    QJsonObject root;
    QJsonArray arr;
    for (const QString& id : m_sessionOrder) {
        Session* session = m_sessions.value(id, nullptr);
        if (!session)
            continue;
        QJsonObject s;
        s.insert(QStringLiteral("uuid"), session->id());
        s.insert(QStringLiteral("title"), session->title());
        s.insert(QStringLiteral("type"), session->type() == Session::SessionType::Private
                     ? QStringLiteral("private") : QStringLiteral("group"));
        s.insert(QStringLiteral("ownerId"), session->ownerId());

        QJsonArray participants;
        for (const QString& pid : session->participantIds())
            participants.append(pid);
        s.insert(QStringLiteral("participants"), participants);

        s.insert(QStringLiteral("history"), session->llmHistory());
        s.insert(QStringLiteral("io_history"), session->ioHistory());
        arr.append(s);
    }
    root.insert(QStringLiteral("sessions"), arr);

    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QFile f(filePath);
    if (f.open(QFile::WriteOnly | QFile::Text))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool SessionManager::loadAllFromDisk(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QFile::ReadOnly | QFile::Text))
        return false;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    // 清理现有数据
    for (Session* session : m_sessions)
        session->deleteLater();
    m_sessions.clear();
    m_sessionOrder.clear();

    QJsonObject root = doc.object();
    QJsonArray arr = root[QStringLiteral("sessions")].toArray();
    for (const QJsonValue& v : arr) {
        QJsonObject s = v.toObject();
        QString uuid = s[QStringLiteral("uuid")].toString().trimmed();
        if (uuid.isEmpty())
            uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);

        QString title = s[QStringLiteral("title")].toString();
        QString ownerId = s[QStringLiteral("ownerId")].toString();
        QJsonArray participantsArr = s[QStringLiteral("participants")].toArray();
        QStringList participants;
        for (const QJsonValue& pv : participantsArr)
            participants.append(pv.toString());

        QString typeStr = s[QStringLiteral("type")].toString();
        Session* session;
        if (typeStr == QStringLiteral("group")) {
            session = Session::createGroup(ownerId, participants, title, this);
        } else {
            QString pA = participants.value(0);
            QString pB = participants.value(1);
            session = Session::createPrivate(pA, pB, this);
            session->setTitle(title);
        }

        // 覆盖自动生成的 UUID，使用持久化的 UUID
        const QString oldId = session->id();
        session->setId(uuid);
        replaceSessionId(oldId, uuid);

        // 加载历史
        session->setLlmHistory(s[QStringLiteral("history")].toArray());
        session->setIoHistory(s[QStringLiteral("io_history")].toArray());

        m_sessions.insert(session->id(), session);
        m_sessionOrder.append(session->id());
    }
    return true;
}
