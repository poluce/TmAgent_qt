#ifndef HEARTBEATWAKE_H
#define HEARTBEATWAKE_H

#include <QObject>
#include <QString>
#include <functional>

class QTimer;

class HeartbeatWake : public QObject {
    Q_OBJECT
public:
    explicit HeartbeatWake(QObject* parent = nullptr);

    void setHandler(const std::function<void(const QString&)>& handler);
    void setCoalesceMs(int ms);
    int coalesceMs() const;

    void request(const QString& reason);
    void cancel();

signals:
    void executed(const QString& reason);
    void skipped(const QString& reason);

private slots:
    void onCoalesceTimeout();

private:
    std::function<void(const QString&)> m_handler;
    QString m_pendingReason;
    bool m_scheduled = false;
    bool m_running = false;
    int m_coalesceMs = 250;
    QTimer* m_timer = nullptr;
};

#endif // HEARTBEATWAKE_H
