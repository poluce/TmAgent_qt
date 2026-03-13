#ifndef QSS_UTILS_H
#define QSS_UTILS_H

#include <QString>

class QWidget;

namespace QssUtils {
QString resolveStyleSheetPath(const QString& fileNameOrPath);
QString loadStyleSheetFile(const QString& fileNameOrPath);
QString buildCombinedStyleSheet(const QString& specificFileNameOrPath, const QString& inlineStyle = QString(), const QString& globalFileName = QString("global.qss"));
bool applyStyleSheetFromFile(QWidget* target, const QString& fileNameOrPath, const QString& globalFileName = QString("global.qss"));
} // namespace QssUtils

#endif // QSS_UTILS_H
