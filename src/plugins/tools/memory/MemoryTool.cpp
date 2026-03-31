#include "MemoryTool.h"
#include "ToolSchemaSupport.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QtMath>
#include <QUuid>
#include <QVariant>
#include <algorithm>

namespace {

MemoryTool::WriteHandler& memoryWriteHandlerStorage()
{
    static MemoryTool::WriteHandler handler;
    return handler;
}

struct SemanticEntry {
    QString relPath;
    int lineNo = 0;
    QString content;
    QVector<double> vector;
};

const int kSemanticVectorDims = 48;
const double kSemanticFallbackThreshold = 0.18;
const int kSemanticCandidateLimit = 24;

QString hitKey(const QString& relPath, int lineNo)
{
    return relPath.trimmed() + QStringLiteral("#") + QString::number(lineNo);
}

QStringList semanticTokens(const QString& text)
{
    QStringList tokens;
    const QString lowered = text.simplified().toLower();
    if (lowered.isEmpty())
        return tokens;

    static const QRegularExpression regex(QStringLiteral("[\\p{L}\\p{N}]+"));
    QRegularExpressionMatchIterator it = regex.globalMatch(lowered);
    while (it.hasNext()) {
        const QString token = it.next().captured(0).trimmed();
        if (!token.isEmpty())
            tokens.append(token);
    }
    return tokens;
}

QVector<double> semanticVector(const QString& text)
{
    QVector<double> values(kSemanticVectorDims, 0.0);
    const QStringList tokens = semanticTokens(text);
    if (tokens.isEmpty())
        return values;

    for (const QString& token : tokens) {
        const uint bucketHash = qHash(token);
        const int bucket = static_cast<int>(bucketHash % static_cast<uint>(kSemanticVectorDims));
        const bool positive = (qHash(token, 17U) & 1U) == 0U;
        const double weight = qBound(0.5, static_cast<double>(token.size()) / 6.0, 1.8);
        values[bucket] += positive ? weight : -weight;
    }

    double norm = 0.0;
    for (double value : std::as_const(values))
        norm += value * value;
    if (norm <= 0.0)
        return values;

    norm = qSqrt(norm);
    for (double& value : values)
        value /= norm;
    return values;
}

QJsonArray vectorToJson(const QVector<double>& vector)
{
    QJsonArray arr;
    for (double value : vector)
        arr.append(value);
    return arr;
}

QVector<double> vectorFromJson(const QJsonArray& arr)
{
    QVector<double> values(kSemanticVectorDims, 0.0);
    const int count = qMin(arr.size(), kSemanticVectorDims);
    for (int i = 0; i < count; ++i)
        values[i] = arr.at(i).toDouble();
    return values;
}

double cosineSimilarity(const QVector<double>& lhs, const QVector<double>& rhs)
{
    if (lhs.isEmpty() || rhs.isEmpty())
        return 0.0;
    const int count = qMin(lhs.size(), rhs.size());
    double dot = 0.0;
    for (int i = 0; i < count; ++i)
        dot += lhs.at(i) * rhs.at(i);
    return qBound(0.0, dot, 1.0);
}

bool loadSemanticIndexFile(const QString& path, QList<SemanticEntry>* entries, QString* error)
{
    if (entries)
        entries->clear();
    if (error)
        error->clear();

    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = parseError.errorString();
        return false;
    }

    const QJsonArray rawEntries = doc.object().value(QStringLiteral("entries")).toArray();
    if (!entries)
        return true;

    for (const QJsonValue& value : rawEntries) {
        const QJsonObject obj = value.toObject();
        SemanticEntry entry;
        entry.relPath = obj.value(QStringLiteral("rel_path")).toString().trimmed();
        entry.lineNo = obj.value(QStringLiteral("line_no")).toInt();
        entry.content = obj.value(QStringLiteral("content")).toString().simplified();
        entry.vector = vectorFromJson(obj.value(QStringLiteral("vector")).toArray());
        if (entry.relPath.isEmpty() || entry.lineNo <= 0 || entry.content.isEmpty())
            continue;
        entries->append(entry);
    }
    return true;
}

bool saveSemanticIndexFile(const QString& path, const QList<SemanticEntry>& entries, QString* error)
{
    if (error)
        error->clear();

    QJsonArray arr;
    for (const SemanticEntry& entry : entries) {
        QJsonObject obj;
        obj.insert(QStringLiteral("rel_path"), entry.relPath);
        obj.insert(QStringLiteral("line_no"), entry.lineNo);
        obj.insert(QStringLiteral("content"), entry.content);
        obj.insert(QStringLiteral("vector"), vectorToJson(entry.vector));
        arr.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("indexed_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("entries"), arr);

    QSaveFile file(path);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建语义索引目录");
        return false;
    }
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

QHash<QString, double> semanticScoreMap(const QList<SemanticEntry>& entries, const QString& query, int maxResults)
{
    QHash<QString, double> scores;
    const QVector<double> queryVector = semanticVector(query);
    if (queryVector.isEmpty())
        return scores;

    QList<QPair<QString, double>> ranked;
    ranked.reserve(entries.size());
    for (const SemanticEntry& entry : entries) {
        const double score = cosineSimilarity(queryVector, entry.vector);
        if (score < kSemanticFallbackThreshold)
            continue;
        ranked.append(qMakePair(hitKey(entry.relPath, entry.lineNo), score));
    }

    std::stable_sort(ranked.begin(), ranked.end(), [](const QPair<QString, double>& a, const QPair<QString, double>& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0))
            return a.second > b.second;
        return a.first < b.first;
    });

    const int limit = qMin(maxResults, ranked.size());
    for (int i = 0; i < limit; ++i)
        scores.insert(ranked.at(i).first, ranked.at(i).second);
    return scores;
}

}

QList<Tool> MemoryTool::toolSchemas()
{
    return {
        makeToolSchema(
            QStringLiteral("memory_search"),
            QStringLiteral("搜索助手记忆文档（memory.md / user_view.md / daily memory）。"),
            QJsonObject {
                { QStringLiteral("query"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索关键词")) },
                { QStringLiteral("scope"), makePropertySchema(QStringLiteral("string"), QStringLiteral("检索范围：self(默认) / all")) },
                { QStringLiteral("agent_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定助手 ID")) },
                { QStringLiteral("include_daily"), makePropertySchema(QStringLiteral("boolean"), QStringLiteral("是否包含 daily 记忆日志（默认 true）")) },
                { QStringLiteral("max_results"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("最多返回命中条数（默认 10，范围 1-100）")) },
                { QStringLiteral("max_snippet_chars"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("每条命中摘要最大长度（默认 180）")) }
            },
            QStringList { QStringLiteral("query") }),
        makeToolSchema(
            QStringLiteral("memory_reindex"),
            QStringLiteral("重建助手记忆检索索引（SQLite FTS 派生索引）。"),
            QJsonObject {
                { QStringLiteral("scope"), makePropertySchema(QStringLiteral("string"), QStringLiteral("重建范围：self(默认) / all")) },
                { QStringLiteral("agent_id"), makePropertySchema(QStringLiteral("string"), QStringLiteral("指定助手 ID")) }
            }),
        makeToolSchema(
            QStringLiteral("memory_write"),
            QStringLiteral("主动写入当前助手的长期记忆（memory.md）。"),
            QJsonObject {
                { QStringLiteral("memory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要写入长期记忆的内容")) },
                { QStringLiteral("reason"), makePropertySchema(QStringLiteral("string"), QStringLiteral("写入原因（可选，用于审计说明）")) }
            },
            QStringList { QStringLiteral("memory") })
    };
}

QString MemoryTool::executeSearch(const QJsonObject& args)
{
    const QString query = args.value(QStringLiteral("query")).toString().trimmed();
    if (query.isEmpty())
        return QStringLiteral("错误: query 不能为空");

    const QString normalizedScope = normalizeScope(args);
    const QString agentId = resolveAgentIdArg(args);
    const bool includeDaily = args.value(QStringLiteral("include_daily")).toBool(true);
    const int maxResults = qBound(1, args.value(QStringLiteral("max_results")).toInt(10), 100);
    const int maxSnippetChars = qBound(60, args.value(QStringLiteral("max_snippet_chars")).toInt(180), 400);
    const int candidateLimitPerAgent = qBound(maxResults, qMax(maxResults * 3, 12), 48);

    const QString root = dataRootPath();
    const QString agentsRoot = agentsRootPath(root);
    QStringList targetAgents;
    QString resolveError;
    if (!resolveTargetAgents(agentsRoot, normalizedScope, agentId, &targetAgents, &resolveError)) {
        return resolveError;
    }

    QList<SearchHit> hits;
    QStringList warnings;
    bool usedSqlite = false;

    for (const QString& id : targetAgents) {
        const QString dbPath = sqliteIndexPath(agentsRoot, id);
        bool indexOk = false;
        if (!QFileInfo::exists(dbPath) || isIndexStale(agentsRoot, id, dbPath)) {
            QString rebuildError;
            int indexedRows = 0;
            indexOk = rebuildAgentIndex(agentsRoot, id, &indexedRows, &rebuildError);
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
            QList<SearchHit> sqliteHits = searchWithSqlite(
                agentsRoot,
                id,
                query,
                includeDaily,
                candidateLimitPerAgent,
                &sqliteError);
            if (!sqliteError.isEmpty()) {
                if (isLikelyCorruptedSqliteError(sqliteError)) {
                    int rebuiltRows = 0;
                    QString rebuildError;
                    if (rebuildAgentIndex(agentsRoot, id, &rebuiltRows, &rebuildError)) {
                        QString retryError;
                        QList<SearchHit> retryHits = searchWithSqlite(
                            agentsRoot,
                            id,
                            query,
                            includeDaily,
                            candidateLimitPerAgent,
                            &retryError);
                        if (retryError.isEmpty()) {
                            usedSqlite = true;
                            appendCappedHits(&hits, retryHits, maxSnippetChars);
                            warnings.append(
                                QStringLiteral("%1: 索引异常已自动修复并重试成功（rows=%2）")
                                    .arg(id, QString::number(rebuiltRows)));
                            continue;
                        }
                        warnings.append(QStringLiteral("%1: 索引修复后检索仍失败（%2），已回退 Markdown")
                                            .arg(id, retryError));
                    } else {
                        warnings.append(QStringLiteral("%1: 索引修复失败（%2），已回退 Markdown")
                                            .arg(id, rebuildError));
                    }
                } else {
                    warnings.append(QStringLiteral("%1: SQLite 检索失败，已回退 Markdown（%2）")
                                        .arg(id, sqliteError));
                }
            } else {
                usedSqlite = true;
                appendCappedHits(&hits, sqliteHits, maxSnippetChars);
                continue;
            }
        }

        QList<SearchHit> markdownHits = searchWithMarkdown(
            root,
            agentsRoot,
            id,
            query,
            includeDaily,
            candidateLimitPerAgent);
        appendCappedHits(&hits, markdownHits, maxSnippetChars);
    }

    sortAndTrimHits(&hits, maxResults);

    QString output;
    output += QStringLiteral("memory_search query: %1\n").arg(query);
    output += QStringLiteral("scope: %1\n").arg(normalizedScope);
    output += QStringLiteral("backend: %1\n")
                  .arg(usedSqlite ? QStringLiteral("sqlite_fts_ranked") : QStringLiteral("markdown_ranked"));
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
                         .arg(hit.agentId, hit.relativePath, QString::number(hit.lineNo), clipSnippet(hit.snippet, maxSnippetChars)));
    }
    output += QStringLiteral("\n命中列表:\n");
    output += lines.join(QStringLiteral("\n"));
    return output;
}

ToolResult MemoryTool::executeWrite(const QJsonObject& args)
{
    const QString memoryText = args.value(QStringLiteral("memory")).toString().trimmed();
    if (memoryText.isEmpty()) {
        return ToolResult(
            QStringLiteral("错误: memory 不能为空"),
            QStringLiteral("记忆写入失败：缺少 memory"),
            false);
    }

    const WriteHandler handler = memoryWriteHandlerStorage();
    if (!handler) {
        return ToolResult(
            QStringLiteral("错误: memory_write 当前不可用（未注册写入处理器）"),
            QStringLiteral("记忆写入当前不可用"),
            false);
    }

    return handler(args);
}

void MemoryTool::setWriteHandler(const WriteHandler& handler)
{
    memoryWriteHandlerStorage() = handler;
}

QString MemoryTool::executeRebuild(const QJsonObject& args)
{
    const QString normalizedScope = normalizeScope(args);
    const QString agentId = resolveAgentIdArg(args);
    const QString root = dataRootPath();
    const QString agentsRoot = agentsRootPath(root);

    QStringList targetAgents;
    QString resolveError;
    if (!resolveTargetAgents(agentsRoot, normalizedScope, agentId, &targetAgents, &resolveError)) {
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

QString MemoryTool::dataRootPath()
{
    return QDir::home().filePath(QStringLiteral(".tmagent"));
}

QString MemoryTool::agentsRootPath(const QString& root)
{
    return QDir(root).filePath(QStringLiteral("identities/agents"));
}

QString MemoryTool::normalizeScope(const QJsonObject& args)
{
    const QString scope = args.value(QStringLiteral("scope")).toString().trimmed().toLower();
    return scope.isEmpty() ? QStringLiteral("self") : scope;
}

QString MemoryTool::resolveAgentIdArg(const QJsonObject& args)
{
    QString agentId = args.value(QStringLiteral("agent_id")).toString().trimmed();
    if (agentId.isEmpty())
        agentId = args.value(QStringLiteral("_agent_id")).toString().trimmed();
    return agentId;
}

bool MemoryTool::resolveTargetAgents(const QString& agentsRoot, const QString& scope, const QString& agentId, QStringList* targetAgents, QString* error)
{
    if (targetAgents)
        targetAgents->clear();
    if (error)
        error->clear();

    QDir agentsDir(agentsRoot);
    if (!agentsDir.exists()) {
        if (error) {
            *error = QStringLiteral("错误: 记忆目录不存在: %1")
                         .arg(QDir::toNativeSeparators(agentsRoot));
        }
        return false;
    }

    if (scope == QLatin1String("all")) {
        if (targetAgents) {
            *targetAgents = agentsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
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

QString MemoryTool::sqliteIndexPath(const QString& agentsRoot, const QString& agentId)
{
    return QDir(QDir(agentsRoot).filePath(agentId)).filePath(QStringLiteral("memory_index.sqlite"));
}

QString MemoryTool::semanticIndexPath(const QString& agentsRoot, const QString& agentId)
{
    return QDir(QDir(agentsRoot).filePath(agentId)).filePath(QStringLiteral("memory_vector_index.json"));
}

QStringList MemoryTool::memorySourceFiles(const QString& agentPath, bool includeDaily, int maxDailyFiles)
{
    QStringList files;
    files << QDir(agentPath).filePath(QStringLiteral("memory.md"));
    files << QDir(agentPath).filePath(QStringLiteral("user_view.md"));

    if (!includeDaily)
        return files;

    QDir dailyDir(QDir(agentPath).filePath(QStringLiteral("memory")));
    if (!dailyDir.exists())
        return files;

    QStringList dailyFiles = dailyDir.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files, QDir::Name);
    if (maxDailyFiles > 0 && dailyFiles.size() > maxDailyFiles)
        dailyFiles = dailyFiles.mid(dailyFiles.size() - maxDailyFiles);
    for (const QString& daily : dailyFiles)
        files << dailyDir.filePath(daily);
    return files;
}

bool MemoryTool::isIndexStale(const QString& agentsRoot, const QString& agentId, const QString& indexPath)
{
    const QFileInfo indexInfo(indexPath);
    if (!indexInfo.exists())
        return true;
    const QFileInfo semanticInfo(semanticIndexPath(agentsRoot, agentId));
    if (!semanticInfo.exists())
        return true;

    const QDateTime indexTime = indexInfo.lastModified();
    const QDateTime semanticTime = semanticInfo.lastModified();
    const QString agentPath = QDir(agentsRoot).filePath(agentId);
    const QStringList files = memorySourceFiles(agentPath, true, -1);
    for (const QString& path : files) {
        const QFileInfo fi(path);
        if (!fi.exists())
            continue;
        if (fi.lastModified() > indexTime || fi.lastModified() > semanticTime)
            return true;
    }
    return false;
}

bool MemoryTool::rebuildAgentIndex(const QString& agentsRoot, const QString& agentId, int* indexedRows, QString* error)
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
    const QString semanticPath = semanticIndexPath(agentsRoot, agentId);
    QString lastError;
    int rowCount = 0;
    QList<SemanticEntry> semanticEntries;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const QString connectionName = QStringLiteral("memory_index_build_%1_%2")
                                           .arg(agentId, QUuid::createUuid().toString(QUuid::WithoutBraces));
        bool ok = false;
        semanticEntries.clear();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(dbPath);
            if (!db.open()) {
                lastError = db.lastError().text();
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
                    lastError = schema.lastError().text();
                    ok = false;
                }
            }

            if (ok && !db.transaction()) {
                lastError = db.lastError().text();
                ok = false;
            }

            rowCount = 0;
            if (ok) {
                QSqlQuery clear(db);
                if (!clear.exec(QStringLiteral("DELETE FROM memory_fts"))
                    || !clear.exec(QStringLiteral("DELETE FROM memory_meta"))) {
                    lastError = clear.lastError().text();
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
                        insert.bindValue(0, QVariant(relPath));
                        insert.bindValue(1, QVariant(lineNo));
                        insert.bindValue(2, QVariant(line));
                        if (!insert.exec()) {
                            lastError = insert.lastError().text();
                            ok = false;
                            break;
                        }
                        SemanticEntry semanticEntry;
                        semanticEntry.relPath = relPath;
                        semanticEntry.lineNo = lineNo;
                        semanticEntry.content = line;
                        semanticEntry.vector = semanticVector(line);
                        semanticEntries.append(semanticEntry);
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
                    meta.bindValue(0, QVariant(key));
                    meta.bindValue(1, QVariant(value));
                    return meta.exec();
                };
                if (!upsertMeta(QStringLiteral("indexed_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                    || !upsertMeta(QStringLiteral("row_count"), QString::number(rowCount))) {
                    lastError = meta.lastError().text();
                    ok = false;
                }
            }

            if (ok) {
                if (!db.commit()) {
                    lastError = db.lastError().text();
                    ok = false;
                }
            } else {
                db.rollback();
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);

        if (ok) {
            QString semanticError;
            if (!saveSemanticIndexFile(semanticPath, semanticEntries, &semanticError)) {
                if (error)
                    *error = semanticError;
                return false;
            }
            if (indexedRows)
                *indexedRows = rowCount;
            return true;
        }

        if (attempt == 0 && isLikelyCorruptedSqliteError(lastError)) {
            QFile::remove(dbPath);
            continue;
        }
        break;
    }

    if (error)
        *error = lastError;
    return false;
}

QList<MemoryTool::SearchHit> MemoryTool::searchWithSqlite(const QString& agentsRoot, const QString& agentId, const QString& query, bool includeDaily, int maxResults, QString* error)
{
    QList<SearchHit> hits;
    if (error)
        error->clear();
    if (maxResults <= 0)
        return hits;

    const QString dbPath = sqliteIndexPath(agentsRoot, agentId);
    if (!QFileInfo::exists(dbPath))
        return hits;
    const QString semanticPath = semanticIndexPath(agentsRoot, agentId);
    QList<SemanticEntry> semanticEntries;
    QString semanticError;
    const bool semanticLoaded = loadSemanticIndexFile(semanticPath, &semanticEntries, &semanticError);
    if (!semanticLoaded && error && error->trimmed().isEmpty())
        *error = QStringLiteral("semantic index load failed: %1").arg(semanticError);
    const QHash<QString, double> semanticScores = semanticScoreMap(semanticEntries, query, qMax(maxResults * 3, kSemanticCandidateLimit));

    const QString connectionName = QStringLiteral("memory_index_search_%1_%2")
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
                QString sql = QStringLiteral(
                    "SELECT rel_path, line_no, content, bm25(memory_fts) AS rank "
                    "FROM memory_fts WHERE memory_fts MATCH ?");
                if (!includeDaily)
                    sql += QStringLiteral(" AND rel_path NOT LIKE 'memory/%'");
                sql += QStringLiteral(" ORDER BY rank ASC, line_no ASC");
                sql += QStringLiteral(" LIMIT ?");
                queryStmt.prepare(sql);
                queryStmt.bindValue(0, QVariant(matchExpr));
                queryStmt.bindValue(1, QVariant(maxResults));
                if (!queryStmt.exec()) {
                    ok = false;
                } else {
                    fillHitsFromQuery(agentId, query, queryStmt, true, &hits);
                }

                if (ok && !semanticScores.isEmpty()) {
                    for (SearchHit& hit : hits) {
                        const QString key = hitKey(hit.sourceRelativePath, hit.lineNo);
                        hit.vectorScore = semanticScores.value(key, 0.0);
                        hit.finalScore = finalScoreForHit(hit);
                    }
                }

                if (!ok || hits.isEmpty()) {
                    QSqlQuery likeStmt(db);
                    QStringList tokens = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
                    if (tokens.isEmpty())
                        tokens.append(query.trimmed());

                    QStringList clauses;
                    for (int i = 0; i < tokens.size(); ++i)
                        clauses.append(QStringLiteral("content LIKE ?"));

                    QString likeSql = QStringLiteral(
                        "SELECT rel_path, line_no, content FROM memory_fts WHERE %1")
                                          .arg(clauses.join(QStringLiteral(" AND ")));
                    if (!includeDaily)
                        likeSql += QStringLiteral(" AND rel_path NOT LIKE 'memory/%'");
                    likeSql += QStringLiteral(" ORDER BY line_no ASC LIMIT ?");
                    likeStmt.prepare(likeSql);
                    for (const QString& token : std::as_const(tokens))
                        likeStmt.addBindValue(QVariant(QStringLiteral("%") + token + QStringLiteral("%")));
                    likeStmt.addBindValue(QVariant(maxResults));
                    if (!likeStmt.exec()) {
                        if (error)
                            *error = likeStmt.lastError().text();
                        ok = false;
                    } else {
                        hits.clear();
                        fillHitsFromQuery(agentId, query, likeStmt, false, &hits);
                        ok = true;
                        if (!semanticScores.isEmpty()) {
                            for (SearchHit& hit : hits) {
                                const QString key = hitKey(hit.sourceRelativePath, hit.lineNo);
                                hit.vectorScore = semanticScores.value(key, 0.0);
                                hit.finalScore = finalScoreForHit(hit);
                            }
                        }
                    }
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    if (ok && hits.isEmpty() && !semanticScores.isEmpty()) {
        QList<QPair<QString, double>> ranked;
        for (auto it = semanticScores.constBegin(); it != semanticScores.constEnd(); ++it)
            ranked.append(qMakePair(it.key(), it.value()));
        std::stable_sort(ranked.begin(), ranked.end(), [](const QPair<QString, double>& a, const QPair<QString, double>& b) {
            if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0))
                return a.second > b.second;
            return a.first < b.first;
        });

        const int limit = qMin(kSemanticCandidateLimit, qMax(maxResults, 6));
        for (int i = 0; i < ranked.size() && i < limit; ++i) {
            const QStringList parts = ranked.at(i).first.split(QLatin1Char('#'));
            if (parts.size() != 2)
                continue;
            const QString relPath = parts.at(0);
            const int lineNo = parts.at(1).toInt();
            for (const SemanticEntry& entry : semanticEntries) {
                if (entry.relPath != relPath || entry.lineNo != lineNo)
                    continue;
                SearchHit hit;
                hit.agentId = agentId;
                hit.sourceRelativePath = relPath;
                hit.sourceKind = sourceKindForRelativePath(relPath);
                hit.relativePath = QStringLiteral("identities/agents/%1/%2").arg(agentId, relPath);
                hit.lineNo = lineNo;
                hit.snippet = entry.content;
                hit.textScore = 0.18;
                hit.vectorScore = ranked.at(i).second;
                hit.finalScore = finalScoreForHit(hit);
                hits.append(hit);
                break;
            }
        }
    }

    if (!ok)
        hits.clear();
    return hits;
}

void MemoryTool::fillHitsFromQuery(const QString& agentId, const QString& query, QSqlQuery& queryStmt, bool hasSqliteRank, QList<SearchHit>* hits)
{
    if (!hits)
        return;
    while (queryStmt.next()) {
        SearchHit hit;
        hit.agentId = agentId;
        const QString relPath = queryStmt.value(0).toString().trimmed();
        hit.sourceRelativePath = relPath;
        hit.sourceKind = sourceKindForRelativePath(relPath);
        hit.relativePath = QStringLiteral("identities/agents/%1/%2").arg(agentId, relPath);
        hit.lineNo = queryStmt.value(1).toInt();
        hit.snippet = queryStmt.value(2).toString().simplified();
        hit.textScore = hasSqliteRank
            ? textScoreFromSqliteRank(queryStmt.value(3))
            : textScoreFromSnippetMatch(hit.snippet, query);
        hit.vectorScore = 0.0;
        hit.finalScore = finalScoreForHit(hit);
        hits->append(hit);
    }
}

QString MemoryTool::buildMatchExpr(const QString& raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return QStringLiteral("\"\"");
    QString escaped = text;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    const QString exactPhrase = QStringLiteral("\"%1\"").arg(escaped);

    QStringList tokens = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() <= 1)
        return exactPhrase;

    QStringList tokenExprs;
    for (QString token : tokens) {
        token = token.trimmed();
        if (token.isEmpty())
            continue;
        token.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        tokenExprs.append(QStringLiteral("\"%1\"").arg(token));
    }
    if (tokenExprs.size() <= 1)
        return exactPhrase;
    return QStringLiteral("%1 OR (%2)").arg(exactPhrase, tokenExprs.join(QStringLiteral(" AND ")));
}

bool MemoryTool::isLikelyCorruptedSqliteError(const QString& errorText)
{
    const QString lower = errorText.toLower();
    return lower.contains(QStringLiteral("malformed"))
        || lower.contains(QStringLiteral("not a database"))
        || lower.contains(QStringLiteral("file is encrypted"))
        || lower.contains(QStringLiteral("database disk image is malformed"));
}

QList<MemoryTool::SearchHit> MemoryTool::searchWithMarkdown(const QString& root, const QString& agentsRoot, const QString& agentId, const QString& query, bool includeDaily, int maxResults)
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
            hit.sourceRelativePath = QDir(agentPath).relativeFilePath(path);
            hit.sourceKind = sourceKindForRelativePath(hit.sourceRelativePath);
            hit.relativePath = QDir(root).relativeFilePath(path);
            hit.lineNo = lineNo;
            hit.snippet = line;
            hit.textScore = textScoreFromSnippetMatch(line, query);
            hit.finalScore = finalScoreForHit(hit);
            hits.append(hit);
        }
        file.close();
    }
    sortAndTrimHits(&hits, maxResults);
    return hits;
}

void MemoryTool::appendCappedHits(QList<SearchHit>* target, const QList<SearchHit>& source, int maxSnippetChars)
{
    if (!target)
        return;
    for (const SearchHit& rawHit : source) {
        SearchHit hit = rawHit;
        hit.snippet = clipSnippet(hit.snippet, maxSnippetChars);
        if (hit.relativePath.trimmed().isEmpty())
            continue;
        target->append(hit);
    }
}

QString MemoryTool::sourceKindForRelativePath(const QString& relativePath)
{
    const QString normalized = QDir::fromNativeSeparators(relativePath.trimmed());
    if (normalized == QLatin1String("memory.md"))
        return QStringLiteral("long_term");
    if (normalized == QLatin1String("user_view.md"))
        return QStringLiteral("user_view");
    if (normalized.startsWith(QStringLiteral("memory/")) && normalized.endsWith(QStringLiteral(".md")))
        return QStringLiteral("daily");
    return QStringLiteral("other");
}

double MemoryTool::textScoreFromSqliteRank(const QVariant& rankValue)
{
    bool ok = false;
    const double raw = rankValue.toDouble(&ok);
    if (!ok)
        return 0.5;
    return 1.0 / (1.0 + qExp(raw));
}

double MemoryTool::textScoreFromSnippetMatch(const QString& snippet, const QString& query)
{
    const QString loweredSnippet = snippet.simplified().toLower();
    const QString loweredQuery = query.simplified().toLower();
    if (loweredSnippet.isEmpty() || loweredQuery.isEmpty())
        return 0.0;

    double score = loweredSnippet.contains(loweredQuery) ? 0.85 : 0.25;

    const QStringList tokens = loweredQuery.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int matchedTokens = 0;
    for (const QString& token : tokens) {
        if (!token.isEmpty() && loweredSnippet.contains(token))
            ++matchedTokens;
    }
    if (!tokens.isEmpty())
        score += (static_cast<double>(matchedTokens) / static_cast<double>(tokens.size())) * 0.3;
    if (matchedTokens == tokens.size() && matchedTokens > 1)
        score += 0.1;

    return qBound(0.0, score, 1.0);
}

int MemoryTool::recencyBonusForRelativePath(const QString& relativePath)
{
    const QString normalized = QDir::fromNativeSeparators(relativePath.trimmed());
    if (!normalized.startsWith(QStringLiteral("memory/")) || !normalized.endsWith(QStringLiteral(".md")))
        return 0;

    const QFileInfo info(normalized);
    const QDate day = QDate::fromString(info.completeBaseName(), Qt::ISODate);
    if (!day.isValid())
        return 0;

    const int daysAgo = day.daysTo(QDate::currentDate());
    if (daysAgo < 0)
        return 18;
    return qMax(0, 18 - qMin(daysAgo, 18));
}

bool MemoryTool::isLowValueMetadataLine(const QString& snippet)
{
    const QString lowered = snippet.trimmed().toLower();
    return lowered.startsWith(QStringLiteral("- fp:"))
        || lowered.startsWith(QStringLiteral("- source_"))
        || lowered.startsWith(QStringLiteral("- reflected:"))
        || lowered.startsWith(QStringLiteral("## "));
}

double MemoryTool::finalScoreForHit(const SearchHit& hit)
{
    const double retrievalScore = qBound(
        0.0,
        hit.vectorScore > 0.0
            ? (hit.textScore * 0.72 + hit.vectorScore * 0.28)
            : hit.textScore,
        1.0);
    double score = retrievalScore * 100.0;

    if (hit.sourceKind == QLatin1String("long_term"))
        score += 26.0;
    else if (hit.sourceKind == QLatin1String("user_view"))
        score += 18.0;
    else if (hit.sourceKind == QLatin1String("daily"))
        score += 8.0 + recencyBonusForRelativePath(hit.sourceRelativePath);

    const QString snippet = hit.snippet.trimmed();
    if (snippet.startsWith(QStringLiteral("- memory:")))
        score += 10.0;
    if (snippet.contains(QStringLiteral("反思提炼：")))
        score += 8.0;
    if (isLowValueMetadataLine(snippet))
        score -= 20.0;

    score += qMin(6.0, static_cast<double>(snippet.size()) / 48.0);
    return score;
}

void MemoryTool::sortAndTrimHits(QList<SearchHit>* hits, int maxResults)
{
    if (!hits)
        return;

    std::stable_sort(hits->begin(), hits->end(), [](const SearchHit& a, const SearchHit& b) {
        if (!qFuzzyCompare(a.finalScore + 1.0, b.finalScore + 1.0))
            return a.finalScore > b.finalScore;
        if (a.sourceRelativePath != b.sourceRelativePath)
            return a.sourceRelativePath < b.sourceRelativePath;
        if (a.lineNo != b.lineNo)
            return a.lineNo < b.lineNo;
        return a.snippet < b.snippet;
    });

    QSet<QString> seen;
    QList<SearchHit> deduped;
    deduped.reserve(hits->size());
    for (const SearchHit& hit : std::as_const(*hits)) {
        const QString key = QStringLiteral("%1|%2|%3|%4")
                                .arg(hit.agentId, hit.sourceRelativePath, QString::number(hit.lineNo), hit.snippet);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        deduped.append(hit);
        if (maxResults > 0 && deduped.size() >= maxResults)
            break;
    }
    *hits = deduped;
}

QString MemoryTool::clipSnippet(const QString& input, int maxChars)
{
    const QString text = input.simplified();
    if (text.size() <= maxChars)
        return text;
    return text.left(maxChars) + QStringLiteral("...");
}
