#include "qss_utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QWidget>

namespace QssUtils {
QString resolveStyleSheetPath(const QString& fileNameOrPath)
{
    if (fileNameOrPath.startsWith(":/"))
        return fileNameOrPath;
    if (QFile::exists(fileNameOrPath))
        return QFileInfo(fileNameOrPath).absoluteFilePath();

    const QString fileName = QFileInfo(fileNameOrPath).fileName();
    if (fileName.isEmpty())
        return QString();

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("resources/styles/" + fileName),
        QDir(appDir).filePath("../resources/styles/" + fileName),
        QDir(QDir::currentPath()).filePath("resources/styles/" + fileName),
        QDir(QDir::currentPath()).filePath("../resources/styles/" + fileName)
    };

    for (const QString& path : candidates) {
        if (QFile::exists(path))
            return path;
    }
    return QString();
}

QString loadStyleSheetFile(const QString& fileNameOrPath)
{
    if (fileNameOrPath.trimmed().isEmpty())
        return QString();

    QString targetPath = resolveStyleSheetPath(fileNameOrPath);

    // 文件系统找不到时，尝试 qrc 资源
    if (targetPath.isEmpty()) {
        const QString fileName = QFileInfo(fileNameOrPath).fileName();
        if (fileName.isEmpty())
            return QString();
        targetPath = QStringLiteral(":/styles/%1").arg(fileName);
    }

    QFile file(targetPath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QString buildCombinedStyleSheet(const QString& specificFileNameOrPath, const QString& inlineStyle, const QString& globalFileName)
{
    QString combined = loadStyleSheetFile(globalFileName);

    // inlineStyle 优先于文件样式
    const QString extra = inlineStyle.trimmed().isEmpty()
        ? loadStyleSheetFile(specificFileNameOrPath)
        : inlineStyle;

    if (!extra.isEmpty()) {
        if (!combined.isEmpty())
            combined += '\n';
        combined += extra;
    }
    return combined;
}

bool applyStyleSheetFromFile(QWidget* target, const QString& fileNameOrPath, const QString& globalFileName)
{
    if (!target)
        return false;

    target->setStyleSheet(buildCombinedStyleSheet(fileNameOrPath, QString(), globalFileName));
    return true;
}
} // namespace QssUtils
