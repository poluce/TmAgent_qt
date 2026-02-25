#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include "core/model/Session.h"
#include <QHash>
#include <QObject>

/**
 * @brief Session 管理器——管理所有 Session 的创建、查询、持久化
 *
 * 单例模式。统一消息写入入口，Session 的 parent = SessionManager。
 */
class SessionManager : public QObject {
    Q_OBJECT
public:
    static SessionManager* instance();

    /// 创建私聊 Session
    Session* createPrivateSession(const QString& participantA, const QString& participantB);

    /// 创建群聊 Session
    Session* createGroupSession(const QString& ownerId, const QStringList& participantIds, const QString& title = QString());

    /// 按 ID 查找 Session
    Session* findById(const QString& id) const;

    /// 获取某个 Identity 参与的所有 Session
    QList<Session*> sessionsForIdentity(const QString& identityId) const;

    /// 获取所有 Session
    QList<Session*> allSessions() const;

    /// 移除 Session
    bool removeSession(const QString& id);

    /// 统一消息写入入口
    void postMessage(const QString& sessionId, const Message& msg);

    /// 获取 Session 数量
    int sessionCount() const;

    /// 按创建顺序获取 Session（用于稳定列表顺序）
    Session* sessionAt(int index) const;

    /// 获取 Session 在有序列表中的索引
    int indexOf(const QString& sessionId) const;

    /// 替换 Session 的 ID（用于持久化恢复后的 re-key）
    bool replaceSessionId(const QString& oldId, const QString& newId);

    // ---- 持久化 ----
    void saveAllToDisk(const QString& filePath);
    bool loadAllFromDisk(const QString& filePath);

signals:
    void sessionCreated(Session* session);
    void sessionRemoved(const QString& id);
    void messagePosted(const QString& sessionId, const Message& msg);

private:
    explicit SessionManager(QObject* parent = nullptr);
    ~SessionManager() override = default;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    QHash<QString, Session*> m_sessions; // id -> Session*
    QList<QString> m_sessionOrder;       // 有序 ID 列表（用于稳定列表顺序）
};

#endif // SESSIONMANAGER_H
