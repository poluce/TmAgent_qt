#include "DatabaseManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

static const int kCurrentSchemaVersion = 3;
static const char* kMainConnectionName = "tmagent_main";

DatabaseManager* DatabaseManager::instance()
{
    static DatabaseManager s_instance;
    return &s_instance;
}

DatabaseManager::DatabaseManager()
{
    m_dbPath = QDir::home().filePath(QStringLiteral(".tmagent/tmagent.db"));
}

QString DatabaseManager::databasePath() const
{
    return m_dbPath;
}

bool DatabaseManager::isReady() const
{
    return m_initialized;
}

int DatabaseManager::schemaVersion() const
{
    return m_schemaVersion;
}

QSqlDatabase DatabaseManager::connection() const
{
    // 每个线程使用独立的连接名
    const QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThread()));
    const QString connName = QStringLiteral("tmagent_") + threadId;

    if (QSqlDatabase::contains(connName))
        return QSqlDatabase::database(connName, true);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(m_dbPath);
    if (!db.open()) {
        qWarning() << "[DatabaseManager] 无法打开数据库连接:" << connName
                   << db.lastError().text();
    }
    return db;
}

bool DatabaseManager::initialize()
{
    if (m_initialized)
        return true;

    // 确保数据目录存在
    QDir().mkpath(QDir::home().filePath(QStringLiteral(".tmagent")));

    // 使用主连接初始化
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kMainConnectionName);
        db.setDatabaseName(m_dbPath);
        if (!db.open()) {
            qWarning() << "[DatabaseManager] 数据库打开失败:" << db.lastError().text();
            return false;
        }

        enableWalMode(db);

        if (!createTables(db)) {
            qWarning() << "[DatabaseManager] 建表失败";
            db.close();
            return false;
        }

        if (!applyMigrations(db)) {
            qWarning() << "[DatabaseManager] Schema 迁移失败";
            db.close();
            return false;
        }

        db.close();
    }
    QSqlDatabase::removeDatabase(kMainConnectionName);

    m_initialized = true;
    qDebug() << "[DatabaseManager] 初始化成功，版本:" << m_schemaVersion
             << "路径:" << m_dbPath;
    return true;
}

void DatabaseManager::enableWalMode(QSqlDatabase& db)
{
    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"))) {
        qWarning() << "[DatabaseManager] WAL 模式启用失败:" << pragma.lastError().text();
    }
    // 设置 busy_timeout，防止跨进程锁等待失败
    QSqlQuery timeout(db);
    timeout.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
}

bool DatabaseManager::createTables(QSqlDatabase& db)
{
    const QStringList statements = {
        // 身份表
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS identities ("
            "  id         TEXT PRIMARY KEY,"
            "  type       TEXT NOT NULL DEFAULT 'agent',"
            "  name       TEXT NOT NULL DEFAULT '',"
            "  avatar     TEXT DEFAULT '',"
            "  profile    TEXT DEFAULT ''"
            ")"),

        // 会话表
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS sessions ("
            "  id             TEXT PRIMARY KEY,"
            "  type           TEXT NOT NULL DEFAULT 'private',"
            "  title          TEXT DEFAULT '',"
            "  owner_id       TEXT DEFAULT '',"
            "  created_at     TEXT DEFAULT '',"
            "  last_active_at TEXT DEFAULT ''"
            ")"),

        // 会话参与者
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS session_participants ("
            "  session_id  TEXT NOT NULL,"
            "  identity_id TEXT NOT NULL,"
            "  PRIMARY KEY (session_id, identity_id)"
            ")"),

        // 消息表
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            "  id              TEXT PRIMARY KEY,"
            "  session_id      TEXT NOT NULL,"
            "  trace_id        TEXT DEFAULT '',"
            "  turn_id         TEXT DEFAULT '',"
            "  seq             INTEGER DEFAULT 0,"
            "  sender_id       TEXT DEFAULT '',"
            "  content_type    TEXT DEFAULT 'text',"
            "  content_text    TEXT DEFAULT '',"
            "  content_payload TEXT DEFAULT '{}',"
            "  timestamp       TEXT DEFAULT '',"
            "  status          TEXT DEFAULT 'completed',"
            "  visible_in_chat INTEGER NOT NULL DEFAULT 1,"
            "  source          TEXT DEFAULT 'gui'"
            ")"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_msg_session "
            "ON messages(session_id)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_msg_session_time "
            "ON messages(session_id, timestamp DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_msg_trace "
            "ON messages(trace_id)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_msg_turn "
            "ON messages(turn_id)"),

        // 事件表
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS events ("
            "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp       TEXT NOT NULL,"
            "  timestamp_ms    INTEGER NOT NULL,"
            "  session_id      TEXT DEFAULT '',"
            "  trace_id        TEXT DEFAULT '',"
            "  turn_id         TEXT DEFAULT '',"
            "  run_id          TEXT DEFAULT '',"
            "  request_id      TEXT DEFAULT '',"
            "  tool_call_id    TEXT DEFAULT '',"
            "  actor_id        TEXT DEFAULT '',"
            "  tool_name       TEXT DEFAULT '',"
            "  event_type      TEXT DEFAULT '',"
            "  level           TEXT DEFAULT '',"
            "  duration_ms     INTEGER DEFAULT NULL,"
            "  success         INTEGER DEFAULT NULL,"
            "  summary         TEXT DEFAULT '',"
            "  raw_json        TEXT NOT NULL"
            ")"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_time "
            "ON events(timestamp_ms DESC, id DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_session_time "
            "ON events(session_id, timestamp_ms DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_trace_time "
            "ON events(trace_id, timestamp_ms DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_run_time "
            "ON events(run_id, timestamp_ms DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_request_time "
            "ON events(request_id, timestamp_ms DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_tool_call_time "
            "ON events(tool_call_id, timestamp_ms DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_tool_name_time "
            "ON events(tool_name, timestamp_ms DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_event_type_time "
            "ON events(event_type, timestamp_ms DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_level_time "
            "ON events(level, timestamp_ms DESC)"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_events_duration "
            "ON events(duration_ms)"),

        // Pending Turns
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS pending_turns ("
            "  id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  session_id TEXT NOT NULL,"
            "  state      TEXT DEFAULT 'queued',"
            "  turn_data  TEXT DEFAULT '{}'"
            ")"),

        // 应用状态
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS app_state ("
            "  key   TEXT PRIMARY KEY,"
            "  value TEXT DEFAULT ''"
            ")"),

        // Schema 版本
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_info ("
            "  version INTEGER NOT NULL"
            ")")
    };

    QSqlQuery query(db);
    for (const QString& sql : statements) {
        if (!query.exec(sql)) {
            qWarning() << "[DatabaseManager] 执行建表 SQL 失败:"
                       << query.lastError().text()
                       << "\nSQL:" << sql;
            return false;
        }
    }
    return true;
}

bool DatabaseManager::applyMigrations(QSqlDatabase& db)
{
    auto tableHasColumn = [&](const QString& tableName, const QString& columnName) -> bool {
        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName)))
            return false;
        while (q.next()) {
            if (q.value(1).toString().trimmed() == columnName)
                return true;
        }
        return false;
    };

    auto execStatements = [&](const QStringList& statements, const QString& stage) -> bool {
        QSqlQuery q(db);
        for (const QString& sql : statements) {
            if (!q.exec(sql)) {
                qWarning() << "[DatabaseManager] 迁移失败:" << stage
                           << q.lastError().text()
                           << "\nSQL:" << sql;
                return false;
            }
        }
        return true;
    };

    auto setSchemaVersion = [&](int version) -> bool {
        QSqlQuery clear(db);
        if (!clear.exec(QStringLiteral("DELETE FROM schema_info"))) {
            qWarning() << "[DatabaseManager] 清理版本记录失败:" << clear.lastError().text();
            return false;
        }

        QSqlQuery q(db);
        q.prepare(QStringLiteral("INSERT INTO schema_info (version) VALUES (?)"));
        q.addBindValue(version);
        if (!q.exec()) {
            qWarning() << "[DatabaseManager] 版本记录写入失败:" << q.lastError().text()
                       << "version=" << version;
            return false;
        }
        return true;
    };

    // 读取当前版本
    m_schemaVersion = 0;
    QSqlQuery versionQuery(db);
    if (versionQuery.exec(QStringLiteral("SELECT COALESCE(MAX(version), 0) FROM schema_info"))
        && versionQuery.next()) {
        m_schemaVersion = versionQuery.value(0).toInt();
    }

    if (m_schemaVersion >= kCurrentSchemaVersion)
        return true;

    // 版本 0 -> 1：初始化 schema_info
    if (m_schemaVersion < 1) {
        if (!setSchemaVersion(1))
            return false;
        m_schemaVersion = 1;
    }

    // 版本 1 -> 2：新增 events 表与查询索引
    if (m_schemaVersion < 2) {
        const QStringList migrationV2 = {
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_msg_session_time "
                "ON messages(session_id, timestamp DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_msg_trace "
                "ON messages(trace_id)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_msg_turn "
                "ON messages(turn_id)"),

            QStringLiteral(
                "CREATE TABLE IF NOT EXISTS events ("
                "  id              INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  timestamp       TEXT NOT NULL,"
                "  timestamp_ms    INTEGER NOT NULL,"
                "  session_id      TEXT DEFAULT '',"
                "  trace_id        TEXT DEFAULT '',"
                "  turn_id         TEXT DEFAULT '',"
                "  run_id          TEXT DEFAULT '',"
                "  request_id      TEXT DEFAULT '',"
                "  tool_call_id    TEXT DEFAULT '',"
                "  actor_id        TEXT DEFAULT '',"
                "  tool_name       TEXT DEFAULT '',"
                "  event_type      TEXT DEFAULT '',"
                "  level           TEXT DEFAULT '',"
                "  duration_ms     INTEGER DEFAULT NULL,"
                "  success         INTEGER DEFAULT NULL,"
                "  summary         TEXT DEFAULT '',"
                "  raw_json        TEXT NOT NULL"
                ")"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_time "
                "ON events(timestamp_ms DESC, id DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_session_time "
                "ON events(session_id, timestamp_ms DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_trace_time "
                "ON events(trace_id, timestamp_ms DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_run_time "
                "ON events(run_id, timestamp_ms DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_request_time "
                "ON events(request_id, timestamp_ms DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_tool_call_time "
                "ON events(tool_call_id, timestamp_ms DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_tool_name_time "
                "ON events(tool_name, timestamp_ms DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_event_type_time "
                "ON events(event_type, timestamp_ms DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_level_time "
                "ON events(level, timestamp_ms DESC)"),
            QStringLiteral(
                "CREATE INDEX IF NOT EXISTS idx_events_duration "
                "ON events(duration_ms)")
        };

        if (!execStatements(migrationV2, QStringLiteral("v1->v2")))
            return false;
        if (!setSchemaVersion(2))
            return false;
        m_schemaVersion = 2;
    }

    if (m_schemaVersion < 3) {
        QStringList migrationV3;
        if (!tableHasColumn(QStringLiteral("messages"), QStringLiteral("visible_in_chat"))) {
            migrationV3.append(
                QStringLiteral(
                    "ALTER TABLE messages "
                    "ADD COLUMN visible_in_chat INTEGER NOT NULL DEFAULT 1"));
        }
        if (!migrationV3.isEmpty()
            && !execStatements(migrationV3, QStringLiteral("v2->v3"))) {
            return false;
        }
        if (!setSchemaVersion(3))
            return false;
        m_schemaVersion = 3;
    }

    return m_schemaVersion >= kCurrentSchemaVersion;
}
