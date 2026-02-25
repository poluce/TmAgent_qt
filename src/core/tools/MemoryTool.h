#ifndef MEMORYTOOL_H
#define MEMORYTOOL_H

#include <QJsonObject>
#include <QList>
#include <QString>

class QSqlQuery;

class MemoryTool {
public:
    static QString executeSearch(const QJsonObject& args);
    static QString executeRebuild(const QJsonObject& args);

private:
    struct SearchHit {
        QString agentId;
        QString relativePath;
        int lineNo = 0;
        QString snippet;
    };

    static QString dataRootPath();
    static QString agentsRootPath(const QString& root);
    static QString normalizeScope(const QJsonObject& args);
    static QString resolveAgentIdArg(const QJsonObject& args);
    static bool resolveTargetAgents(const QString& agentsRoot, const QString& scope, const QString& agentId, QStringList* targetAgents, QString* error);
    static QString sqliteIndexPath(const QString& agentsRoot, const QString& agentId);
    static QStringList memorySourceFiles(const QString& agentPath, bool includeDaily, int maxDailyFiles);
    static bool isIndexStale(const QString& agentsRoot, const QString& agentId, const QString& indexPath);
    static bool rebuildAgentIndex(const QString& agentsRoot, const QString& agentId, int* indexedRows, QString* error);
    static QList<SearchHit> searchWithSqlite(const QString& agentsRoot, const QString& agentId, const QString& query, bool includeDaily, int maxResults, QString* error);
    static void fillHitsFromQuery(const QString& agentId, QSqlQuery& queryStmt, QList<SearchHit>* hits);
    static QString buildMatchExpr(const QString& raw);
    static bool isLikelyCorruptedSqliteError(const QString& errorText);
    static QList<SearchHit> searchWithMarkdown(const QString& root, const QString& agentsRoot, const QString& agentId, const QString& query, bool includeDaily, int maxResults);
    static void appendCappedHits(QList<SearchHit>* target, const QList<SearchHit>& source, int maxSnippetChars, int maxResults);
    static QString clipSnippet(const QString& input, int maxChars);
};

#endif // MEMORYTOOL_H
