#ifndef CHAT_LIST_VIEW_H
#define CHAT_LIST_VIEW_H

#include "chat_list_delegate.h"
#include "chat_list_roles.h"
#include <QColor>
#include <QHash>
#include <QList>
#include <QListView>
#include <QModelIndex>
#include <QVariant>

class QStandardItemModel;
class QFont;

class ChatListView : public QListView {
    Q_OBJECT
public:
    explicit ChatListView(QWidget* parent = nullptr);

    ChatListDelegate* chatDelegate() const;
    void setChatDelegate(ChatListDelegate* delegate);
    QStandardItemModel* standardModel();

    ChatListDelegate::Style currentStyle() const;
    void setStyle(const ChatListDelegate::Style& style);

    void setItemHeight(int height);
    void setAvatarSize(int size);
    void setAvatarShape(ChatListDelegate::AvatarShape shape);
    void setAvatarCornerRadius(int radius);
    void setMargins(int margin);
    void setBadgeSize(int size);
    void setShowSeparator(bool show);
    void setShowUnreadBadge(bool show);

    void setBackgroundColor(const QColor& color);
    void setHoverColor(const QColor& color);
    void setSelectedColor(const QColor& color);
    void setNameColor(const QColor& color);
    void setMessageColor(const QColor& color);
    void setTimeColor(const QColor& color);
    void setSeparatorColor(const QColor& color);
    void setBadgeColor(const QColor& color);
    void setBadgeTextColor(const QColor& color);

    void setNameFont(const QFont& font);
    void setMessageFont(const QFont& font);
    void setTimeFont(const QFont& font);
    void setBadgeFont(const QFont& font);

    int addChatItem(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount = 0);
    void updateChatItem(int row, const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount);
    bool updateChatItemData(int row, int role, const QVariant& value);
    bool updateChatItemData(int row, const QHash<int, QVariant>& values);
    int findRowByName(const QString& name) const;
    QList<int> findRowsByName(const QString& name) const;
    bool updateChatItemByName(const QString& name, int role, const QVariant& value);
    bool updateChatItemByName(const QString& name, const QHash<int, QVariant>& values);
    int updateChatItemsByName(const QString& name, int role, const QVariant& value);
    int updateChatItemsByName(const QString& name, const QHash<int, QVariant>& values);
    bool removeChatItem(int row);
    bool removeChatItem(const QModelIndex& index);
    bool removeChatItemByName(const QString& name);
    int removeChatItemsByName(const QString& name);
    void clearChats();

signals:
    void chatItemActivated(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount);

private slots:
    void onItemClicked(const QModelIndex& index);

private:
    QStandardItemModel* ensureStandardModel();
    const QStandardItemModel* resolveSourceModel() const;
    void updateViewStyleSheet();

    template<typename T>
    void updateStyleField(T ChatListDelegate::Style::*field, const T& value);

    ChatListDelegate* m_delegate = nullptr;
    QStandardItemModel* m_standardModel = nullptr;
};

#endif // CHAT_LIST_VIEW_H
