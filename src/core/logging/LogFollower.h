#ifndef LOGFOLLOWER_H
#define LOGFOLLOWER_H

#include "LogQueryEngine.h"

#include <QObject>

class QTimer;

class LogFollower : public QObject {
    Q_OBJECT
public:
    explicit LogFollower(const LogQueryEngine::Query& filter,
                         const QString& dataRootPath = QString(),
                         QObject* parent = nullptr);

    void start();

private slots:
    void pollNewEvents();

private:
    LogQueryEngine::Query m_filter;
    QString m_dataRootPath;
    qint64 m_anchorId = 0;
    QTimer* m_pollTimer = nullptr;
};

#endif // LOGFOLLOWER_H
