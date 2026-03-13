#include "chat_list_filter_model.h"
#include <QRegularExpression>

ChatListFilterModel::ChatListFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

void ChatListFilterModel::setSearchRoles(const QList<int>& roles)
{
    m_roles = roles;
    invalidateFilter();
}

QList<int> ChatListFilterModel::searchRoles() const
{
    return m_roles;
}

bool ChatListFilterModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
    const QRegularExpression re = filterRegularExpression();
    if (!re.isValid() || re.pattern().isEmpty()) {
        return true;
    }

    if (m_roles.isEmpty()) {
        return QSortFilterProxyModel::filterAcceptsRow(source_row, source_parent);
    }

    const QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
    for (int role : m_roles) {
        if (re.match(index.data(role).toString()).hasMatch()) {
            return true;
        }
    }
    return false;
}
