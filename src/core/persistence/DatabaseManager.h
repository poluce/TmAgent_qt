#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QString>

class QSqlDatabase;

/**
 * @brief SQLite 数据库管理器（单例）
 *
 * 职责：
 * - 管理全局 SQLite 数据库文件路径和连接
 * - 建表、Schema 迁移
 * - 为每个线程提供独立的 QSqlDatabase 连接
 * - 启用 WAL 模式以支持跨进程并发读写
 */
class DatabaseManager {
public:
    static DatabaseManager* instance();

    /// 数据库文件路径（~/.tmagent/tmagent.db）
    QString databasePath() const;

    /// 获取当前线程可用的数据库连接
    /// 每个线程使用独立的连接名，避免跨线程共享
    QSqlDatabase connection() const;

    /// 初始化数据库（建表 + Schema 迁移）
    /// 应在 ChatService::initialize() 中调用一次
    bool initialize();

    /// 当前 Schema 版本
    int schemaVersion() const;

    /// 数据库是否已成功初始化
    bool isReady() const;

private:
    DatabaseManager();
    ~DatabaseManager() = default;
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool createTables(QSqlDatabase& db);
    bool applyMigrations(QSqlDatabase& db);
    void enableWalMode(QSqlDatabase& db);

    QString m_dbPath;
    bool m_initialized = false;
    int m_schemaVersion = 0;
};

#endif // DATABASEMANAGER_H
