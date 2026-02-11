#include "SessionManager.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {
QString messageTypeToString(MessageContent::Type type)
{
    switch (type) {
    case MessageContent::Type::Text: return QStringLiteral("text");
    case MessageContent::Type::ToolCall: return QStringLiteral("tool_call");
    case MessageContent::Type::ToolResult: return QStringLiteral("tool_result");
    case MessageContent::Type::System: return QStringLiteral("system");
    case MessageContent::Type::File: return QStringLiteral("file");
    }
    return QStringLiteral("text");
}

MessageContent::Type messageTypeFromString(const QString& type)
{
    if (type == QLatin1String("tool_call")) return MessageContent::Type::ToolCall;
    if (type == QLatin1String("tool_result")) return MessageContent::Type::ToolResult;
    if (type == QLatin1String("system")) return MessageContent::Type::System;
    if (type == QLatin1String("file")) return MessageContent::Type::File;
    return MessageContent::Type::Text;
}

QString messageStatusToString(Message::Status status)
{
    switch (status) {
    case Message::Status::Pending: return QStringLiteral("pending");
    case Message::Status::Streaming: return QStringLiteral("streaming");
    case Message::Status::Completed: return QStringLiteral("completed");
    case Message::Status::Cancelled: return QStringLiteral("cancelled");
    case Message::Status::Interrupted: return QStringLiteral("interrupted");
    case Message::Status::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

Message::Status messageStatusFromString(const QString& status)
{
    if (status == QLatin1String("pending")) return Message::Status::Pending;
    if (status == QLatin1String("streaming")) return Message::Status::Streaming;
    if (status == QLatin1String("completed")) return Message::Status::Completed;
    if (status == QLatin1String("cancelled")) return Message::Status::Cancelled;
    if (status == QLatin1String("interrupted")) return Message::Status::Interrupted;
    if (status == QLatin1String("error")) return Message::Status::Error;
    return Message::Status::Completed;
}

QJsonObject messageToJson(const Message& msg)
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

Message messageFromJson(const QJsonObject& obj, const QString& fallbackSessionId)
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
    QJsonArray mentions = obj.value(QStringLiteral("mentions")).toArray();
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

} // namespace

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
    root.insert(QStringLiteral("schemaVersion"), 3);
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

        QJsonArray messages;
        const QList<Message> allMessages = session->allMessages();
        for (const Message& msg : allMessages)
            messages.append(messageToJson(msg));
        s.insert(QStringLiteral("messages"), messages);
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
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if (schemaVersion != 3) {
        qWarning() << "[SessionManager] 不支持的会话文件版本，已跳过加载。expected=3 actual="
                   << schemaVersion;
        return false;
    }

    if (!root.value(QStringLiteral("sessions")).isArray()) {
        qWarning() << "[SessionManager] 会话文件结构无效（缺少 sessions 数组），已跳过加载。";
        return false;
    }
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

        QJsonArray messagesArr = s[QStringLiteral("messages")].toArray();
        for (const QJsonValue& mv : messagesArr) {
            Message msg = messageFromJson(mv.toObject(), session->id());
            if (msg.isValid())
                session->addMessage(msg);
        }

        m_sessions.insert(session->id(), session);
        m_sessionOrder.append(session->id());
    }
    return true;
}
