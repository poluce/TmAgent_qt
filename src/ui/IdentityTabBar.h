#ifndef IDENTITYTABBAR_H
#define IDENTITYTABBAR_H

#include <QTabBar>
#include <QString>

/**
 * @brief 顶部 Identity 切换 Tab 栏
 *
 * Tab 0 为用户（固定不可关闭），后续 Tab 为 Agent（可关闭）。
 * 每个 Tab 的 data 存储对应的 identityId。
 */
class IdentityTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit IdentityTabBar(QWidget* parent = nullptr);

    /** 添加用户 Tab（固定，不可关闭） */
    int addUserTab(const QString& identityId, const QString& displayName);

    /** 添加 Agent Tab（可关闭） */
    int addAgentTab(const QString& identityId, const QString& displayName);

    /** 获取指定 Tab 的 identityId */
    QString identityIdForTab(int index) const;

    /** 根据 identityId 查找 Tab 索引，未找到返回 -1 */
    int tabIndexForIdentity(const QString& identityId) const;

    /** 更新指定 Identity 的 Tab 名称 */
    void updateTabName(const QString& identityId, const QString& newName);

signals:
    /** Agent Tab 关闭请求 */
    void agentTabCloseRequested(int index, const QString& identityId);
};

#endif // IDENTITYTABBAR_H
