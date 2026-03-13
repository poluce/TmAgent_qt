#include "chat_list_view.h"
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <algorithm>

ChatListView::ChatListView(QWidget* parent) : QListView(parent)
{
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setUniformItemSizes(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);

    m_delegate = new ChatListDelegate(this);
    setItemDelegate(m_delegate);
    setObjectName("chatListView");
    updateViewStyleSheet();

    connect(this, &QListView::clicked, this, &ChatListView::onItemClicked);
}

ChatListDelegate* ChatListView::chatDelegate() const
{
    return m_delegate;
}

QStandardItemModel* ChatListView::standardModel()
{
    return ensureStandardModel();
}

void ChatListView::setChatDelegate(ChatListDelegate* delegate)
{
    if (!delegate || delegate == m_delegate) {
        return;
    }
    if (!delegate->parent()) {
        delegate->setParent(this);
    }
    m_delegate = delegate;
    setItemDelegate(m_delegate);
    updateViewStyleSheet();
    viewport()->update();
}

template<typename T>
void ChatListView::updateStyleField(T ChatListDelegate::Style::*field, const T& value)
{
    auto s = currentStyle();
    s.*field = value;
    setStyle(s);
}

void ChatListView::setItemHeight(int height)
{
    updateStyleField(&ChatListDelegate::Style::itemHeight, height);
}

void ChatListView::setAvatarSize(int size)
{
    updateStyleField(&ChatListDelegate::Style::avatarSize, size);
}

void ChatListView::setAvatarShape(ChatListDelegate::AvatarShape shape)
{
    updateStyleField(&ChatListDelegate::Style::avatarShape, shape);
}

void ChatListView::setAvatarCornerRadius(int radius)
{
    updateStyleField(&ChatListDelegate::Style::avatarCornerRadius, radius);
}

void ChatListView::setMargins(int margin)
{
    updateStyleField(&ChatListDelegate::Style::margin, margin);
}

void ChatListView::setBadgeSize(int size)
{
    updateStyleField(&ChatListDelegate::Style::badgeSize, size);
}

void ChatListView::setShowSeparator(bool show)
{
    updateStyleField(&ChatListDelegate::Style::showSeparator, show);
}

void ChatListView::setShowUnreadBadge(bool show)
{
    updateStyleField(&ChatListDelegate::Style::showUnreadBadge, show);
}

void ChatListView::setBackgroundColor(const QColor& color)
{
    updateStyleField(&ChatListDelegate::Style::backgroundColor, color);
}

void ChatListView::setHoverColor(const QColor& color)
{
    updateStyleField(&ChatListDelegate::Style::hoverColor, color);
}

void ChatListView::setSelectedColor(const QColor& color)
{
    updateStyleField(&ChatListDelegate::Style::selectedColor, color);
}

void ChatListView::setNameColor(const QColor& color)
{
    updateStyleField(&ChatListDelegate::Style::nameColor, color);
}

void ChatListView::setMessageColor(const QColor& color)
{
    updateStyleField(&ChatListDelegate::Style::messageColor, color);
}

void ChatListView::setTimeColor(const QColor& color)
{
    updateStyleField(&ChatListDelegate::Style::timeColor, color);
}

void ChatListView::setSeparatorColor(const QColor& color)
{
    updateStyleField(&ChatListDelegate::Style::separatorColor, color);
}

void ChatListView::setBadgeColor(const QColor& color)
{
    updateStyleField(&ChatListDelegate::Style::badgeColor, color);
}

void ChatListView::setBadgeTextColor(const QColor& color)
{
    updateStyleField(&ChatListDelegate::Style::badgeTextColor, color);
}

void ChatListView::setNameFont(const QFont& font)
{
    updateStyleField(&ChatListDelegate::Style::nameFont, font);
}

void ChatListView::setMessageFont(const QFont& font)
{
    updateStyleField(&ChatListDelegate::Style::messageFont, font);
}

void ChatListView::setTimeFont(const QFont& font)
{
    updateStyleField(&ChatListDelegate::Style::timeFont, font);
}

void ChatListView::setBadgeFont(const QFont& font)
{
    updateStyleField(&ChatListDelegate::Style::badgeFont, font);
}

int ChatListView::addChatItem(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount)
{
    QStandardItemModel* model = ensureStandardModel();
    QStandardItem* item = new QStandardItem();
    item->setData(name, ChatListNameRole);
    item->setData(message, ChatListMessageRole);
    item->setData(time, ChatListTimeRole);
    item->setData(avatarColor, ChatListAvatarColorRole);
    item->setData(unreadCount, ChatListUnreadCountRole);
    model->appendRow(item);
    return model->rowCount() - 1;
}

void ChatListView::updateChatItem(int row, const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount)
{
    QStandardItemModel* model = ensureStandardModel();
    if (row < 0 || row >= model->rowCount()) {
        return;
    }
    QStandardItem* item = model->item(row);
    if (!item) {
        return;
    }
    item->setData(name, ChatListNameRole);
    item->setData(message, ChatListMessageRole);
    item->setData(time, ChatListTimeRole);
    item->setData(avatarColor, ChatListAvatarColorRole);
    item->setData(unreadCount, ChatListUnreadCountRole);
}

bool ChatListView::updateChatItemData(int row, int role, const QVariant& value)
{
    QStandardItemModel* model = ensureStandardModel();
    if (row < 0 || row >= model->rowCount()) {
        return false;
    }
    QStandardItem* item = model->item(row);
    if (!item) {
        return false;
    }
    item->setData(value, role);
    return true;
}

bool ChatListView::updateChatItemData(int row, const QHash<int, QVariant>& values)
{
    QStandardItemModel* model = ensureStandardModel();
    if (row < 0 || row >= model->rowCount()) {
        return false;
    }
    QStandardItem* item = model->item(row);
    if (!item) {
        return false;
    }
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        item->setData(it.value(), it.key());
    }
    return true;
}

const QStandardItemModel* ChatListView::resolveSourceModel() const
{
    const QAbstractItemModel* current = model();
    if (auto* proxy = qobject_cast<const QSortFilterProxyModel*>(current)) {
        current = proxy->sourceModel();
    }
    return qobject_cast<const QStandardItemModel*>(current);
}

int ChatListView::findRowByName(const QString& name) const
{
    const QStandardItemModel* standard = resolveSourceModel();
    if (!standard) {
        return -1;
    }
    for (int row = 0; row < standard->rowCount(); ++row) {
        if (standard->index(row, 0).data(ChatListNameRole).toString() == name) {
            return row;
        }
    }
    return -1;
}

QList<int> ChatListView::findRowsByName(const QString& name) const
{
    QList<int> rows;
    const QStandardItemModel* standard = resolveSourceModel();
    if (!standard) {
        return rows;
    }
    for (int row = 0; row < standard->rowCount(); ++row) {
        if (standard->index(row, 0).data(ChatListNameRole).toString() == name) {
            rows.append(row);
        }
    }
    return rows;
}

bool ChatListView::updateChatItemByName(const QString& name, int role, const QVariant& value)
{
    const int row = findRowByName(name);
    if (row < 0) {
        return false;
    }
    return updateChatItemData(row, role, value);
}

bool ChatListView::updateChatItemByName(const QString& name, const QHash<int, QVariant>& values)
{
    const int row = findRowByName(name);
    if (row < 0) {
        return false;
    }
    return updateChatItemData(row, values);
}

int ChatListView::updateChatItemsByName(const QString& name, int role, const QVariant& value)
{
    int count = 0;
    const QList<int> rows = findRowsByName(name);
    for (int row : rows) {
        if (updateChatItemData(row, role, value)) {
            ++count;
        }
    }
    return count;
}

int ChatListView::updateChatItemsByName(const QString& name, const QHash<int, QVariant>& values)
{
    int count = 0;
    const QList<int> rows = findRowsByName(name);
    for (int row : rows) {
        if (updateChatItemData(row, values)) {
            ++count;
        }
    }
    return count;
}

bool ChatListView::removeChatItem(int row)
{
    QStandardItemModel* model = ensureStandardModel();
    if (row < 0 || row >= model->rowCount()) {
        return false;
    }
    return model->removeRow(row);
}

bool ChatListView::removeChatItem(const QModelIndex& index)
{
    if (!index.isValid()) {
        return false;
    }
    QModelIndex sourceIndex = index;
    if (auto* proxy = qobject_cast<QSortFilterProxyModel*>(model())) {
        if (index.model() == proxy) {
            sourceIndex = proxy->mapToSource(index);
        }
    }
    if (!sourceIndex.isValid()) {
        return false;
    }
    QStandardItemModel* standard = ensureStandardModel();
    if (sourceIndex.row() < 0 || sourceIndex.row() >= standard->rowCount()) {
        return false;
    }
    return standard->removeRow(sourceIndex.row());
}

bool ChatListView::removeChatItemByName(const QString& name)
{
    const int row = findRowByName(name);
    if (row < 0) {
        return false;
    }
    return removeChatItem(row);
}

int ChatListView::removeChatItemsByName(const QString& name)
{
    QStandardItemModel* model = ensureStandardModel();
    QList<int> rows = findRowsByName(name);
    if (rows.isEmpty()) {
        return 0;
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    int count = 0;
    for (int row : rows) {
        if (row >= 0 && row < model->rowCount() && model->removeRow(row)) {
            ++count;
        }
    }
    return count;
}

void ChatListView::clearChats()
{
    if (m_standardModel) {
        m_standardModel->clear();
    }
}

void ChatListView::onItemClicked(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }
    const QString name = index.data(ChatListNameRole).toString();
    const QString message = index.data(ChatListMessageRole).toString();
    const QString time = index.data(ChatListTimeRole).toString();
    const QColor avatarColor = index.data(ChatListAvatarColorRole).value<QColor>();
    const int unreadCount = index.data(ChatListUnreadCountRole).toInt();
    emit chatItemActivated(name, message, time, avatarColor, unreadCount);
}

ChatListDelegate::Style ChatListView::currentStyle() const
{
    return m_delegate ? m_delegate->style() : ChatListDelegate::Style();
}

void ChatListView::setStyle(const ChatListDelegate::Style& style)
{
    if (m_delegate) {
        m_delegate->setStyle(style);
    }
    updateViewStyleSheet();
    viewport()->update();
}

QStandardItemModel* ChatListView::ensureStandardModel()
{
    if (!m_standardModel) {
        QAbstractItemModel* current = model();
        if (auto* standard = qobject_cast<QStandardItemModel*>(current)) {
            m_standardModel = standard;
            return m_standardModel;
        }
        if (auto* proxy = qobject_cast<QSortFilterProxyModel*>(current)) {
            auto* sourceStandard = qobject_cast<QStandardItemModel*>(proxy->sourceModel());
            if (sourceStandard) {
                m_standardModel = sourceStandard;
                return m_standardModel;
            }
            m_standardModel = new QStandardItemModel(this);
            proxy->setSourceModel(m_standardModel);
            return m_standardModel;
        }
    }
    if (!m_standardModel) {
        m_standardModel = new QStandardItemModel(this);
        setModel(m_standardModel);
    }
    return m_standardModel;
}

void ChatListView::updateViewStyleSheet()
{
    const QColor bg = m_delegate ? m_delegate->style().backgroundColor : QColor(Qt::white);
    setProperty("chatListBg", bg.name());
    style()->unpolish(this);
    style()->polish(this);
}
