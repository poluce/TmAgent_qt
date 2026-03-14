#include "ChatListUiSupport.h"

#include "chat_list_view.h"
#include "chat_list_roles.h"
#include "chat_list_widget.h"
#include <QAbstractItemModel>
#include <QColor>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

namespace ChatListUiSupport {

int sourceRowForIndex(ChatListWidget* widget, const QModelIndex& index)
{
    if (!widget || !widget->listView() || !index.isValid())
        return -1;

    if (QSortFilterProxyModel* proxy = qobject_cast<QSortFilterProxyModel*>(widget->listView()->model()))
        return proxy->mapToSource(index).row();
    return index.row();
}

int currentSourceRow(ChatListWidget* widget)
{
    if (!widget || !widget->listView())
        return -1;
    return sourceRowForIndex(widget, widget->listView()->currentIndex());
}

void selectSourceRow(ChatListWidget* widget, int row)
{
    if (!widget || !widget->listView() || row < 0)
        return;

    QAbstractItemModel* model = widget->listView()->model();
    QModelIndex selected;
    if (QSortFilterProxyModel* proxy = qobject_cast<QSortFilterProxyModel*>(model))
        selected = proxy->mapFromSource(widget->listView()->standardModel()->index(row, 0));
    else if (model)
        selected = model->index(row, 0);
    if (selected.isValid())
        widget->listView()->setCurrentIndex(selected);
}

void clearCurrentSelection(ChatListWidget* widget)
{
    if (!widget || !widget->listView())
        return;
    widget->listView()->clearSelection();
    widget->listView()->setCurrentIndex(QModelIndex());
}

QString shortenPreview(const QString& preview, int maxChars)
{
    QString shortPreview = preview;
    if (maxChars > 0 && shortPreview.length() > maxChars)
        shortPreview = shortPreview.left(maxChars) + QStringLiteral("...");
    return shortPreview;
}

bool updateChatItemPreview(ChatListWidget* widget,
                           int row,
                           const QString& name,
                           const QString& preview,
                           const QString& timeText,
                           const QString& avatarPath)
{
    if (!widget || row < 0)
        return false;

    widget->updateChatItem(row, name, shortenPreview(preview), timeText, QColor(Qt::gray), 0);
    if (!avatarPath.isEmpty())
        widget->updateChatItemData(row, ChatListAvatarPathRole, avatarPath);
    return true;
}

} // namespace ChatListUiSupport
