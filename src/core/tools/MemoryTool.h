#ifndef MEMORYTOOL_H
#define MEMORYTOOL_H

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>

class MemoryTool {
public:
    static QString executeSearch(const QJsonObject& args)
    {
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty())
            return QStringLiteral("错误: query 不能为空");

        const QString normalizedScope = normalizeScope(args);
        const QString agentId = resolveAgentIdArg(args);
        const bool includeDaily = args.value(QStringLiteral("include_daily")).toBool(true);
        const int maxResults = qBound(1, args.value(QStringLiteral("max_results")).toInt(10), 100);
        const int maxSnippetChars =
            qBound(60, args.value(QStringLiteral("max_snippet_chars")).toInt(180), 400);

        const QString root = dataRootPath();
        const QString agentsRoot = agentsRootPath(root);
        QStringList targetAgents;
        QString resolveError;
        if (!resolveTargetAgents(agentsRoot,
                                 normalizedScope,
                                 agentId,
                                 &targetAgents,
                                 &resolveError)) {
            return resolveError;
        }

        QList<SearchHit> hits;
        QStringList warnings;
        bool usedSqlite = false;

        for (const QString& id : targetAgents) {
            if (hits.size() >= maxResults)
                break;
            const int remaining = maxResults - hits.size();

            const QString dbPath = sqliteIndexPath(agentsRoot, id);
            bool indexOk = false;
            if (!QFileInfo::exists(dbPath) || isIndexStale(agentsRoot, id, dbPath)) {
                QString rebuildError;
                int indexedRows = 0;
                indexOk =
                    rebuildAgentIndex(agentsRoot, id, &indexedRows, &rebuildError);
                if (!indexOk && !rebuildError.isEmpty()) {
                    warnings.append(
                        QStringLiteral("%1: 索引重建失败，已回退 Markdown（%2）")
                            .arg(id, rebuildError));
                }
            }
            if (!indexOk && QFileInfo::exists(dbPath))
                indexOk = true;

            if (indexOk) {
                QString sqliteError;
                QList<SearchHit> sqliteHits =
                    searchWithSqlite(agentsRoot, id, query, includeDaily, remaining, &sqliteError);
                if (!sqliteError.isEmpty()) {
                    warnings.append(QStringLiteral("%1: SQLite 检索失败，已回退 Markdown（%2）")
                                        .arg(id, sqliteError));
                } else {
                    usedSqlite = true;
                    appendCappedHits(&hits, sqliteHits, maxSnippetChars, maxResults);
                    continue;
                }
            }

            QList<SearchHit> markdownHits =
                searchWithMarkdown(root, agentsRoot, id, query, includeDaily, remaining);
            appendCappedHits(&hits, markdownHits, maxSnippetChars, maxResults);
        }

        QString output;
        output += QStringLiteral("memory_search query: %1\n").arg(query);
        output += QStringLiteral("scope: %1\n").arg(normalizedScope);
        output += QStringLiteral("backend: %1\n")
                      .arg(usedSqlite ? QStringLiteral("sqlite_fts") : QStringLiteral("markdown_scan"));
        output += QStringLiteral("results: %1\n").arg(hits.size());
        if (!warnings.isEmpty())
            output += QStringLiteral("warnings: %1\n").arg(warnings.join(QStringLiteral(" | ")));

        if (hits.isEmpty()) {
            output += QStringLiteral("未找到匹配的记忆条目。");
            return output;
        }

        QStringList lines;
        for (const SearchHit& hit : qAsConst(hits)) {
            lines.append(QStringLiteral("- [%1] %2:%3 %4")
                             .arg(hit.agentId,
                                  hit.relativePath,
                                  QString::number(hit.lineNo),
                                  clipSnippet(hit.snippet, maxSnippetChars)));
        }
        output += QStringLiteral("\n命中列表:\n");
        output += lines.join(QStringLiteral("\n"));
        return output;
    }

    static QString executeRebuild(const QJsonObject& args)
    {
        const QString normalizedScope = normalizeScope(args);
        const QString agentId = resolveAgentIdArg(args);
        const QString root = dataRootPath();
        const QString agentsRoot = agentsRootPath(root);

        QStringList targetAgents;
        QString resolveError;
        if (!resolveTargetAgents(agentsRoot,
                                 normalizedScope,
                                 agentId,
                                 &targetAgents,
                                 &resolveError)) {
            return resolveError;
        }

        if (targetAgents.isEmpty())
            return QStringLiteral("memory_reindex: 没有可重建的助手。");

        QStringList okLines;
        QStringList failedLines;
        int totalRows = 0;
        for (const QString& id : targetAgents) {
            int indexedRows = 0;
            QString error;
            if (rebuildAgentIndex(agentsRoot, id, &indexedRows, &error)) {
                totalRows += indexedRows;
                okLines.append(
                    QStringLiteral("- [%1] indexed_rows=%2").arg(id, QString::number(indexedRows)));
            } else {
                failedLines.append(QStringLiteral("- [%1] %2").arg(id, error));
            }
        }

        QString output;
        output += QStringLiteral("memory_reindex scope: %1\n").arg(normalizedScope);
        output += QStringLiteral("agents_total: %1\n").arg(targetAgents.size());
        output += QStringLiteral("agents_success: %1\n").arg(okLines.size());
        output += QStringLiteral("rows_indexed: %1\n").arg(totalRows);
        if (!okLines.isEmpty()) {
            output += QStringLiteral("\n成功列表:\n");
            output += okLines.join(QStringLiteral("\n"));
        }
        if (!failedLines.isEmpty()) {
            output += QStringLiteral("\n失败列表:\n");
            output += failedLines.join(QStringLiteral("\n"));
        }
        return output;
    }

private:
    struct SearchHit {
        QString agentId;
        QString relativePath;
        int lineNo = 0;
        QString snippet;
    };

    static QString dataRootPath()
    {
        return QDir::home().filePath(QStringLiteral(".tmagent"));
    }

    static QString agentsRootPath(const QString& root)
    {
        return QDir(root).filePath(QStringLiteral("identities/agents"));
    }

    static QString normalizeScope(const QJsonObject& args)
    {
        const QString scope =
            args.value(QStringLiteral("scope")).toString().trimmed().toLower();
        return scope.isEmpty() ? QStringLiteral("self") : scope;
    }

    static QString resolveAgentIdArg(const QJsonObject& args)
    {
        QString agentId = args.value(QStringLiteral("agent_id")).toString().trimmed();
        if (agentId.isEmpty())
            agentId = args.value(QStringLiteral("_agent_id")).toString().trimmed();
        return agentId;
    }

    static bool resolveTargetAgents(const QString& agentsRoot,
                                    const QString& scope,
                                    const QString& agentId,
                                    QStringList* targetAgents,
                                    QString* error)
    {
        if (targetAgents)
            targetAgents->clear();
        if (error)
            error->clear();

        QDir agentsDir(agentsRoot);
        if (!agentsDir.exists()) {
            if (error) {
                *error =
                    QStringLiteral("错误: 记忆目录不存在: %1")
                        .arg(QDir::toNativeSeparators(agentsRoot));
            }
            return false;
        }

        if (scope == QLatin1String("all")) {
            if (targetAgents) {
                *targetAgents =
                    agentsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            }
            return true;
        }

        if (agentId.isEmpty()) {
            if (error)
                *error = QStringLiteral("错误: 缺少 agent_id（scope=self/agent 时必填）");
            return false;
        }
        const QString agentPath = agentsDir.filePath(agentId);
        if (!QDir(agentPath).exists()) {
            if (error)
                *error = QStringLiteral("错误: agent 目录不存在: %1").arg(agentId);
            return false;
        }
        if (targetAgents)
            targetAgents->append(agentId);
        return true;
    }

    static QString sqliteIndexPath(const QString& agentsRoot, const QString& agentId)
    {
        return QDir(QDir(agentsRoot).filePath(agentId)).filePath(QStringLiteral("memory_index.sqlite"));
    }

    static QStringList memorySourceFiles(const QString& agentPath,
                                         bool includeDaily,
                                         int maxDailyFiles)
    {
        QStringList files;
        files << QDir(agentPath).filePath(QStringLiteral("memory.md"));
        files << QDir(agentPath).filePath(QStringLiteral("user_view.md"));

        if (!includeDaily)
            return files;

        QDir dailyDir(QDir(agentPath).filePath(QStringLiteral("memory")));
        if (!dailyDir.exists())
            return files;

        QStringList dailyFiles =
            dailyDir.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files, QDir::Name);
        if (maxDailyFiles > 0 && dailyFiles.size() > maxDailyFiles)
            dailyFiles = dailyFiles.mid(dailyFiles.size() - maxDailyFiles);
        for (const QString& daily : dailyFiles)
            files << dailyDir.filePath(daily);
        return files;
    }

    static bool isIndexStale(const QString& agentsRoot,
                             const QString& agentId,
                             const QString& indexPath)
    {
        const QFileInfo indexInfo(indexPath);
        if (!indexInfo.exists())
            return true;

        const QDateTime indexTime = indexInfo.lastModified();
        const QString agentPath = QDir(agentsRoot).filePath(agentId);
        const QStringList files = memorySourceFiles(agentPath, true, -1);
        for (const QString& path : files) {
            const QFileInfo fi(path);
            if (!fi.exists())
                continue;
            if (fi.lastModified() > indexTime)
                return true;
        }
        return false;
    }

    static bool rebuildAgentIndex(const QString& agentsRoot,
                                  const QString& agentId,
                                  int* indexedRows,
                                  QString* error)
    {
        if (indexedRows)
            *indexedRows = 0;
        if (error)
            error->clear();

        const QString agentPath = QDir(agentsRoot).filePath(agentId);
        if (!QDir(agentPath).exists()) {
            if (error)
                *error = QStringLiteral("agent 目录不存在");
            return false;
        }

        const QString dbPath = sqliteIndexPath(agentsRoot, agentId);
        const QString connectionName =
            QStringLiteral("memory_index_build_%1_%2")
                .arg(agentId, QUuid::createUuid().toString(QUuid::WithoutBraces));
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(dbPath);
            if (!db.open()) {
                if (error)
                    *error = db.lastError().text();
                ok = false;
            } else {
                ok = true;
                QSqlQuery pragma(db);
                pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
                pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

                QSqlQuery schema(db);
                if (!schema.exec(
                        QStringLiteral("CREATE TABLE IF NOT EXISTS memory_meta (key TEXT PRIMARY KEY, value TEXT)"))
                    || !schema.exec(
                        QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS memory_fts USING fts5(rel_path, line_no UNINDEXED, content, tokenize='unicode61')"))) {
                    if (error)
                        *error = schema.lastError().text();
                    ok = false;
                }
            }

            if (ok && !db.transaction()) {
                if (error)
                    *error = db.lastError().text();
                ok = false;
            }

            int rowCount = 0;
            if (ok) {
                QSqlQuery clear(db);
                if (!clear.exec(QStringLiteral("DELETE FROM memory_fts"))
                    || !clear.exec(QStringLiteral("DELETE FROM memory_meta"))) {
                    if (error)
                        *error = clear.lastError().text();
                    ok = false;
                }
            }

            QSqlQuery insert(db);
            if (ok) {
                insert.prepare(
                    QStringLiteral("INSERT INTO memory_fts(rel_path, line_no, content) VALUES (?, ?, ?)"));
                const QStringList files = memorySourceFiles(agentPath, true, -1);
                for (const QString& path : files) {
                    QFile file(path);
                    if (!file.exists() || !file.open(QFile::ReadOnly | QFile::Text))
                        continue;

                    const QString relPath = QDir(agentPath).relativeFilePath(path);
                    int lineNo = 0;
                    while (!file.atEnd()) {
                        const QString line = QString::fromUtf8(file.readLine()).simplified();
                        ++lineNo;
                        if (line.isEmpty())
                            continue;
                        insert.bindValue(0, relPath);
                        insert.bindValue(1, lineNo);
                        insert.bindValue(2, line);
                        if (!insert.exec()) {
                            if (error)
                                *error = insert.lastError().text();
                            ok = false;
                            break;
                        }
                        ++rowCount;
                    }
                    file.close();
                    if (!ok)
                        break;
                }
            }

            if (ok) {
                QSqlQuery meta(db);
                meta.prepare(
                    QStringLiteral("INSERT OR REPLACE INTO memory_meta(key, value) VALUES (?, ?)"));
                auto upsertMeta = [&meta](const QString& key, const QString& value) {
                    meta.bindValue(0, key);
                    meta.bindValue(1, value);
                    return meta.exec();
                };
                if (!upsertMeta(QStringLiteral("indexed_at_utc"),
                                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                    || !upsertMeta(QStringLiteral("row_count"), QString::number(rowCount))) {
                    if (error)
                        *error = meta.lastError().text();
                    ok = false;
                } else if (indexedRows) {
                    *indexedRows = rowCount;
                }
            }

            if (ok) {
                if (!db.commit()) {
                    if (error)
                        *error = db.lastError().text();
                    ok = false;
                }
            } else {
                db.rollback();
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
        return ok;
    }

    static QList<SearchHit> searchWithSqlite(const QString& agentsRoot,
                                             const QString& agentId,
                                             const QString& query,
                                             bool includeDaily,
                                             int maxResults,
                                             QString* error)
    {
        QList<SearchHit> hits;
        if (error)
            error->clear();
        if (maxResults <= 0)
            return hits;

        const QString dbPath = sqliteIndexPath(agentsRoot, agentId);
        if (!QFileInfo::exists(dbPath))
            return hits;

        const QString connectionName =
            QStringLiteral("memory_index_search_%1_%2")
                .arg(agentId, QUuid::createUuid().toString(QUuid::WithoutBraces));
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(dbPath);
            if (!db.open()) {
                if (error)
                    *error = db.lastError().text();
                ok = false;
            } else {
                ok = true;
                const QString matchExpr = buildMatchExpr(query);

                QSqlQuery check(db);
                if (!check.exec(
                        QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='memory_fts'"))
                    || !check.next()) {
                    if (error)
                        *error = QStringLiteral("memory_fts 表不存在");
                    ok = false;
                }

                if (ok) {
                    QSqlQuery queryStmt(db);
                    QString sql =
                        QStringLiteral("SELECT rel_path, line_no, content FROM memory_fts WHERE memory_fts MATCH ?");
                    if (!includeDaily)
                        sql += QStringLiteral(" AND rel_path NOT LIKE 'memory/%'");
                    sql += QStringLiteral(" LIMIT ?");
                    queryStmt.prepare(sql);
                    queryStmt.bindValue(0, matchExpr);
                    queryStmt.bindValue(1, maxResults);
                    if (!queryStmt.exec()) {
                        // MATCH 表达式可能被特殊字符破坏，降级到 LIKE。
                        QSqlQuery likeStmt(db);
                        QString likeSql = QStringLiteral(
                            "SELECT rel_path, line_no, content FROM memory_fts WHERE content LIKE ?");
                        if (!includeDaily)
                            likeSql += QStringLiteral(" AND rel_path NOT LIKE 'memory/%'");
                        likeSql += QStringLiteral(" LIMIT ?");
                        likeStmt.prepare(likeSql);
                        likeStmt.bindValue(0, QStringLiteral("%") + query + QStringLiteral("%"));
                        likeStmt.bindValue(1, maxResults);
                        if (!likeStmt.exec()) {
                            if (error)
                                *error = likeStmt.lastError().text();
                            ok = false;
                        } else {
                            fillHitsFromQuery(agentId, likeStmt, &hits);
                        }
                    } else {
                        fillHitsFromQuery(agentId, queryStmt, &hits);
                    }
                }
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
        if (!ok)
            hits.clear();
        return hits;
    }

    static void fillHitsFromQuery(const QString& agentId,
                                  QSqlQuery& queryStmt,
                                  QList<SearchHit>* hits)
    {
        if (!hits)
            return;
        while (queryStmt.next()) {
            SearchHit hit;
            hit.agentId = agentId;
            const QString relPath = queryStmt.value(0).toString().trimmed();
            hit.relativePath =
                QStringLiteral("identities/agents/%1/%2").arg(agentId, relPath);
            hit.lineNo = queryStmt.value(1).toInt();
            hit.snippet = queryStmt.value(2).toString().simplified();
            hits->append(hit);
        }
    }

    static QString buildMatchExpr(const QString& raw)
    {
        const QString text = raw.trimmed();
        if (text.isEmpty())
            return QStringLiteral("\"\"");
        QString escaped = text;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    }

    static QList<SearchHit> searchWithMarkdown(const QString& root,
                                               const QString& agentsRoot,
                                               const QString& agentId,
                                               const QString& query,
                                               bool includeDaily,
                                               int maxResults)
    {
        QList<SearchHit> hits;
        if (maxResults <= 0)
            return hits;

        const QString agentPath = QDir(agentsRoot).filePath(agentId);
        const QStringList files = memorySourceFiles(agentPath, includeDaily, 14);
        const QString loweredQuery = query.toLower();
        for (const QString& path : files) {
            if (hits.size() >= maxResults)
                break;
            QFile file(path);
            if (!file.exists() || !file.open(QFile::ReadOnly | QFile::Text))
                continue;

            int lineNo = 0;
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).simplified();
                ++lineNo;
                if (line.isEmpty())
                    continue;
                if (!line.toLower().contains(loweredQuery))
                    continue;
                SearchHit hit;
                hit.agentId = agentId;
                hit.relativePath = QDir(root).relativeFilePath(path);
                hit.lineNo = lineNo;
                hit.snippet = line;
                hits.append(hit);
                if (hits.size() >= maxResults)
                    break;
            }
            file.close();
        }
        return hits;
    }

    static void appendCappedHits(QList<SearchHit>* target,
                                 const QList<SearchHit>& source,
                                 int maxSnippetChars,
                                 int maxResults)
    {
        if (!target || maxResults <= 0)
            return;
        for (const SearchHit& rawHit : source) {
            if (target->size() >= maxResults)
                break;
            SearchHit hit = rawHit;
            hit.snippet = clipSnippet(hit.snippet, maxSnippetChars);
            if (hit.relativePath.trimmed().isEmpty())
                continue;
            target->append(hit);
        }
    }

    static QString clipSnippet(const QString& input, int maxChars)
    {
        const QString text = input.simplified();
        if (text.size() <= maxChars)
            return text;
        return text.left(maxChars) + QStringLiteral("...");
    }
};

#endif // MEMORYTOOL_H
