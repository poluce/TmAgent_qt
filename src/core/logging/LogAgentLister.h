#ifndef LOGAGENTLISTER_H
#define LOGAGENTLISTER_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LogAgentLister {

struct SessionSummary {
    QString sessionId;
    QString title;
    QString type;
    qint64 messageCount = 0;
    QDateTime lastActiveAt;
};

struct AgentInfo {
    // 基本信息
    QString agentId;
    QString name;
    QString avatar;

    // 模型配置
    QString providerInstanceId;
    QString selectedModelId;
    QString configId;

    // 角色配置
    QString description;
    QString systemPrompt;
    int recursionDepth = 3;
    bool delegateEnabled = true;
    QStringList allowedTools;

    // 记忆摘要（detail 模式）
    QString identitySnippet;
    QString soulSnippet;
    QString memorySnippet;
    QString userViewSnippet;
    int dailyMemoryFileCount = 0;
    bool hasMemoryIndex = false;

    // 关联会话
    QVector<SessionSummary> sessions;

    // 元信息
    QDateTime profileLastModified;
    qint64 totalDiskBytes = 0;
};

struct ListResult {
    QVector<AgentInfo> agents;
    QStringList warnings;
};

struct QueryOptions {
    QString dataRootPath;
    QString filterAgentId;
    QString filterAgentName;
    bool detail = false;
};

ListResult listAgents(const QueryOptions& options = QueryOptions());

QString formatTable(const ListResult& result);
QString formatJson(const ListResult& result);
QString formatReport(const ListResult& result);

} // namespace LogAgentLister

#endif // LOGAGENTLISTER_H
