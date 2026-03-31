#ifndef LOGCATALOG_H
#define LOGCATALOG_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LogCatalog {

struct SessionInfo {
    QString sessionId;
    QString agentId;
    QString title;
    QDateTime createdAt;
    QDateTime lastModified;
    qint64 messageCount = 0;
    qint64 fileSizeBytes = 0;
};

struct SessionListResult {
    QVector<SessionInfo> sessions;
    QStringList warnings;
};

struct SessionSummary {
    QString sessionId;
    QString title;
    QString type;
    qint64 messageCount = 0;
    QDateTime lastActiveAt;
};

struct AgentInfo {
    QString agentId;
    QString name;
    QString avatar;
    QString providerInstanceId;
    QString selectedModelId;
    QString configId;
    QString description;
    QString systemPrompt;
    int recursionDepth = 3;
    bool delegateEnabled = true;
    QStringList allowedTools;
    QString identitySnippet;
    QString soulSnippet;
    QString memorySnippet;
    QString userViewSnippet;
    int dailyMemoryFileCount = 0;
    bool hasMemoryIndex = false;
    QVector<SessionSummary> sessions;
    QDateTime profileLastModified;
    qint64 totalDiskBytes = 0;
};

struct AgentQueryOptions {
    QString dataRootPath;
    QString filterAgentId;
    QString filterAgentName;
    bool detail = false;
};

struct AgentListResult {
    QVector<AgentInfo> agents;
    QStringList warnings;
};

SessionListResult listSessions(const QString& dataRootPath = QString());
AgentListResult listAgents(const AgentQueryOptions& options = AgentQueryOptions());

QString formatTable(const SessionListResult& result);
QString formatJson(const SessionListResult& result);

QString formatTable(const AgentListResult& result);
QString formatJson(const AgentListResult& result);
QString formatReport(const AgentListResult& result);

} // namespace LogCatalog

#endif // LOGCATALOG_H
