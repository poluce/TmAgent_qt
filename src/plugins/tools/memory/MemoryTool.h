#ifndef MEMORYTOOL_H
#define MEMORYTOOL_H

#include "core/agent/ToolTypes.h"
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

class QSqlQuery;
class QVariant;

class MemoryTool {
public:
    using WriteHandler = std::function<ToolResult(const QJsonObject&)>;
    static QList<Tool> toolSchemas();

    static QString executeSearch(const QJsonObject& args);
    static QString executeRebuild(const QJsonObject& args);
    static ToolResult executeWrite(const QJsonObject& args);
    static void setWriteHandler(const WriteHandler& handler);

private:
    struct SearchHit {
        QString agentId;
        QString relativePath;
        QString sourceRelativePath;
        QString sourceKind;
        int lineNo = 0;
        QString snippet;
        double textScore = 0.0;
        double vectorScore = 0.0;
        double finalScore = 0.0;
    };

    static QString dataRootPath();
    static QString agentsRootPath(const QString& root);
    static QString normalizeScope(const QJsonObject& args);
    static QString resolveAgentIdArg(const QJsonObject& args);
    static bool resolveTargetAgents(const QString& agentsRoot, const QString& scope, const QString& agentId, QStringList* targetAgents, QString* error);
    static QString sqliteIndexPath(const QString& agentsRoot, const QString& agentId);
    static QString semanticIndexPath(const QString& agentsRoot, const QString& agentId);
    static QStringList memorySourceFiles(const QString& agentPath, bool includeDaily, int maxDailyFiles);
    static bool isIndexStale(const QString& agentsRoot, const QString& agentId, const QString& indexPath);
    static bool rebuildAgentIndex(const QString& agentsRoot, const QString& agentId, int* indexedRows, QString* error);
    static QList<SearchHit> searchWithSqlite(const QString& agentsRoot, const QString& agentId, const QString& query, bool includeDaily, int maxResults, QString* error);
    static void fillHitsFromQuery(const QString& agentId, const QString& query, QSqlQuery& queryStmt, bool hasSqliteRank, QList<SearchHit>* hits);
    static QString buildMatchExpr(const QString& raw);
    static bool isLikelyCorruptedSqliteError(const QString& errorText);
    static QList<SearchHit> searchWithMarkdown(const QString& root, const QString& agentsRoot, const QString& agentId, const QString& query, bool includeDaily, int maxResults);
    static void appendCappedHits(QList<SearchHit>* target, const QList<SearchHit>& source, int maxSnippetChars);
    static QString sourceKindForRelativePath(const QString& relativePath);
    static double textScoreFromSqliteRank(const QVariant& rankValue);
    static double textScoreFromSnippetMatch(const QString& snippet, const QString& query);
    static int recencyBonusForRelativePath(const QString& relativePath);
    static bool isLowValueMetadataLine(const QString& snippet);
    static double finalScoreForHit(const SearchHit& hit);
    static void sortAndTrimHits(QList<SearchHit>* hits, int maxResults);
    static QString clipSnippet(const QString& input, int maxChars);
};

#endif // MEMORYTOOL_H
