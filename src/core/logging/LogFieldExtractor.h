#ifndef LOGFIELDEXTRACTOR_H
#define LOGFIELDEXTRACTOR_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace LogFields {

// --- 辅助函数 ---
QString stringField(const QJsonObject& obj, const QString& key);
QString stringFieldAny(const QJsonObject& obj, const QStringList& keys);
QDateTime parseTimestampFromObject(const QJsonObject& obj);

// --- 字段提取 ---
QString extractEventType(const QJsonObject& obj, bool isEventSource);
QString extractToolName(const QJsonObject& obj, bool isEventSource);
QString extractToolCallId(const QJsonObject& obj, bool isEventSource);
QString extractRequestId(const QJsonObject& obj, bool isEventSource);
QString extractActorId(const QJsonObject& obj, bool isEventSource);
bool extractSuccess(const QJsonObject& obj, bool isEventSource, bool* known, bool* value);
QString extractLevel(const QJsonObject& obj, bool isEventSource);

QString valueAtPath(const QJsonObject& obj, const QStringList& path);
QString firstNonEmpty(const QStringList& candidates);

} // namespace LogFields

#endif // LOGFIELDEXTRACTOR_H
