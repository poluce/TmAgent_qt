#include "IdentityTabBar.h"
#include <QToolButton>
#include <QVariant>

IdentityTabBar::IdentityTabBar(QWidget* parent)
    : QTabBar(parent)
{
    setExpanding(false);
    setDocumentMode(true);
}

int IdentityTabBar::addUserTab(const QString& identityId, const QString& displayName)
{
    int index = addTab(displayName);
    setTabData(index, identityId);
    // 用户 Tab 不设关闭按钮
    return index;
}

int IdentityTabBar::addAgentTab(const QString& identityId, const QString& displayName)
{
    int index = addTab(displayName);
    setTabData(index, identityId);

    // 添加关闭按钮
    auto* closeBtn = new QToolButton(this);
    closeBtn->setText(QStringLiteral("×"));
    closeBtn->setAutoRaise(true);
    closeBtn->setFixedSize(16, 16);
    setTabButton(index, QTabBar::RightSide, closeBtn);

    connect(closeBtn, &QToolButton::clicked, this, [this, identityId]() {
        int idx = tabIndexForIdentity(identityId);
        if (idx > 0) // 不允许关闭 Tab 0
            emit agentTabCloseRequested(idx, identityId);
    });

    return index;
}

QString IdentityTabBar::identityIdForTab(int index) const
{
    if (index < 0 || index >= count())
        return QString();
    return tabData(index).toString();
}

int IdentityTabBar::tabIndexForIdentity(const QString& identityId) const
{
    for (int i = 0; i < count(); ++i) {
        if (tabData(i).toString() == identityId)
            return i;
    }
    return -1;
}

void IdentityTabBar::updateTabName(const QString& identityId, const QString& newName)
{
    int idx = tabIndexForIdentity(identityId);
    if (idx >= 0)
        setTabText(idx, newName);
}
