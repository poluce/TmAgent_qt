#ifndef CODEXTEAMMATE_H
#define CODEXTEAMMATE_H

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>

/**
 * @brief Codex 队友模型
 *
 * 每个 CodexTeammate 对应一个 Codex app-server 中的 Thread，
 * 拥有独立的名字、角色设定，可多轮对话。
 * 由 CodexTeammateManager 创建和管理。
 */
class CodexTeammate : public QObject {
    Q_OBJECT
public:
    enum class Status {
        Idle,     // 空闲，可接受新消息
        Busy,     // 正在执行 Turn
        Error,    // 上次 Turn 出错
        Shutdown  // 已关闭
    };
    Q_ENUM(Status)

    struct Config {
        QString name;
        QString role;
        QString workingDirectory;
        int turnIdleTimeoutMs = 0;       // 0 = 无 turn 级 idle timeout
        QJsonObject threadOverrides;
    };

    QString id() const { return m_id; }
    QString name() const { return m_name; }
    void setName(const QString& name);
    QString role() const { return m_role; }
    void setRole(const QString& role);
    QString threadId() const { return m_threadId; }
    Status status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    int turnCount() const { return m_turnCount; }
    qint64 createdAtMs() const { return m_createdAtMs; }
    qint64 lastActiveAtMs() const { return m_lastActiveAtMs; }
    QString workingDirectory() const { return m_workingDirectory; }
    int turnIdleTimeoutMs() const { return m_turnIdleTimeoutMs; }

    QJsonObject toJson() const;

signals:
    void nameChanged(const QString& name);
    void roleChanged(const QString& role);
    void statusChanged(Status status);
    void turnStarted(const QString& turnId);
    void turnCompleted(const QString& turnId, bool success, const QString& result);
    void messageDelta(const QString& turnId, const QString& delta);

private:
    friend class CodexTeammateManager;
    explicit CodexTeammate(const QString& id, const Config& config, QObject* parent = nullptr);

    void setStatus(Status status);
    void setThreadId(const QString& threadId);
    void setLastError(const QString& error);
    void incrementTurnCount();
    void touchLastActive();

    QString m_id;
    QString m_name;
    QString m_role;
    QString m_threadId;
    Status m_status = Status::Idle;
    QString m_lastError;
    int m_turnCount = 0;
    qint64 m_createdAtMs = 0;
    qint64 m_lastActiveAtMs = 0;
    int m_turnIdleTimeoutMs = 0;
    QString m_workingDirectory;
    QJsonObject m_threadOverrides;
};

#endif // CODEXTEAMMATE_H
