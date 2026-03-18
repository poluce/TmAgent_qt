#include "core/service/include/ConversationContextTypes.h"

#include <QJsonValue>

namespace ConversationContext {

QJsonObject buildBaseEnvelope(const QString& sessionId, const QString& kind, const QString& timestampUtc)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), kSchemaVersion);
    obj.insert(QStringLiteral("session_id"), sessionId.trimmed());
    obj.insert(QStringLiteral("kind"), kind.trimmed());
    obj.insert(QStringLiteral("updated_at_utc"), timestampUtc.trimmed());
    return obj;
}

QStringList jsonArrayToStringList(const QJsonValue& value)
{
    QStringList result;
    const QJsonArray arr = value.toArray();
    for (const QJsonValue& item : arr) {
        const QString text = item.toString().simplified();
        if (!text.isEmpty())
            result.append(text);
    }
    return result;
}

QJsonArray stringListToJsonArray(const QStringList& values)
{
    QJsonArray arr;
    for (const QString& value : values) {
        const QString text = value.simplified();
        if (!text.isEmpty())
            arr.append(text);
    }
    return arr;
}

} // namespace ConversationContext
