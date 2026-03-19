#ifndef TEAMMATE_H
#define TEAMMATE_H

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>

/**
 * @brief 通用队友模型
 *
 * 每个 Teammate 代表一个持久化的子代理会话，
 * 后端可以是 Codex、Claude Code 或其他实现。
 * 由 TeammateManager 创建和管理。
 */
class Teammate : public QObject {
    Q_OBJECT
public:
    enum class Status {
        Idle,
        Busy,
        Error,
        Shutdown
    };
    Q_ENUM(Status)

    struct Config {
        QString name;
        QString role;
        QString backend;                 // "codex", "claude-code", ...
        QString workingDirectory;
        QString ownerAgentId;            // 创建者 Agent ID（隔离用）
        int turnIdleTimeoutMs = 0;
        QJsonObject backendOverrides;    // 后端特有的额外参数
    };

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    void setName(const QString& name);
    QString role() const { return m_role; }
    void setRole(const QString& role);
    QString backend() const { return m_backend; }
    QString threadId() const { return m_threadId; }
    QString ownerAgentId() const { return m_ownerAgentId; }
    Status status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    int turnCount() const { return m_turnCount; }
    qint64 createdAtMs() const { return m_createdAtMs; }
    qint64 lastActiveAtMs() const { return m_lastActiveAtMs; }
    QString workingDirectory() const { return m_workingDirectory; }
    int turnIdleTimeoutMs() const { return m_turnIdleTimeoutMs; }
    QJsonObject backendOverrides() const { return m_backendOverrides; }

    static QString statusToString(Status status);
    QJsonObject toJson() const;

signals:
    void nameChanged(const QString& name);
    void roleChanged(const QString& role);
    void statusChanged(Status status);
    void turnStarted(const QString& turnId);
    void turnCompleted(const QString& turnId, bool success, const QString& result);
    void messageDelta(const QString& turnId, const QString& delta);

private:
    friend class TeammateManager;
    friend class CodexTeammateBackend;
    explicit Teammate(const QString& id, const Config& config, QObject* parent = nullptr);

    void setStatus(Status status);
    void setThreadId(const QString& threadId);
    void setLastError(const QString& error);
    void incrementTurnCount();
    void touchLastActive();

    QString m_id;
    QString m_name;
    QString m_role;
    QString m_backend;
    QString m_threadId;
    QString m_ownerAgentId;
    Status m_status = Status::Idle;
    QString m_lastError;
    int m_turnCount = 0;
    int m_turnIdleTimeoutMs = 0;
    qint64 m_createdAtMs = 0;
    qint64 m_lastActiveAtMs = 0;
    QString m_workingDirectory;
    QJsonObject m_backendOverrides;
};

#endif // TEAMMATE_H
