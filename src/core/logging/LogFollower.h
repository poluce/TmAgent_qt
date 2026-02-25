#ifndef LOGFOLLOWER_H
#define LOGFOLLOWER_H

#include "LogQueryEngine.h"

#include <QObject>

class QFileSystemWatcher;
class QTimer;

class LogFollower : public QObject {
    Q_OBJECT
public:
    explicit LogFollower(const LogQueryEngine::Query& filter,
                         const QString& dataRootPath = QString(),
                         QObject* parent = nullptr);

    void start();

private slots:
    void onFileChanged(const QString& path);
    void onPollCheck();

private:
    void readNewLines();
    void processLine(const QByteArray& line);

    LogQueryEngine::Query m_filter;
    QString m_logFilePath;
    QString m_dataRootPath;
    qint64 m_fileOffset = 0;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_pollTimer = nullptr; // 备用轮询（500ms）
};

#endif // LOGFOLLOWER_H
