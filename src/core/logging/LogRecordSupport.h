#ifndef LOGRECORDSUPPORT_H
#define LOGRECORDSUPPORT_H

#include "LogQueryEngine.h"

#include <QDateTime>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace LogRecordSupport {

QString resolveDataRoot(const QString& dataRootPath);
QString databasePathFromRoot(const QString& dataRootPath);
QSqlDatabase openConnection(const QString& dataRootPath, QString* error = nullptr);

LogQueryEngine::OutputFormat parseFormat(const QString& raw);
QString formatResult(const LogQueryEngine::Result& result);
QJsonObject resultToJson(const LogQueryEngine::Result& result);

bool sourceMatches(const QString& source, bool isEvent);
qint64 toMs(const QDateTime& dt);
bool withinTimeRange(const QDateTime& timestamp, const LogQueryEngine::Query& query);
bool hitMatches(const LogQueryEngine::Hit& hit,
                const LogQueryEngine::Query& query,
                const QString& rawCompactLower);

LogQueryEngine::Hit buildEventHit(const QJsonObject& obj,
                                  const QString& filePath,
                                  int lineNo,
                                  bool includeRaw = false);
LogQueryEngine::Hit buildMessageHit(const QJsonObject& obj,
                                    const QString& sessionIdFallback,
                                    const QString& filePath,
                                    int lineNo,
                                    bool includeRaw = false);
bool eventMatchesFilter(const QJsonObject& obj,
                        const LogQueryEngine::Query& filter,
                        LogQueryEngine::Hit* outHit = nullptr);

QVector<LogQueryEngine::Hit> queryEvents(const LogQueryEngine::Query& query,
                                         LogQueryEngine::Result* result);
QVector<LogQueryEngine::Hit> queryMessages(const LogQueryEngine::Query& query,
                                           LogQueryEngine::Result* result);

QString clip(const QString& text, int maxChars);

} // namespace LogRecordSupport

#endif // LOGRECORDSUPPORT_H
