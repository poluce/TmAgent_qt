#include "MemoryDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

MemoryDocument::MemoryDocument(const QString& filePath)
    : m_filePath(filePath)
{
}

void MemoryDocument::setFilePath(const QString& filePath)
{
    m_filePath = filePath;
}

QString MemoryDocument::filePath() const
{
    return m_filePath;
}

bool MemoryDocument::exists() const
{
    return !m_filePath.trimmed().isEmpty() && QFileInfo::exists(m_filePath);
}

QString MemoryDocument::read(bool* ok) const
{
    if (ok)
        *ok = false;
    if (m_filePath.trimmed().isEmpty())
        return QString();

    QFile file(m_filePath);
    if (!file.exists()) {
        if (ok)
            *ok = true;
        return QString();
    }
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QString();

    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    if (ok)
        *ok = true;
    return content;
}

QString MemoryDocument::readTruncated(int maxChars, bool* ok) const
{
    const QString content = read(ok);
    if (maxChars <= 0 || content.size() <= maxChars)
        return content;
    return content.right(maxChars);
}

bool MemoryDocument::ensureParentDir(QString* error) const
{
    if (m_filePath.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("memory file path is empty");
        return false;
    }

    const QString parentPath = QFileInfo(m_filePath).absolutePath();
    if (QDir().mkpath(parentPath))
        return true;

    if (error)
        *error = QStringLiteral("failed to create parent dir: %1").arg(parentPath);
    return false;
}

bool MemoryDocument::writeAtomic(const QString& content, QString* error) const
{
    if (!ensureParentDir(error))
        return false;

    QSaveFile file(m_filePath);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        if (error)
            *error = QStringLiteral("failed to open file for write: %1").arg(m_filePath);
        return false;
    }
    if (file.write(content.toUtf8()) < 0) {
        if (error)
            *error = QStringLiteral("failed to write file: %1").arg(m_filePath);
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("failed to commit file: %1").arg(m_filePath);
        return false;
    }
    return true;
}

bool MemoryDocument::appendAtomic(const QString& entry, QString* error) const
{
    bool readOk = false;
    QString content = read(&readOk);
    if (!readOk) {
        if (error)
            *error = QStringLiteral("failed to read file before append: %1").arg(m_filePath);
        return false;
    }

    QString normalizedEntry = entry;
    if (!normalizedEntry.endsWith(QLatin1Char('\n')))
        normalizedEntry.append(QLatin1Char('\n'));

    if (!content.isEmpty() && !content.endsWith(QLatin1Char('\n')))
        content.append(QLatin1Char('\n'));
    content.append(normalizedEntry);
    return writeAtomic(content, error);
}
