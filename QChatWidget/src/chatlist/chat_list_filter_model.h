#ifndef CHAT_LIST_FILTER_MODEL_H
#define CHAT_LIST_FILTER_MODEL_H

#include <QList>
#include <QSortFilterProxyModel>

class ChatListFilterModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit ChatListFilterModel(QObject* parent = nullptr);

    void setSearchRoles(const QList<int>& roles);
    QList<int> searchRoles() const;

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;

private:
    QList<int> m_roles;
};

#endif // CHAT_LIST_FILTER_MODEL_H
