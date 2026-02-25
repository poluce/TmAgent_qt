#include "LogFollower.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

namespace {

QString stringField(const QJsonObject& obj, const QString& key)
{
    return obj.value(key).toString().trimmed();
}

QString stringFieldAny(const QJsonObject& obj, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QString value = stringField(obj, key);
        if (!value.isEmpty())
            return value;
    }
    return QString();
}

QDateTime parseTimestamp(const QJsonObject& obj)
{
    const QString ts = stringFieldAny(obj, QStringList()
                                               << QStringLiteral("timestamp")
                                               << QStringLiteral("time")
                                               << QStringLiteral("createdAt")
                                               << QStringLiteral("created_at"));
    if (ts.isEmpty())
        return QDateTime();

    QDateTime dt = QDateTime::fromString(ts, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(ts, Qt::ISODate);
    if (dt.isValid() && dt.timeSpec() == Qt::LocalTime)
        dt = dt.toUTC();
    return dt;
}

QString extractEventType(const QJsonObject& obj)
{
    QString type = stringField(obj, QStringLiteral("type"));
    if (!type.isEmpty())
        return type;
    return stringField(obj.value(QStringLiteral("event")).toObject(), QStringLiteral("type"));
}

QString extractToolName(const QJsonObject& obj)
{
    QString value = stringFieldAny(obj, QStringList()
                                            << QStringLiteral("tool_name")
                                            << QStringLiteral("toolName"));
    if (!value.isEmpty())
        return value;
    const QJsonObject toolEventObj = obj.value(QStringLiteral("toolEvent")).toObject();
    value = stringFieldAny(toolEventObj, QStringList()
                                             << QStringLiteral("toolName")
                                             << QStringLiteral("tool_name"));
    if (!value.isEmpty())
        return value;
    return stringField(toolEventObj.value(QStringLiteral("data")).toObject(),
                       QStringLiteral("tool_name"));
}

QString extractLevel(const QJsonObject& obj)
{
    return stringFieldAny(obj, QStringList()
                                   << QStringLiteral("level")
                                   << QStringLiteral("severity"));
}

QString clip(const QString& text, int maxChars)
{
    const QString simplified = text.simplified();
    if (maxChars <= 0 || simplified.size() <= maxChars)
        return simplified;
    return simplified.left(maxChars) + QStringLiteral("...");
}

bool matchesFilter(const QJsonObject& obj, const LogQueryEngine::Query& filter)
{
    // session_id
    if (!filter.sessionId.isEmpty()) {
        const QString sid = stringFieldAny(obj, QStringList()
                                                    << QStringLiteral("session_id")
                                                    << QStringLiteral("sessionId"));
        if (sid.compare(filter.sessionId, Qt::CaseInsensitive) != 0)
            return false;
    }

    // tool_name
    if (!filter.toolName.isEmpty()) {
        if (extractToolName(obj).compare(filter.toolName, Qt::CaseInsensitive) != 0)
            return false;
    }

    // event_type
    if (!filter.eventType.isEmpty()) {
        if (extractEventType(obj).compare(filter.eventType, Qt::CaseInsensitive) != 0)
            return false;
    }

    // actor_id
    if (!filter.actorId.isEmpty()) {
        const QString actor = stringFieldAny(obj, QStringList()
                                                      << QStringLiteral("actorIdentityId")
                                                      << QStringLiteral("actor_id"));
        if (actor.compare(filter.actorId, Qt::CaseInsensitive) != 0)
            return false;
    }

    // keyword
    if (!filter.keyword.isEmpty()) {
        const QString raw = QString::fromUtf8(
            QJsonDocument(obj).toJson(QJsonDocument::Compact)).toLower();
        if (!raw.contains(filter.keyword.toLower()))
            return false;
    }

    return true;
}

} // namespace

LogFollower::LogFollower(const LogQueryEngine::Query& filter,
                         const QString& dataRootPath,
                         QObject* parent)
    : QObject(parent)
    , m_filter(filter)
    , m_dataRootPath(dataRootPath)
{
    if (m_dataRootPath.isEmpty())
        m_dataRootPath = QDir::home().filePath(QStringLiteral(".tmagent"));

    m_logFilePath = QDir(m_dataRootPath)
                        .filePath(QStringLiteral("logs/events-current.jsonl"));
}

void LogFollower::start()
{
    QTextStream err(stderr);

    // 记录当前文件大小作为起始偏移（只显示新增内容）
    QFileInfo fi(m_logFilePath);
    if (fi.exists()) {
        m_fileOffset = fi.size();
    } else {
        err << QStringLiteral("日志文件不存在，等待创建: %1\n")
                   .arg(QDir::toNativeSeparators(m_logFilePath));
        err.flush();
        m_fileOffset = 0;
    }

    err << QStringLiteral("Following: %1\n").arg(QDir::toNativeSeparators(m_logFilePath));
    err << QStringLiteral("Press Ctrl+C to stop.\n");
    err.flush();

    // QFileSystemWatcher 监听文件变化
    m_watcher = new QFileSystemWatcher(this);
    if (fi.exists()) {
        m_watcher->addPath(m_logFilePath);
    }
    // 同时监听目录（文件可能被重建）
    const QString logsDir = QFileInfo(m_logFilePath).absolutePath();
    if (QDir(logsDir).exists())
        m_watcher->addPath(logsDir);

    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &LogFollower::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) {
                // 文件可能被重建，重新添加监听
                QFileInfo fi(m_logFilePath);
                if (fi.exists() && !m_watcher->files().contains(m_logFilePath)) {
                    m_watcher->addPath(m_logFilePath);
                    m_fileOffset = 0; // 新文件从头读
                    readNewLines();
                }
            });

    // 备用轮询定时器（Windows 上 QFileSystemWatcher 对追加写入通知不可靠）
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &LogFollower::onPollCheck);
    m_pollTimer->start();
}

void LogFollower::onFileChanged(const QString& path)
{
    Q_UNUSED(path)
    readNewLines();

    // QFileSystemWatcher 在某些平台上文件修改后会移除监听，需要重新添加
    if (!m_watcher->files().contains(m_logFilePath)) {
        if (QFileInfo::exists(m_logFilePath))
            m_watcher->addPath(m_logFilePath);
    }
}

void LogFollower::onPollCheck()
{
    QFileInfo fi(m_logFilePath);
    if (!fi.exists())
        return;

    if (fi.size() > m_fileOffset)
        readNewLines();
    else if (fi.size() < m_fileOffset) {
        // 文件被截断或重建
        m_fileOffset = 0;
        readNewLines();
    }
}

void LogFollower::readNewLines()
{
    QFile file(m_logFilePath);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return;

    if (file.size() < m_fileOffset) {
        // 文件被截断，从头开始
        m_fileOffset = 0;
    }

    file.seek(m_fileOffset);

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            processLine(trimmed);
    }

    m_fileOffset = file.pos();
    file.close();
}

void LogFollower::processLine(const QByteArray& line)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;

    const QJsonObject obj = doc.object();

    // 应用过滤器
    if (!matchesFilter(obj, m_filter))
        return;

    // 提取字段
    const QDateTime timestamp = parseTimestamp(obj);
    const QString level = extractLevel(obj);
    const QString eventType = extractEventType(obj);
    const QString toolName = extractToolName(obj);

    // 构建摘要
    QString summary = stringField(obj, QStringLiteral("summary"));
    if (summary.isEmpty()) {
        const QString error = stringField(obj, QStringLiteral("error"));
        if (!error.isEmpty()) {
            summary = QStringLiteral("error=%1").arg(error);
        } else {
            summary = clip(QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact)), 160);
        }
    }

    // 输出格式: [时间] [level] [event_type] [tool_name] summary
    const QString timeStr = timestamp.isValid()
        ? timestamp.toLocalTime().toString(QStringLiteral("HH:mm:ss.zzz"))
        : QStringLiteral("??:??:??.???");

    QTextStream out(stdout);
    out << QStringLiteral("[%1] [%2] [%3] [%4] %5\n")
               .arg(timeStr,
                    level.isEmpty() ? QStringLiteral("-") : level,
                    eventType.isEmpty() ? QStringLiteral("-") : eventType,
                    toolName.isEmpty() ? QStringLiteral("-") : toolName,
                    clip(summary, 200));
    out.flush();
}
