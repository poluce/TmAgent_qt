#ifndef CONVERSATIONCONTEXTTYPES_H
#define CONVERSATIONCONTEXTTYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace ConversationContext {

static constexpr int kSchemaVersion = 1;

struct TaskContextSnapshot {
    QJsonObject payload;

    QJsonObject toJson() const { return payload; }
    static TaskContextSnapshot fromJson(const QJsonObject& obj)
    {
        TaskContextSnapshot snapshot;
        snapshot.payload = obj;
        return snapshot;
    }
};

struct ContextCompressionCheckpoint {
    QJsonObject payload;

    QJsonObject toJson() const { return payload; }
    static ContextCompressionCheckpoint fromJson(const QJsonObject& obj)
    {
        ContextCompressionCheckpoint checkpoint;
        checkpoint.payload = obj;
        return checkpoint;
    }
};

struct ResumePacket {
    QJsonObject payload;

    QJsonObject toJson() const { return payload; }
    static ResumePacket fromJson(const QJsonObject& obj)
    {
        ResumePacket packet;
        packet.payload = obj;
        return packet;
    }
};

QJsonObject buildBaseEnvelope(const QString& sessionId, const QString& kind, const QString& timestampUtc);
QStringList jsonArrayToStringList(const QJsonValue& value);
QJsonArray stringListToJsonArray(const QStringList& values);

} // namespace ConversationContext

#endif // CONVERSATIONCONTEXTTYPES_H
