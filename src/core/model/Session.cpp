#include "Session.h"

Session::Session(SessionType type, QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_type(type)
    , m_createdAt(QDateTime::currentDateTime())
    , m_lastActiveAt(QDateTime::currentDateTime())
{
}

Session* Session::createPrivate(const QString& participantA, const QString& participantB, QObject* parent)
{
    auto* session = new Session(SessionType::Private, parent);
    session->m_participantIds << participantA << participantB;
    session->m_ownerId = participantA;
    return session;
}

Session* Session::createGroup(const QString& ownerId, const QStringList& participantIds, const QString& title, QObject* parent)
{
    auto* session = new Session(SessionType::Group, parent);
    session->m_ownerId = ownerId;
    session->m_participantIds = participantIds;
    if (!participantIds.contains(ownerId))
        session->m_participantIds.prepend(ownerId);
    session->m_title = title;
    return session;
}

QString Session::id() const { return m_id; }
void Session::setId(const QString& id) { m_id = id; }
Session::SessionType Session::type() const { return m_type; }
QString Session::ownerId() const { return m_ownerId; }

QStringList Session::participantIds() const { return m_participantIds; }

bool Session::hasParticipant(const QString& identityId) const
{
    return m_participantIds.contains(identityId);
}

void Session::addParticipant(const QString& identityId)
{
    if (!m_participantIds.contains(identityId)) {
        m_participantIds.append(identityId);
        emit participantAdded(identityId);
    }
}

void Session::removeParticipant(const QString& identityId)
{
    if (m_participantIds.removeAll(identityId) > 0) {
        emit participantRemoved(identityId);
    }
}

void Session::addMessage(const Message& msg)
{
    Message stored = msg;
    if (stored.seq <= 0)
        stored.seq = static_cast<qint64>(m_messages.size()) + 1;
    m_messages.append(stored);
    m_lastActiveAt = QDateTime::currentDateTime();
    emit messageAdded(stored);
}

int Session::messageCount() const { return m_messages.size(); }

Message Session::messageAt(int index) const
{
    if (index >= 0 && index < m_messages.size())
        return m_messages.at(index);
    return Message();
}

QList<Message> Session::allMessages() const { return m_messages; }

Message Session::lastMessage() const
{
    return m_messages.isEmpty() ? Message() : m_messages.last();
}

void Session::clearMessages()
{
    m_messages.clear();
}

const Message* Session::findMessageById(const QString& messageId) const
{
    if (messageId.isEmpty())
        return nullptr;
    for (const Message& msg : m_messages) {
        if (msg.id == messageId)
            return &msg;
    }
    return nullptr;
}

QString Session::title() const { return m_title; }

void Session::setTitle(const QString& title)
{
    if (m_title != title) {
        m_title = title;
        emit titleChanged(title);
    }
}

QDateTime Session::createdAt() const { return m_createdAt; }
QDateTime Session::lastActiveAt() const { return m_lastActiveAt; }

Session::StreamState& Session::streamState() { return m_streamState; }
const Session::StreamState& Session::streamState() const { return m_streamState; }
bool Session::isStreaming() const { return m_streamState.isStreaming; }
