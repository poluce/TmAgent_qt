#include "LogSummarizer.h"
#include "LogFieldExtractor.h"

#include <QJsonDocument>

namespace LogSummarizer {

QString summarizeEvent(const QJsonObject& obj)
{
    const QString type = LogFields::extractEventType(obj, true);
    const QString toolName = LogFields::extractToolName(obj, true);
    const QString error = LogFields::stringField(obj, QStringLiteral("error"));
    const QString summary = LogFields::stringField(obj, QStringLiteral("summary"));

    QString text;
    if (!type.isEmpty())
        text += type;
    if (!toolName.isEmpty()) {
        if (!text.isEmpty())
            text += QStringLiteral(" ");
        text += QStringLiteral("tool=%1").arg(toolName);
    }
    if (!summary.isEmpty()) {
        if (!text.isEmpty())
            text += QStringLiteral(" ");
        text += summary;
    } else {
        const QString toolEventStatus = LogFields::valueAtPath(obj, QStringList()
                                                                         << QStringLiteral("toolEvent")
                                                                         << QStringLiteral("status"));
        if (!toolEventStatus.isEmpty()) {
            if (!text.isEmpty())
                text += QStringLiteral(" ");
            text += QStringLiteral("status=%1").arg(toolEventStatus);
        }
    }
    if (!error.isEmpty()) {
        if (!text.isEmpty())
            text += QStringLiteral(" ");
        text += QStringLiteral("error=%1").arg(error);
    }
    if (text.isEmpty())
        text = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    return clip(text.simplified(), 220);
}

QString summarizeMessage(const QJsonObject& obj)
{
    const QJsonObject contentObj = obj.value(QStringLiteral("content")).toObject();
    const QJsonObject payloadObj = contentObj.value(QStringLiteral("payload")).toObject();
    const QString type = LogFields::stringField(contentObj, QStringLiteral("type")).toLower();
    const QString sender = LogFields::stringField(obj, QStringLiteral("senderId"));
    const QString text = LogFields::stringField(contentObj, QStringLiteral("text"));
    const QString toolName = LogFields::stringField(payloadObj, QStringLiteral("tool_name"));

    QString summary = QStringLiteral("sender=%1 type=%2")
                          .arg(sender.isEmpty() ? QStringLiteral("(unknown)") : sender,
                               type.isEmpty() ? QStringLiteral("(unknown)") : type);
    if (!toolName.isEmpty())
        summary += QStringLiteral(" tool=%1").arg(toolName);

    if (type == QLatin1String("text") || type == QLatin1String("system")) {
        if (!text.isEmpty())
            summary += QStringLiteral(" %1").arg(text);
    } else if (type == QLatin1String("tool_result")) {
        QString resultText = text;
        if (resultText.isEmpty())
            resultText = LogFields::stringField(payloadObj, QStringLiteral("raw_result"));
        if (!resultText.isEmpty())
            summary += QStringLiteral(" %1").arg(resultText);
    } else if (!text.isEmpty()) {
        summary += QStringLiteral(" %1").arg(text);
    }
    return clip(summary.simplified(), 220);
}

QString clip(const QString& text, int maxChars)
{
    const QString simplified = text.simplified();
    if (maxChars <= 0 || simplified.size() <= maxChars)
        return simplified;
    return simplified.left(maxChars) + QStringLiteral("...");
}

} // namespace LogSummarizer
