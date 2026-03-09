#ifndef LOGSCANNER_H
#define LOGSCANNER_H

#include "LogQueryEngine.h"

#include <QDateTime>
#include <QString>
#include <QVector>

namespace LogScanner {

bool sourceMatches(const QString& source, bool isEvent);
bool withinTimeRange(const QDateTime& timestamp, const LogQueryEngine::Query& query);
bool hitMatches(const LogQueryEngine::Hit& hit, const LogQueryEngine::Query& query, const QString& rawCompactLower);

QVector<LogQueryEngine::Hit> scanEventFile(const QString& filePath, const LogQueryEngine::Query& query, LogQueryEngine::Result* result);
QVector<LogQueryEngine::Hit> scanSessionFile(const QString& sessionId, const QString& filePath, const LogQueryEngine::Query& query, LogQueryEngine::Result* result);

qint64 toMs(const QDateTime& dt);

} // namespace LogScanner

#endif // LOGSCANNER_H
