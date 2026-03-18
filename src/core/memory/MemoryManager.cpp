#include "MemoryManager.h"

#include "MemoryDocument.h"
#include "core/model/Identity.h"
#include "core/model/IdentityProfile.h"
#include "core/persistence/ChatPersistenceService.h"
#include "llm/ModelFactory.h"
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {
const int kSoulReadChars = 1600;
const int kIdentityReadChars = 1400;
const int kUserReadChars = 1200;
const int kSharedWorkReadChars = 1000;
const int kUserViewReadChars = 1400;
const int kLongMemoryReadChars = 2200;
const int kDailyReadChars = 900;

QString normalizeMemoryText(const QString& text)
{
    QString normalized = text;
    normalized.replace(QLatin1Char('\r'), QLatin1Char(' '));
    normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
    normalized = normalized.simplified();
    return normalized;
}

QString makeMemoryFingerprint(const QString& text)
{
    const QByteArray digest = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString::fromLatin1(digest.left(12));
}

bool containsAny(const QString& source, const QStringList& keywords)
{
    for (const QString& keyword : keywords) {
        if (keyword.isEmpty())
            continue;
        if (source.contains(keyword, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool isManualRememberRequested(const QString& userText)
{
    const QString text = userText.trimmed();
    if (text.isEmpty())
        return false;
    const QString lower = text.toLower();
    if (lower.contains(QStringLiteral("不要记住"))
        || lower.contains(QStringLiteral("不用记住"))
        || lower.contains(QStringLiteral("不需要记住"))
        || lower.contains(QStringLiteral("don't remember"))) {
        return false;
    }

    const QStringList triggers = {
        QStringLiteral("记住"),
        QStringLiteral("记一下"),
        QStringLiteral("请记住"),
        QStringLiteral("记在"),
        QStringLiteral("remember this"),
        QStringLiteral("please remember"),
        QStringLiteral("keep this in mind")
    };
    return containsAny(lower, triggers);
}

bool looksLikeStableUserFact(const QString& userText)
{
    const QString normalized = normalizeMemoryText(userText);
    if (normalized.isEmpty())
        return false;
    if (normalized.contains(QLatin1Char('?')) || normalized.contains(QStringLiteral("？")))
        return false;

    const QStringList keywords = {
        QStringLiteral("我是"),
        QStringLiteral("我叫"),
        QStringLiteral("请叫我"),
        QStringLiteral("称呼我"),
        QStringLiteral("我希望"),
        QStringLiteral("我偏好"),
        QStringLiteral("我倾向"),
        QStringLiteral("我习惯"),
        QStringLiteral("默认"),
        QStringLiteral("长期"),
        QStringLiteral("一直"),
        QStringLiteral("不喜欢"),
        QStringLiteral("喜欢"),
        QStringLiteral("岗位"),
        QStringLiteral("角色"),
        QStringLiteral("企业文化"),
        QStringLiteral("制度"),
        QStringLiteral("my name is"),
        QStringLiteral("call me"),
        QStringLiteral("i prefer"),
        QStringLiteral("i like"),
        QStringLiteral("i don't like"),
        QStringLiteral("default"),
        QStringLiteral("for future")
    };
    return containsAny(normalized.toLower(), keywords);
}

QStringList buildLongTermCandidates(const QString& userText, const QString& assistantText)
{
    QStringList candidates;
    const QString userLine = normalizeMemoryText(userText);
    if (userLine.isEmpty())
        return candidates;

    const bool manual = isManualRememberRequested(userText);
    if (manual) {
        candidates.append(QStringLiteral("用户明确要求记住：%1").arg(userLine));
        const QString assistantLine = normalizeMemoryText(assistantText);
        if (!assistantLine.isEmpty())
            candidates.append(QStringLiteral("助手对该记忆请求的回应：%1").arg(assistantLine));
        return candidates;
    }

    if (looksLikeStableUserFact(userText))
        candidates.append(QStringLiteral("用户长期偏好/设定：%1").arg(userLine));
    return candidates;
}

bool policyBool(const QJsonObject& policy, const QString& key, bool defaultValue)
{
    if (policy.contains(key))
        return policy.value(key).toBool(defaultValue);
    const QJsonObject rules = policy.value(QStringLiteral("memory_rules")).toObject();
    if (rules.contains(key))
        return rules.value(key).toBool(defaultValue);
    return defaultValue;
}

int policyInt(const QJsonObject& policy, const QString& key, int defaultValue)
{
    if (policy.contains(key))
        return policy.value(key).toInt(defaultValue);
    const QJsonObject rules = policy.value(QStringLiteral("memory_rules")).toObject();
    if (rules.contains(key))
        return rules.value(key).toInt(defaultValue);
    return defaultValue;
}

QStringList memorySourceFilesForIndex(const QString& agentRootPath)
{
    QStringList files;
    if (agentRootPath.trimmed().isEmpty())
        return files;

    files << QDir(agentRootPath).filePath(QStringLiteral("memory.md"));
    files << QDir(agentRootPath).filePath(QStringLiteral("user_view.md"));

    QDir dailyDir(QDir(agentRootPath).filePath(QStringLiteral("memory")));
    if (dailyDir.exists()) {
        const QStringList dailyFiles = dailyDir.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files, QDir::Name);
        for (const QString& file : dailyFiles)
            files << dailyDir.filePath(file);
    }

    return files;
}

QStringList extractUserSignalsFromDailyContent(const QString& content, int maxSignals)
{
    QStringList userSignals;
    if (maxSignals <= 0)
        return userSignals;

    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        QString signal;
        if (line.startsWith(QStringLiteral("- user:"))) {
            signal = line.mid(QStringLiteral("- user:").size()).trimmed();
        } else if (line.startsWith(QStringLiteral("- user_signal:"))) {
            signal = line.mid(QStringLiteral("- user_signal:").size()).trimmed();
        }

        if (signal.isEmpty() || signal == QLatin1String("(empty)"))
            continue;
        userSignals.append(signal);
        if (userSignals.size() >= maxSignals)
            break;
    }
    return userSignals;
}

QString qualityLevelFromScore(int score)
{
    if (score >= 85)
        return QStringLiteral("excellent");
    if (score >= 70)
        return QStringLiteral("good");
    if (score >= 50)
        return QStringLiteral("fair");
    return QStringLiteral("poor");
}

bool isLikelyCorruptedSqliteError(const QString& errorText)
{
    const QString lower = errorText.toLower();
    return lower.contains(QStringLiteral("malformed"))
        || lower.contains(QStringLiteral("not a database"))
        || lower.contains(QStringLiteral("file is encrypted"))
        || lower.contains(QStringLiteral("database disk image is malformed"));
}

bool buildSearchIndexOnce(const QString& dbPath, const QString& agentRootPath, const QString& connectionName, int* rowCountOut, QString* error)
{
    if (rowCountOut)
        *rowCountOut = 0;
    if (error)
        error->clear();

    bool ok = false;
    int rowCount = 0;
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
            if (!schema.exec(QStringLiteral(
                    "CREATE TABLE IF NOT EXISTS memory_meta (key TEXT PRIMARY KEY, value TEXT)"))
                || !schema.exec(QStringLiteral(
                    "CREATE VIRTUAL TABLE IF NOT EXISTS memory_fts USING fts5(rel_path, line_no UNINDEXED, content, tokenize='unicode61')"))) {
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

        if (ok) {
            QSqlQuery clear(db);
            if (!clear.exec(QStringLiteral("DELETE FROM memory_fts"))
                || !clear.exec(QStringLiteral("DELETE FROM memory_meta"))) {
                if (error)
                    *error = clear.lastError().text();
                ok = false;
            }
        }

        if (ok) {
            QSqlQuery insert(db);
            insert.prepare(
                QStringLiteral("INSERT INTO memory_fts(rel_path, line_no, content) VALUES (?, ?, ?)"));

            const QStringList sourceFiles = memorySourceFilesForIndex(agentRootPath);
            for (const QString& sourcePath : sourceFiles) {
                QFile file(sourcePath);
                if (!file.exists() || !file.open(QFile::ReadOnly | QFile::Text))
                    continue;

                const QString relPath = QDir(agentRootPath).relativeFilePath(sourcePath);
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
            meta.prepare(QStringLiteral("INSERT OR REPLACE INTO memory_meta(key, value) VALUES (?, ?)"));
            auto upsertMeta = [&meta](const QString& key, const QString& value) {
                meta.bindValue(0, QVariant(key));
                meta.bindValue(1, QVariant(value));
                return meta.exec();
            };
            if (!upsertMeta(QStringLiteral("indexed_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                || !upsertMeta(QStringLiteral("row_count"), QString::number(rowCount))) {
                if (error)
                    *error = meta.lastError().text();
                ok = false;
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
    if (ok && rowCountOut)
        *rowCountOut = rowCount;
    return ok;
}
}

MemoryManager::MemoryManager(ChatPersistenceService* persistence)
    : m_persistence(persistence)
{
}

void MemoryManager::setPersistence(ChatPersistenceService* persistence)
{
    m_persistence = persistence;
}

QString MemoryManager::dataRootPath() const
{
    return m_persistence ? m_persistence->dataRootPath() : QString();
}

QString MemoryManager::agentsRootPath() const
{
    return m_persistence ? m_persistence->agentsDirPath() : QString();
}

QString MemoryManager::agentDirPath(const QString& agentId) const
{
    if (agentId.trimmed().isEmpty())
        return QString();
    return QDir(agentsRootPath()).filePath(agentId.trimmed());
}

QString MemoryManager::dailyMemoryDirPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("memory"));
}

QString MemoryManager::userDocPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("user.md"));
}

QString MemoryManager::sharedWorkDocPath() const
{
    return QDir(dataRootPath()).filePath(QStringLiteral("shared_work.md"));
}

QString MemoryManager::policyPath() const
{
    return m_persistence ? m_persistence->memoryPolicyPath() : QString();
}

QString MemoryManager::soulDocPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("soul.md"));
}

QString MemoryManager::identityDocPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("identity.md"));
}

QString MemoryManager::longTermMemoryDocPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("memory.md"));
}

QString MemoryManager::userViewDocPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("user_view.md"));
}

QString MemoryManager::dailyMemoryPath(const QString& agentId) const
{
    const QString dateStr = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    return QDir(dailyMemoryDirPath(agentId)).filePath(dateStr + QStringLiteral(".md"));
}

QString MemoryManager::memoryIndexPath(const QString& agentId) const
{
    return QDir(agentDirPath(agentId)).filePath(QStringLiteral("memory_index.sqlite"));
}

QStringList MemoryManager::latestDailyMemoryPaths(const QString& agentId, int maxFiles) const
{
    QStringList result;
    if (maxFiles <= 0)
        return result;

    QDir dir(dailyMemoryDirPath(agentId));
    if (!dir.exists())
        return result;

    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString& file : files) {
        result.append(dir.filePath(file));
        if (result.size() >= maxFiles)
            break;
    }
    return result;
}

QString MemoryManager::buildUserTemplate() const
{
    return QStringLiteral(
        "# User Profile\n\n"
        "- preferred_name:\n"
        "- communication_style:\n"
        "- long_term_preferences:\n");
}

QString MemoryManager::buildSharedWorkTemplate() const
{
    return QStringLiteral(
        "# Shared Workboard\n\n"
        "> Shared current work context across agents. Updated only by memory steward.\n\n");
}

QString MemoryManager::buildPolicyTemplate() const
{
    return QStringLiteral(
        "{\n"
        "  \"memory_steward_agent_id\": \"\",\n"
        "  \"memory_rules\": {\n"
        "    \"auto_extract_enabled\": true,\n"
        "    \"min_user_chars_for_extract\": 12,\n"
        "    \"max_long_memory_candidates_per_turn\": 3,\n"
        "    \"reflect_enabled\": true,\n"
        "    \"reflect_every_n_turns\": 8,\n"
        "    \"reflect_max_candidates_per_run\": 4,\n"
        "    \"reflect_scan_daily_files\": 7\n"
        "  },\n"
        "  \"note\": \"Set memory_steward_agent_id to one existing agent id to maintain shared_work.md\"\n"
        "}\n");
}

QString MemoryManager::buildSoulTemplate(const Identity* agent) const
{
    const QString agentName = agent ? agent->name().trimmed() : QStringLiteral("Unnamed Agent");
    QString prompt;
    if (agent && agent->profile())
        prompt = agent->profile()->systemPrompt().trimmed();
    if (prompt.isEmpty())
        prompt = QStringLiteral("Follow safety boundaries and provide concise, evidence-based help.");

    return QStringLiteral(
               "# Soul\n\n"
               "- agent_id: `%1`\n"
               "- name: %2\n"
               "- initialized_at: %3\n\n"
               "## Core Behavior\n\n"
               "%4\n")
        .arg(agent ? agent->id() : QString())
        .arg(agentName)
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
        .arg(prompt);
}

QString MemoryManager::buildIdentityTemplate(const Identity* agent) const
{
    QString role = QStringLiteral("unspecified");
    QString modelKey;
    QString avatar;
    int recursionDepth = 3;
    if (agent) {
        avatar = agent->avatar().trimmed();
        if (agent->profile()) {
            role = agent->profile()->description().trimmed();
            if (role.isEmpty())
                role = QStringLiteral("unspecified");
            const LLMConfig cfg = agent->profile()->llmConfig();
            if (ModelFactory* factory = ModelFactory::instance())
                modelKey = factory->resolveModelId(cfg).trimmed();
            if (modelKey.isEmpty())
                modelKey = cfg.selectedModelId.trimmed();
            recursionDepth = agent->profile()->recursionDepth();
        }
    }
    if (modelKey.trimmed().isEmpty())
        modelKey = QStringLiteral("unspecified");

    return QStringLiteral(
               "# Identity Card\n\n"
               "- agent_id: `%1`\n"
               "- name: %2\n"
               "- role: %3\n"
               "- model: %4\n"
               "- recursion_depth: %5\n"
               "- avatar: %6\n")
        .arg(agent ? agent->id() : QString())
        .arg(agent ? agent->name().trimmed() : QStringLiteral("Unnamed Agent"))
        .arg(role)
        .arg(modelKey)
        .arg(recursionDepth)
        .arg(avatar.isEmpty() ? QStringLiteral("(none)") : avatar);
}

QString MemoryManager::buildLongTermMemoryTemplate() const
{
    return QStringLiteral(
        "# Long-term Memory\n\n"
        "> Stable facts, preferences, and reusable methods.\n\n");
}

QString MemoryManager::buildUserViewTemplate(const Identity* agent) const
{
    return QStringLiteral(
               "# User View (%1)\n\n"
               "> This is how this agent understands the user.\n\n")
        .arg(agent ? agent->name().trimmed() : QStringLiteral("agent"));
}

bool MemoryManager::ensureAgentMemoryDirs(const QString& agentId, QString* error) const
{
    const QString trimmed = agentId.trimmed();
    if (trimmed.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent id is empty");
        return false;
    }

    const QString agentDir = agentDirPath(trimmed);
    const QString dailyDir = dailyMemoryDirPath(trimmed);
    if (!QDir().mkpath(agentDir) || !QDir().mkpath(dailyDir)) {
        if (error)
            *error = QStringLiteral("failed to create agent memory dirs: %1").arg(trimmed);
        return false;
    }
    return true;
}

bool MemoryManager::writeIfMissing(const QString& filePath, const QString& content, QString* error) const
{
    MemoryDocument doc(filePath);
    if (doc.exists())
        return true;
    return doc.writeAtomic(content, error);
}

bool MemoryManager::writeIfChanged(const QString& filePath, const QString& content, QString* error) const
{
    MemoryDocument doc(filePath);
    bool ok = false;
    const QString current = doc.read(&ok);
    if (!ok) {
        if (error && error->trimmed().isEmpty())
            *error = QStringLiteral("failed to read memory file: %1").arg(filePath);
        return false;
    }
    if (doc.exists() && current == content)
        return true;
    return doc.writeAtomic(content, error);
}

bool MemoryManager::ensureUserMemoryDocument(QString* error) const
{
    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }
    if (!writeIfMissing(userDocPath(), buildUserTemplate(), error))
        return false;
    if (!writeIfMissing(sharedWorkDocPath(), buildSharedWorkTemplate(), error))
        return false;
    if (!writeIfMissing(policyPath(), buildPolicyTemplate(), error))
        return false;
    return true;
}

bool MemoryManager::initializeForAgent(const Identity* agent, QString* error) const
{
    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }
    if (!agent || !agent->isAgent()) {
        if (error)
            *error = QStringLiteral("invalid agent");
        return false;
    }

    if (!ensureUserMemoryDocument(error))
        return false;
    if (!ensureAgentMemoryDirs(agent->id(), error))
        return false;
    if (!writeIfMissing(soulDocPath(agent->id()), buildSoulTemplate(agent), error))
        return false;
    // identity.md 允许随配置变化自动刷新；soul.md 仍保持首次初始化时间戳不变。
    if (!writeIfChanged(identityDocPath(agent->id()), buildIdentityTemplate(agent), error))
        return false;
    if (!writeIfMissing(longTermMemoryDocPath(agent->id()), buildLongTermMemoryTemplate(), error))
        return false;
    if (!writeIfMissing(userViewDocPath(agent->id()), buildUserViewTemplate(agent), error))
        return false;
    return true;
}

bool MemoryManager::removeAgentMemory(const QString& agentId, QString* error) const
{
    const QString dirPath = agentDirPath(agentId);
    if (dirPath.trimmed().isEmpty())
        return true;
    QFileInfo info(dirPath);
    if (!info.exists())
        return true;

    // 关闭该 agent 目录下 SQLite 文件持有的所有数据库连接，
    // 否则 Windows 上文件被锁会导致 removeRecursively 失败。
    {
        const QStringList allConns = QSqlDatabase::connectionNames();
        const QString trimmedId = agentId.trimmed();
        for (const QString& conn : allConns) {
            if (conn.contains(trimmedId)) {
                {
                    QSqlDatabase db = QSqlDatabase::database(conn, false);
                    if (db.isOpen())
                        db.close();
                }
                QSqlDatabase::removeDatabase(conn);
            }
        }
    }

    if (!QDir(dirPath).removeRecursively()) {
        if (error)
            *error = QStringLiteral("failed to remove agent memory dir: %1").arg(dirPath);
        return false;
    }
    return true;
}

bool MemoryManager::rebuildSearchIndex(const QString& agentId, QJsonObject* metadata, QString* error) const
{
    if (metadata)
        *metadata = QJsonObject();
    if (error)
        error->clear();

    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent id is empty");
        return false;
    }
    if (!ensureAgentMemoryDirs(trimmedAgentId, error))
        return false;

    const QString agentRootPath = agentDirPath(trimmedAgentId);
    const QString dbPath = memoryIndexPath(trimmedAgentId);
    bool ok = false;
    int rowCount = 0;
    int attempts = 0;
    bool recoveredFromCorruption = false;
    QString lastError;
    for (int attempt = 0; attempt < 2; ++attempt) {
        ++attempts;
        const QString connectionName = QStringLiteral("memory_index_%1_%2")
                                           .arg(trimmedAgentId, QUuid::createUuid().toString(QUuid::WithoutBraces));
        QString attemptError;
        if (buildSearchIndexOnce(dbPath, agentRootPath, connectionName, &rowCount, &attemptError)) {
            ok = true;
            break;
        }
        lastError = attemptError;
        if (attempt == 0 && isLikelyCorruptedSqliteError(attemptError)) {
            QFile::remove(dbPath);
            recoveredFromCorruption = true;
            continue;
        }
        break;
    }

    if (!ok && error)
        *error = lastError;

    if (!ok)
        return false;

    if (metadata) {
        metadata->insert(QStringLiteral("agent_id"), trimmedAgentId);
        metadata->insert(QStringLiteral("path"), dbPath);
        metadata->insert(QStringLiteral("rows_indexed"), rowCount);
        metadata->insert(QStringLiteral("rebuild_attempts"), attempts);
        metadata->insert(QStringLiteral("recovered_from_corruption"), recoveredFromCorruption);
        metadata->insert(QStringLiteral("indexed_at_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    }
    return true;
}

QString MemoryManager::sanitizeSingleLine(const QString& text, int maxChars) const
{
    QString normalized = text;
    normalized.replace(QLatin1Char('\r'), QLatin1Char(' '));
    normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
    normalized = normalized.simplified();
    if (maxChars > 0 && normalized.size() > maxChars)
        normalized = normalized.left(maxChars) + QStringLiteral("...");
    return normalized;
}

QJsonObject MemoryManager::readPolicyObject() const
{
    if (!m_persistence)
        return QJsonObject();
    bool ok = false;
    const QJsonObject policy = m_persistence->readJsonObject(policyPath(), &ok);
    if (!ok)
        return QJsonObject();
    return policy;
}

QString MemoryManager::memoryStewardAgentId() const
{
    const QJsonObject policy = readPolicyObject();
    return policy.value(QStringLiteral("memory_steward_agent_id")).toString().trimmed();
}

bool MemoryManager::shouldUpdateSharedWork(const QString& agentId) const
{
    const QString stewardId = memoryStewardAgentId();
    return !stewardId.isEmpty() && stewardId == agentId.trimmed();
}

bool MemoryManager::reflectionEnabled() const
{
    const QJsonObject policyObj = readPolicyObject();
    return policyBool(policyObj, QStringLiteral("reflect_enabled"), true);
}

int MemoryManager::reflectionIntervalTurns() const
{
    const QJsonObject policyObj = readPolicyObject();
    return qBound(
        1,
        policyInt(policyObj, QStringLiteral("reflect_every_n_turns"), 8),
        200);
}

QString MemoryManager::composeMemoryContext(const QString& agentId, int maxChars) const
{
    if (!m_persistence)
        return QString();

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty())
        return QString();

    struct SectionSpec {
        QString title;
        QString path;
        int maxReadChars = 0;
    };

    QList<SectionSpec> specs = {
        { QStringLiteral("SOUL"), soulDocPath(trimmedAgentId), kSoulReadChars },
        { QStringLiteral("IDENTITY"), identityDocPath(trimmedAgentId), kIdentityReadChars },
        { QStringLiteral("USER_PROFILE"), userDocPath(), kUserReadChars },
        { QStringLiteral("SHARED_WORK"), sharedWorkDocPath(), kSharedWorkReadChars },
        { QStringLiteral("USER_VIEW"), userViewDocPath(trimmedAgentId), kUserViewReadChars },
        { QStringLiteral("LONG_MEMORY"), longTermMemoryDocPath(trimmedAgentId), kLongMemoryReadChars }
    };

    const QStringList dailyFiles = latestDailyMemoryPaths(trimmedAgentId, 2);
    for (const QString& path : dailyFiles) {
        specs.append({ QStringLiteral("DAILY_%1").arg(QFileInfo(path).baseName()), path, kDailyReadChars });
    }

    QString context = QStringLiteral(
        "## Memory Context (system)\n"
        "Use the following memory context as background facts. Do not quote it verbatim to user.\n");
    bool appendedSection = false;

    const int hardLimit = qMax(1200, maxChars);
    for (const SectionSpec& spec : specs) {
        bool ok = false;
        MemoryDocument doc(spec.path);
        const QString content = doc.readTruncated(spec.maxReadChars, &ok).trimmed();
        if (!ok || content.isEmpty())
            continue;

        QString block = QStringLiteral("\n[%1]\n%2\n").arg(spec.title, content);
        if (context.size() + block.size() > hardLimit) {
            const int remain = hardLimit - context.size();
            if (remain > 80)
                context.append(block.left(remain));
            appendedSection = true;
            break;
        }
        context.append(block);
        appendedSection = true;
    }

    if (!appendedSection)
        return QString();
    return context.trimmed();
}

bool MemoryManager::retainTurn(const QString& agentId, const QString& sessionId, const TurnTask& turn, QString* summary, QString* writtenPath, QJsonObject* metadata, QString* error) const
{
    if (summary)
        summary->clear();
    if (writtenPath)
        *writtenPath = QString();
    if (metadata)
        *metadata = QJsonObject();
    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent id is empty");
        return false;
    }
    if (!ensureAgentMemoryDirs(trimmedAgentId, error))
        return false;

    const QString userLine = sanitizeSingleLine(turn.userContent, 260);
    const QString assistantLine = sanitizeSingleLine(turn.assistantContent, 420);
    if (userLine.isEmpty() && assistantLine.isEmpty())
        return true;

    const QString entry = QStringLiteral(
                              "## %1\n"
                              "- session_id: `%2`\n"
                              "- turn_id: `%3`\n"
                              "- trace_id: `%4`\n"
                              "- user: %5\n"
                              "- assistant_summary: %6\n")
                              .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                              .arg(sessionId)
                              .arg(turn.turnId)
                              .arg(turn.requestTraceId)
                              .arg(userLine.isEmpty() ? QStringLiteral("(empty)") : userLine)
                              .arg(assistantLine.isEmpty() ? QStringLiteral("(empty)") : assistantLine);

    const QString dailyPath = dailyMemoryPath(trimmedAgentId);
    MemoryDocument dailyDoc(dailyPath);
    if (!dailyDoc.appendAtomic(entry, error))
        return false;

    const QString userViewEntry = QStringLiteral(
                                      "## %1\n"
                                      "- session_id: `%2`\n"
                                      "- turn_id: `%3`\n"
                                      "- trace_id: `%4`\n"
                                      "- user_signal: %5\n"
                                      "- observed_response: %6\n")
                                      .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                                      .arg(sessionId)
                                      .arg(turn.turnId)
                                      .arg(turn.requestTraceId)
                                      .arg(userLine.isEmpty() ? QStringLiteral("(empty)") : userLine)
                                      .arg(assistantLine.isEmpty() ? QStringLiteral("(empty)") : assistantLine);
    MemoryDocument userViewDoc(userViewDocPath(trimmedAgentId));
    if (!userViewDoc.appendAtomic(userViewEntry, error))
        return false;

    if (shouldUpdateSharedWork(trimmedAgentId)) {
        const QString sharedEntry = QStringLiteral(
                                        "## %1\n"
                                        "- updated_by: `%2`\n"
                                        "- session_id: `%3`\n"
                                        "- turn_id: `%4`\n"
                                        "- trace_id: `%5`\n"
                                        "- current_work: %6\n")
                                        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                                        .arg(trimmedAgentId)
                                        .arg(sessionId)
                                        .arg(turn.turnId)
                                        .arg(turn.requestTraceId)
                                        .arg(assistantLine.isEmpty() ? userLine : assistantLine);
        MemoryDocument sharedDoc(sharedWorkDocPath());
        if (!sharedDoc.appendAtomic(sharedEntry, error))
            return false;
    }

    const QJsonObject policyObj = readPolicyObject();
    const bool autoExtractEnabled = policyBool(policyObj, QStringLiteral("auto_extract_enabled"), true);
    const int minUserCharsForExtract = qBound(
        1,
        policyInt(policyObj, QStringLiteral("min_user_chars_for_extract"), 12),
        4096);
    const int maxLongMemoryCandidates = qBound(
        1,
        policyInt(policyObj, QStringLiteral("max_long_memory_candidates_per_turn"), 3),
        32);

    const QString longMemoryPath = longTermMemoryDocPath(trimmedAgentId);
    int longMemoryAdded = 0;
    int longMemoryDuplicate = 0;
    QString firstLongMemoryEntry;
    QStringList longTermCandidates = buildLongTermCandidates(turn.userContent, turn.assistantContent);
    if (!autoExtractEnabled && !isManualRememberRequested(turn.userContent))
        longTermCandidates.clear();
    if (!turn.userContent.trimmed().isEmpty()
        && turn.userContent.trimmed().size() < minUserCharsForExtract
        && !isManualRememberRequested(turn.userContent)) {
        longTermCandidates.clear();
    }
    if (longTermCandidates.size() > maxLongMemoryCandidates)
        longTermCandidates = longTermCandidates.mid(0, maxLongMemoryCandidates);
    if (!longTermCandidates.isEmpty()) {
        MemoryDocument longDoc(longMemoryPath);
        bool readOk = false;
        QString longContent = longDoc.read(&readOk);
        if (!readOk) {
            if (error)
                *error = QStringLiteral("failed to read long memory document");
            return false;
        }

        for (const QString& candidateRaw : longTermCandidates) {
            const QString candidate = sanitizeSingleLine(candidateRaw, 420);
            if (candidate.isEmpty())
                continue;

            const QString fingerprint = makeMemoryFingerprint(candidate);
            const QString marker = QStringLiteral("[fp:%1]").arg(fingerprint);
            if (longContent.contains(marker)) {
                ++longMemoryDuplicate;
                continue;
            }

            const QString entry = QStringLiteral(
                                      "## %1\n"
                                      "- fp: %2\n"
                                      "- source_session_id: `%3`\n"
                                      "- source_turn_id: `%4`\n"
                                      "- source_trace_id: `%5`\n"
                                      "- memory: %6\n")
                                      .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                                      .arg(marker)
                                      .arg(sessionId)
                                      .arg(turn.turnId)
                                      .arg(turn.requestTraceId)
                                      .arg(candidate);
            if (!longDoc.appendAtomic(entry, error))
                return false;
            longContent.append(entry);
            ++longMemoryAdded;
            if (firstLongMemoryEntry.isEmpty())
                firstLongMemoryEntry = candidate;
        }
    }

    if (summary) {
        if (!firstLongMemoryEntry.isEmpty())
            *summary = firstLongMemoryEntry;
        else
            *summary = assistantLine;
        if (summary->isEmpty())
            *summary = userLine;
    }
    if (writtenPath)
        *writtenPath = dailyPath;
    if (metadata) {
        metadata->insert(QStringLiteral("manualRemember"), isManualRememberRequested(turn.userContent));
        metadata->insert(QStringLiteral("longMemoryAdded"), longMemoryAdded);
        metadata->insert(QStringLiteral("longMemoryDuplicate"), longMemoryDuplicate);
        metadata->insert(QStringLiteral("compacted_count"), longMemoryAdded);
        metadata->insert(QStringLiteral("longMemoryPath"), longMemoryPath);
        metadata->insert(QStringLiteral("dailyPath"), dailyPath);
        metadata->insert(QStringLiteral("autoExtractEnabled"), autoExtractEnabled);
        metadata->insert(QStringLiteral("minUserCharsForExtract"), minUserCharsForExtract);
        metadata->insert(QStringLiteral("maxLongMemoryCandidates"), maxLongMemoryCandidates);
    }
    return true;
}

bool MemoryManager::reflectAndScore(const QString& agentId, const QString& sessionId, const QString& turnId, const QString& traceId, QString* summary, QString* writtenPath, QJsonObject* metadata, QString* error) const
{
    if (summary)
        summary->clear();
    if (writtenPath)
        *writtenPath = QString();
    if (metadata)
        *metadata = QJsonObject();
    if (error)
        error->clear();

    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent id is empty");
        return false;
    }
    if (!ensureAgentMemoryDirs(trimmedAgentId, error))
        return false;

    const QJsonObject policyObj = readPolicyObject();
    const bool enabled = policyBool(policyObj, QStringLiteral("reflect_enabled"), true);
    const int maxCandidates = qBound(
        1,
        policyInt(policyObj, QStringLiteral("reflect_max_candidates_per_run"), 4),
        32);
    const int scanDailyFiles = qBound(
        1,
        policyInt(policyObj, QStringLiteral("reflect_scan_daily_files"), 7),
        30);

    const QString longMemoryPath = longTermMemoryDocPath(trimmedAgentId);
    if (writtenPath)
        *writtenPath = longMemoryPath;

    if (!enabled) {
        if (summary)
            *summary = QStringLiteral("反思任务已禁用");
        if (metadata) {
            metadata->insert(QStringLiteral("reflection_enabled"), false);
            metadata->insert(QStringLiteral("reflection_skipped"), true);
            metadata->insert(QStringLiteral("quality_score"), 0);
            metadata->insert(QStringLiteral("quality_level"), QStringLiteral("disabled"));
            metadata->insert(QStringLiteral("longMemoryPath"), longMemoryPath);
        }
        return true;
    }

    const QStringList dailyPaths = latestDailyMemoryPaths(trimmedAgentId, scanDailyFiles);
    QStringList userSignals;
    for (const QString& path : dailyPaths) {
        MemoryDocument dailyDoc(path);
        bool ok = false;
        const QString content = dailyDoc.read(&ok);
        if (!ok || content.trimmed().isEmpty())
            continue;
        userSignals.append(extractUserSignalsFromDailyContent(content, 200));
        if (userSignals.size() >= 1000)
            break;
    }

    QStringList stableCandidates;
    for (const QString& rawSignal : userSignals) {
        const QString signal = sanitizeSingleLine(rawSignal, 320);
        if (signal.isEmpty())
            continue;
        if (!looksLikeStableUserFact(signal) && !isManualRememberRequested(signal))
            continue;
        const QString candidate = QStringLiteral("反思提炼：%1").arg(signal);
        if (stableCandidates.contains(candidate))
            continue;
        stableCandidates.append(candidate);
        if (stableCandidates.size() >= maxCandidates)
            break;
    }

    MemoryDocument longDoc(longMemoryPath);
    bool readOk = false;
    QString longContent = longDoc.read(&readOk);
    if (!readOk) {
        if (error)
            *error = QStringLiteral("failed to read long memory document");
        return false;
    }

    int longMemoryAdded = 0;
    int longMemoryDuplicate = 0;
    QString firstAddedEntry;
    for (const QString& candidate : stableCandidates) {
        const QString fingerprint = makeMemoryFingerprint(candidate);
        const QString marker = QStringLiteral("[fp:%1]").arg(fingerprint);
        if (longContent.contains(marker)) {
            ++longMemoryDuplicate;
            continue;
        }

        const QString entry = QStringLiteral(
                                  "## %1\n"
                                  "- fp: %2\n"
                                  "- source_session_id: `%3`\n"
                                  "- source_turn_id: `%4`\n"
                                  "- source_trace_id: `%5`\n"
                                  "- reflected: true\n"
                                  "- memory: %6\n")
                                  .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                                  .arg(marker)
                                  .arg(sessionId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : sessionId.trimmed())
                                  .arg(turnId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : turnId.trimmed())
                                  .arg(traceId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : traceId.trimmed())
                                  .arg(candidate);
        if (!longDoc.appendAtomic(entry, error))
            return false;
        longContent.append(entry);
        ++longMemoryAdded;
        if (firstAddedEntry.isEmpty())
            firstAddedEntry = candidate;
    }

    int qualityScore = 55;
    qualityScore += qMin(20, stableCandidates.size() * 4);
    qualityScore += qMin(20, longMemoryAdded * 5);
    if (!stableCandidates.isEmpty()) {
        const double duplicateRatio = static_cast<double>(longMemoryDuplicate) / static_cast<double>(stableCandidates.size());
        qualityScore -= static_cast<int>(duplicateRatio * 20.0 + 0.5);
    }
    if (userSignals.isEmpty())
        qualityScore -= 15;
    qualityScore = qBound(0, qualityScore, 100);

    if (summary) {
        if (!firstAddedEntry.isEmpty())
            *summary = firstAddedEntry;
        else
            *summary = QStringLiteral("反思完成，本轮无新增长期记忆");
    }

    if (metadata) {
        metadata->insert(QStringLiteral("reflection_enabled"), true);
        metadata->insert(QStringLiteral("reflection_skipped"), false);
        metadata->insert(QStringLiteral("scan_daily_files"), scanDailyFiles);
        metadata->insert(QStringLiteral("scanned_daily_files"), dailyPaths.size());
        metadata->insert(QStringLiteral("scanned_user_signals"), userSignals.size());
        metadata->insert(QStringLiteral("stable_candidates"), stableCandidates.size());
        metadata->insert(QStringLiteral("max_candidates_per_run"), maxCandidates);
        metadata->insert(QStringLiteral("longMemoryAdded"), longMemoryAdded);
        metadata->insert(QStringLiteral("longMemoryDuplicate"), longMemoryDuplicate);
        metadata->insert(QStringLiteral("quality_score"), qualityScore);
        metadata->insert(QStringLiteral("quality_level"), qualityLevelFromScore(qualityScore));
        metadata->insert(QStringLiteral("longMemoryPath"), longMemoryPath);
    }
    return true;
}

bool MemoryManager::rememberManual(const QString& agentId, const QString& sessionId, const QString& turnId, const QString& traceId, const QString& text, QString* summary, QString* writtenPath, QJsonObject* metadata, QString* error) const
{
    if (summary)
        summary->clear();
    if (writtenPath)
        *writtenPath = QString();
    if (metadata)
        *metadata = QJsonObject();
    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent id is empty");
        return false;
    }
    if (!ensureAgentMemoryDirs(trimmedAgentId, error))
        return false;

    const QString memoryText = sanitizeSingleLine(text, 420);
    if (memoryText.isEmpty()) {
        if (error)
            *error = QStringLiteral("memory text is empty");
        return false;
    }

    const QString longMemoryPath = longTermMemoryDocPath(trimmedAgentId);
    MemoryDocument longDoc(longMemoryPath);
    bool readOk = false;
    QString longContent = longDoc.read(&readOk);
    if (!readOk) {
        if (error)
            *error = QStringLiteral("failed to read long memory document");
        return false;
    }

    const QString fingerprint = makeMemoryFingerprint(memoryText);
    const QString marker = QStringLiteral("[fp:%1]").arg(fingerprint);
    int longMemoryAdded = 0;
    int longMemoryDuplicate = 0;
    if (!longContent.contains(marker)) {
        const QString entry = QStringLiteral(
                                  "## %1\n"
                                  "- fp: %2\n"
                                  "- source_session_id: `%3`\n"
                                  "- source_turn_id: `%4`\n"
                                  "- source_trace_id: `%5`\n"
                                  "- manual: true\n"
                                  "- memory: %6\n")
                                  .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                                  .arg(marker)
                                  .arg(sessionId)
                                  .arg(turnId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : turnId.trimmed())
                                  .arg(traceId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : traceId.trimmed())
                                  .arg(QStringLiteral("用户手动标记：%1").arg(memoryText));
        if (!longDoc.appendAtomic(entry, error))
            return false;
        ++longMemoryAdded;
    } else {
        ++longMemoryDuplicate;
    }

    if (summary)
        *summary = memoryText;
    if (writtenPath)
        *writtenPath = longMemoryPath;
    if (metadata) {
        metadata->insert(QStringLiteral("manualRemember"), true);
        metadata->insert(QStringLiteral("longMemoryAdded"), longMemoryAdded);
        metadata->insert(QStringLiteral("longMemoryDuplicate"), longMemoryDuplicate);
        metadata->insert(QStringLiteral("compacted_count"), longMemoryAdded);
        metadata->insert(QStringLiteral("longMemoryPath"), longMemoryPath);
    }
    return true;
}

bool MemoryManager::rememberToolRequested(const QString& agentId,
                                          const QString& sessionId,
                                          const QString& turnId,
                                          const QString& traceId,
                                          const QString& text,
                                          const QString& reason,
                                          QString* summary,
                                          QString* writtenPath,
                                          QJsonObject* metadata,
                                          QString* error) const
{
    if (summary)
        summary->clear();
    if (writtenPath)
        *writtenPath = QString();
    if (metadata)
        *metadata = QJsonObject();
    if (!m_persistence) {
        if (error)
            *error = QStringLiteral("persistence is null");
        return false;
    }

    const QString trimmedAgentId = agentId.trimmed();
    if (trimmedAgentId.isEmpty()) {
        if (error)
            *error = QStringLiteral("agent id is empty");
        return false;
    }
    if (!ensureAgentMemoryDirs(trimmedAgentId, error))
        return false;

    const QString memoryText = sanitizeSingleLine(text, 420);
    if (memoryText.isEmpty()) {
        if (error)
            *error = QStringLiteral("memory text is empty");
        return false;
    }

    const QString trimmedReason = sanitizeSingleLine(reason, 180);
    const QString longMemoryPath = longTermMemoryDocPath(trimmedAgentId);
    MemoryDocument longDoc(longMemoryPath);
    bool readOk = false;
    QString longContent = longDoc.read(&readOk);
    if (!readOk) {
        if (error)
            *error = QStringLiteral("failed to read long memory document");
        return false;
    }

    const QString fingerprint = makeMemoryFingerprint(memoryText);
    const QString marker = QStringLiteral("[fp:%1]").arg(fingerprint);
    int longMemoryAdded = 0;
    int longMemoryDuplicate = 0;
    if (!longContent.contains(marker)) {
        QString entry = QStringLiteral(
                            "## %1\n"
                            "- fp: %2\n"
                            "- source_session_id: `%3`\n"
                            "- source_turn_id: `%4`\n"
                            "- source_trace_id: `%5`\n"
                            "- tool_requested: true\n"
                            "- memory: %6\n")
                            .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs))
                            .arg(marker)
                            .arg(sessionId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : sessionId.trimmed())
                            .arg(turnId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : turnId.trimmed())
                            .arg(traceId.trimmed().isEmpty() ? QStringLiteral("(unknown)") : traceId.trimmed())
                            .arg(QStringLiteral("助手主动记录：%1").arg(memoryText));
        if (!trimmedReason.isEmpty())
            entry += QStringLiteral("- reason: %1\n").arg(trimmedReason);
        if (!longDoc.appendAtomic(entry, error))
            return false;
        ++longMemoryAdded;
    } else {
        ++longMemoryDuplicate;
    }

    if (summary)
        *summary = memoryText;
    if (writtenPath)
        *writtenPath = longMemoryPath;
    if (metadata) {
        metadata->insert(QStringLiteral("toolRequested"), true);
        metadata->insert(QStringLiteral("longMemoryAdded"), longMemoryAdded);
        metadata->insert(QStringLiteral("longMemoryDuplicate"), longMemoryDuplicate);
        metadata->insert(QStringLiteral("compacted_count"), longMemoryAdded);
        metadata->insert(QStringLiteral("longMemoryPath"), longMemoryPath);
        if (!trimmedReason.isEmpty())
            metadata->insert(QStringLiteral("reason"), trimmedReason);
    }
    return true;
}
