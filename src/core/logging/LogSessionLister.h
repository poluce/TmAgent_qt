#ifndef LOGSESSIONLISTER_H
#define LOGSESSIONLISTER_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LogSessionLister {

struct SessionInfo {
    QString sessionId;
    QString agentId;
    QString title;
    QDateTime createdAt;
    QDateTime lastModified;
    qint64 messageCount = 0;
    qint64 fileSizeBytes = 0;
};

struct ListResult {
    QVector<SessionInfo> sessions;
    QStringList warnings;
};

// 列出所有会话，dataRootPath 默认 ~/.tmagent
ListResult listSessions(const QString& dataRootPath = QString());

// 格式化输出
QString formatTable(const ListResult& result);
QString formatJson(const ListResult& result);

} // namespace LogSessionLister

#endif // LOGSESSIONLISTER_H
