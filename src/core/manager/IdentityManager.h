#ifndef IDENTITYMANAGER_H
#define IDENTITYMANAGER_H

#include "core/model/Identity.h"
#include <QHash>
#include <QObject>

class IdentityProfile;

/**
 * @brief Identity 管理器——管理所有 Identity 的注册和生命周期
 *
 * 单例模式。持有全局唯一的 userIdentity 和所有 Agent Identity。
 * Identity 的 parent = IdentityManager（自动销毁）。
 */
class IdentityManager : public QObject {
    Q_OBJECT
public:
    static IdentityManager* instance();

    /// 获取用户 Identity（全局唯一，懒创建）
    Identity* userIdentity(const QString& preferredId = QString());

    /// 创建 Agent Identity
    Identity* createAgent(const QString& name, IdentityProfile* profile = nullptr, const QString& preferredId = QString());

    /// 按 ID 查找 Identity
    Identity* findById(const QString& id) const;

    /// 按名称查找 Identity（返回第一个匹配）
    Identity* findByName(const QString& name) const;

    /// 获取所有 Agent Identity
    QList<Identity*> allAgents() const;

    /// 获取所有 Identity（含用户）
    QList<Identity*> allIdentities() const;

    /// 移除 Agent Identity（用户不可移除）
    bool removeAgent(const QString& id);

signals:
    void agentCreated(Identity* agent);
    void agentRemoved(const QString& id);

private:
    explicit IdentityManager(QObject* parent = nullptr);
    ~IdentityManager() override = default;
    IdentityManager(const IdentityManager&) = delete;
    IdentityManager& operator=(const IdentityManager&) = delete;

    Identity* m_userIdentity = nullptr;
    QHash<QString, Identity*> m_identities; // id -> Identity*
};

#endif // IDENTITYMANAGER_H
