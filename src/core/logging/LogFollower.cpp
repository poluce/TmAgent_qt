#include "LogFollower.h"

#include "LogRecordSupport.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QTimer>

LogFollower::LogFollower(const LogQueryEngine::Query& filter,
                         const QString& dataRootPath,
                         QObject* parent)
    : QObject(parent)
    , m_filter(filter)
    , m_dataRootPath(dataRootPath)
{
    if (m_dataRootPath.isEmpty())
        m_dataRootPath = QDir::home().filePath(QStringLiteral(".tmagent"));
}

void LogFollower::start()
{
    QTextStream err(stderr);

    QString dbError;
    QSqlDatabase db = LogRecordSupport::openConnection(m_dataRootPath, &dbError);
    if (!db.isValid() || !db.isOpen()) {
        err << QStringLiteral("SQLite 连接失败，无法 follow events: %1\n").arg(dbError);
        err.flush();
        QCoreApplication::quit();
        return;
    }

    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT COALESCE(MAX(id), 0) FROM events")) && q.next())
        m_anchorId = q.value(0).toLongLong();
    else
        m_anchorId = 0;

    err << QStringLiteral("Following: sqlite://events (anchor_id=%1)\n").arg(m_anchorId);
    err << QStringLiteral("Press Ctrl+C to stop.\n");
    err.flush();

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &LogFollower::pollNewEvents);
    m_pollTimer->start();
}

void LogFollower::pollNewEvents()
{
    QString dbError;
    QSqlDatabase db = LogRecordSupport::openConnection(m_dataRootPath, &dbError);
    if (!db.isValid() || !db.isOpen())
        return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, raw_json FROM events "
        "WHERE id > ? "
        "ORDER BY id ASC "
        "LIMIT 200"));
    q.addBindValue(m_anchorId);

    if (!q.exec())
        return;

    qint64 latestId = m_anchorId;
    while (q.next()) {
        const qint64 id = q.value(0).toLongLong();
        if (id > latestId)
            latestId = id;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(q.value(1).toString().toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject obj = doc.object();
        LogQueryEngine::Hit hit;
        if (!LogRecordSupport::eventMatchesFilter(obj, m_filter, &hit))
            continue;

        const QString timeStr = hit.timestamp.isValid()
            ? hit.timestamp.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz"))
            : QStringLiteral("??:??:??.???");

        QTextStream out(stdout);
        out << QStringLiteral("[%1] [%2] [%3] [%4] %5\n")
                   .arg(timeStr,
                        hit.level.isEmpty() ? QStringLiteral("-") : hit.level,
                        hit.eventType.isEmpty() ? QStringLiteral("-") : hit.eventType,
                        hit.toolName.isEmpty() ? QStringLiteral("-") : hit.toolName,
                        LogRecordSupport::clip(hit.summary, 200));
        out.flush();
    }

    m_anchorId = latestId;
}
