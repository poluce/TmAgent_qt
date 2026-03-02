#ifndef LOGDBSCANNER_H
#define LOGDBSCANNER_H

#include "LogQueryEngine.h"

namespace LogDbScanner {

QVector<LogQueryEngine::Hit> queryEvents(const LogQueryEngine::Query& query,
                                         LogQueryEngine::Result* result);
QVector<LogQueryEngine::Hit> queryMessages(const LogQueryEngine::Query& query,
                                           LogQueryEngine::Result* result);

} // namespace LogDbScanner

#endif // LOGDBSCANNER_H
