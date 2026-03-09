#ifndef LOGDBUTILS_H
#define LOGDBUTILS_H

#include <QSqlDatabase>
#include <QString>

namespace LogDbUtils {

QString resolveDataRoot(const QString& dataRootPath);
QString databasePathFromRoot(const QString& dataRootPath);
QSqlDatabase openConnection(const QString& dataRootPath, QString* error = nullptr);

} // namespace LogDbUtils

#endif // LOGDBUTILS_H
