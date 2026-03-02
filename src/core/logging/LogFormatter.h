#ifndef LOGFORMATTER_H
#define LOGFORMATTER_H

#include "LogQueryEngine.h"

#include <QJsonObject>
#include <QString>

namespace LogFormatter {

LogQueryEngine::OutputFormat parseFormat(const QString& raw);
QString formatResult(const LogQueryEngine::Result& result);
QJsonObject resultToJson(const LogQueryEngine::Result& result);

} // namespace LogFormatter

#endif // LOGFORMATTER_H
