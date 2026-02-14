#ifndef MEMORYDOCUMENT_H
#define MEMORYDOCUMENT_H

#include <QString>

class MemoryDocument {
public:
    explicit MemoryDocument(const QString& filePath = QString());

    void setFilePath(const QString& filePath);
    QString filePath() const;
    bool exists() const;

    QString read(bool* ok = nullptr) const;
    QString readTruncated(int maxChars, bool* ok = nullptr) const;

    bool writeAtomic(const QString& content, QString* error = nullptr) const;
    bool appendAtomic(const QString& entry, QString* error = nullptr) const;

private:
    bool ensureParentDir(QString* error = nullptr) const;

    QString m_filePath;
};

#endif // MEMORYDOCUMENT_H
