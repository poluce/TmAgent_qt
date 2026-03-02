#include "DatabaseManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

static const int kCurrentSchemaVersion = 1;
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
            "  source          TEXT DEFAULT 'gui'"
            ")"),

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_msg_session "
            "ON messages(session_id)"),

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
    // 读取当前版本
    QSqlQuery versionQuery(db);
    if (!versionQuery.exec(QStringLiteral("SELECT version FROM schema_info LIMIT 1"))) {
        // 表可能刚创建，还没有记录
        m_schemaVersion = 0;
    } else if (versionQuery.next()) {
        m_schemaVersion = versionQuery.value(0).toInt();
    }

    if (m_schemaVersion >= kCurrentSchemaVersion)
        return true;

    // 版本 0 → 1：初始化版本记录
    if (m_schemaVersion < 1) {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT OR REPLACE INTO schema_info (version) VALUES (?)"));
        insert.addBindValue(kCurrentSchemaVersion);
        if (!insert.exec()) {
            qWarning() << "[DatabaseManager] 版本记录写入失败:" << insert.lastError().text();
            return false;
        }
        m_schemaVersion = kCurrentSchemaVersion;
    }

    // 未来的迁移在这里按版本号递增添加
    // if (m_schemaVersion < 2) { ... m_schemaVersion = 2; }

    return true;
}
