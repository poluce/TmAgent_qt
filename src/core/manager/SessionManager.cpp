#include "SessionManager.h"

SessionManager* SessionManager::instance()
{
    static SessionManager s_instance;
    return &s_instance;
}

SessionManager::SessionManager(QObject* parent)
    : QObject(parent)
{
}

Session* SessionManager::createPrivateSession(const QString& participantA, const QString& participantB)
{
    auto* session = Session::createPrivate(participantA, participantB, this);
    m_sessions.insert(session->id(), session);
    m_sessionOrder.append(session->id());
    emit sessionCreated(session);
    return session;
}

Session* SessionManager::createGroupSession(const QString& ownerId, const QStringList& participantIds, const QString& title)
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
