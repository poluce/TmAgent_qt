#include "LogFieldExtractor.h"

#include <QJsonDocument>

namespace LogFields {

QString stringField(const QJsonObject& obj, const QString& key)
{
    return obj.value(key).toString().trimmed();
}

QString stringFieldAny(const QJsonObject& obj, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString value = stringField(obj, key);
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

QDateTime parseTimestampFromObject(const QJsonObject& obj)
{
    const QString ts = stringFieldAny(obj, QStringList()
                                               << QStringLiteral("timestamp")
                                               << QStringLiteral("time")
                                               << QStringLiteral("createdAt")
                                               << QStringLiteral("created_at"));
    if (ts.isEmpty())
        return QDateTime();

    QDateTime dt = QDateTime::fromString(ts, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(ts, Qt::ISODate);
    if (dt.isValid() && dt.timeSpec() == Qt::LocalTime)
        dt = dt.toUTC();
    return dt;
}

QString extractEventType(const QJsonObject& obj, bool isEventSource)
{
    if (isEventSource) {
        QString type = stringField(obj, QStringLiteral("type"));
        if (!type.isEmpty())
            return type;
        type = stringField(obj.value(QStringLiteral("event")).toObject(), QStringLiteral("type"));
        return type;
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    return stringField(contentObj, QStringLiteral("type"));
}

QString extractToolName(const QJsonObject& obj, bool isEventSource)
{
    if (isEventSource) {
        QString value = stringFieldAny(obj, QStringList()
                                                << QStringLiteral("tool_name")
                                                << QStringLiteral("toolName"));
        if (!value.isEmpty())
            return value;
        const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
        value = stringFieldAny(toolEventObj, QStringList()
                                               << QStringLiteral("toolName")
                                               << QStringLiteral("tool_name"));
        if (!value.isEmpty())
            return value;
        return stringField(toolEventObj.value(QStringLiteral("data")).toObject(), QStringLiteral("tool_name"));
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    QString value = stringField(payloadObj, QStringLiteral("tool_name"));
    if (!value.isEmpty())
        return value;
    value = stringField(payloadObj.value(QStringLiteral("event_data")).toObject(), QStringLiteral("tool_name"));
    return value;
}

QString extractToolCallId(const QJsonObject& obj, bool isEventSource)
{
    if (isEventSource) {
        QString value = stringField(obj, QStringLiteral("tool_call_id"));
        if (!value.isEmpty())
            return value;
        value = stringField(obj, QStringLiteral("toolId"));
        if (!value.isEmpty())
            return value;
        const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
        value = stringField(toolEventObj, QStringLiteral("toolId"));
        if (!value.isEmpty())
            return value;
        return stringField(toolEventObj.value(QStringLiteral("data")).toObject(), QStringLiteral("tool_call_id"));
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    QString value = stringField(payloadObj, QStringLiteral("tool_call_id"));
    if (!value.isEmpty())
        return value;
    return stringField(payloadObj, QStringLiteral("id"));
}

QString extractRequestId(const QJsonObject& obj, bool isEventSource)
{
    QString value = stringField(obj, QStringLiteral("request_id"));
    if (!value.isEmpty())
        return value;

    if (isEventSource) {
        const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
        value = stringField(toolEventObj.value(QStringLiteral("data")).toObject(), QStringLiteral("child_request_id"));
        if (!value.isEmpty())
            return value;
        return stringField(obj, QStringLiteral("child_request_id"));
    }

    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    value = stringField(payloadObj, QStringLiteral("child_request_id"));
    if (!value.isEmpty())
        return value;
    return stringField(payloadObj.value(QStringLiteral("event_data")).toObject(), QStringLiteral("child_request_id"));
}

QString extractActorId(const QJsonObject& obj, bool isEventSource)
{
    if (isEventSource) {
        QString value = stringFieldAny(obj, QStringList()
                                                << QStringLiteral("actorIdentityId")
                                                << QStringLiteral("actor_id"));
        if (!value.isEmpty())
            return value;
        const QJsonObject toolEventData = obj.value(QStringLiteral("toolEvent")).toObject().value(QStringLiteral("data")).toObject();
        return stringField(toolEventData, QStringLiteral("_agent_id"));
    }
    return firstNonEmpty(QStringList()
                         << stringField(obj, QStringLiteral("senderId"))
                         << stringField(obj, QStringLiteral("sender_id")));
}

bool extractSuccess(const QJsonObject& obj, bool isEventSource, bool* known, bool* value)
{
    if (known)
        *known = false;
    if (value)
        *value = false;

    auto assign = [known, value](bool v) {
        if (known)
            *known = true;
        if (value)
            *value = v;
    };

    if (isEventSource) {
        if (obj.contains(QStringLiteral("success")) && obj.value(QStringLiteral("success")).isBool()) {
            assign(obj.value(QStringLiteral("success")).toBool());
            return true;
        }
        const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
        if (toolEventObj.contains(QStringLiteral("success")) && toolEventObj.value(QStringLiteral("success")).isBool()) {
            assign(toolEventObj.value(QStringLiteral("success")).toBool());
            return true;
        }
        return false;
    }

    const QJsonObject payloadObj = obj.value(QStringLiteral("content")).toObject().value(QStringLiteral("payload")).toObject();
    if (payloadObj.contains(QStringLiteral("success")) && payloadObj.value(QStringLiteral("success")).isBool()) {
        assign(payloadObj.value(QStringLiteral("success")).toBool());
        return true;
    }
    return false;
}

QString extractLevel(const QJsonObject& obj, bool isEventSource)
{
    // 先尝试从显式字段提取
    const QString level = stringFieldAny(obj, QStringList()
                                                  << QStringLiteral("level")
                                                  << QStringLiteral("severity")
                                                  << QStringLiteral("log_level"));
    if (!level.isEmpty())
        return level.toLower();

    // 根据 eventType 推断
    const QString eventType = extractEventType(obj, isEventSource).toLower();
    if (eventType.contains(QStringLiteral("error")) || eventType.contains(QStringLiteral("failed")))
        return QStringLiteral("error");
    if (eventType.contains(QStringLiteral("warning")))
        return QStringLiteral("warning");
    return QStringLiteral("info");
}

QString valueAtPath(const QJsonObject& obj, const QStringList& path)
{
    if (path.isEmpty())
        return QString();

    QJsonValue current(obj);
    for (const QString& segment : path) {
        if (!current.isObject())
            return QString();
        current = current.toObject().value(segment);
    }
    if (current.isString())
        return current.toString().trimmed();
    if (current.isDouble())
        return QString::number(current.toDouble());
    if (current.isBool())
        return current.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return QString();
}

QString firstNonEmpty(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        const QString trimmed = candidate.trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return QString();
}

} // namespace LogFields
