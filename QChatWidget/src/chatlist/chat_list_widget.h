#ifndef CHAT_LIST_WIDGET_H
#define CHAT_LIST_WIDGET_H

#include "chat_list_delegate.h"
#include <QHash>
#include <QItemSelection>
#include <QList>
#include <QModelIndex>
#include <QVariant>
#include <QWidget>
#include <Qt>

class QLineEdit;
class QVBoxLayout;
class QColor;
class ChatListView;
class QToolButton;
class QMenu;
class QAction;

class ChatListWidget : public QWidget {
    Q_OBJECT

public:
    explicit ChatListWidget(QWidget* parent = nullptr);
    ~ChatListWidget();

    ChatListView* listView() const;
    QLineEdit* searchBar() const;

    void applyDefaultStyle();

    void setSearchPlaceholder(const QString& text);
    void setSearchVisible(bool visible);
    void setSearchStyleSheet(const QString& style);
    void enableSearchFiltering(bool enabled);
    void setSearchRoles(const QList<int>& roles);
    void setSearchCaseSensitivity(Qt::CaseSensitivity sensitivity);
    QAction* addHeaderAction(const QString& text, const QVariant& data = QVariant());
    void setHeaderActions(const QList<QAction*>& actions);
    void clearHeaderActions();
    bool applyStyleSheetFile(const QString& fileNameOrPath);
    void setItemHeight(int height);
    void setAvatarSize(int size);
    void setAvatarShape(ChatListDelegate::AvatarShape shape);
    void setAvatarCornerRadius(int radius);
    void setItemMargins(int margin);
    void setBadgeSize(int size);
    void setItemSize(int height, int avatarSize, int margin);

    // 右键菜单定制
    void setContextMenuActions(const QList<QAction*>& actions);
    void clearContextMenuActions();

    // 右键菜单请求信号中的 index 信息
    QModelIndex contextIndex() const;
    QModelIndex contextSourceIndex() const;

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
    bool removeCurrentChat();
    bool removeChatItemByName(const QString& name);
    int removeChatItemsByName(const QString& name);
    void clearChats();

signals:
    void searchTextChanged(const QString& text);
    void chatItemActivated(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount);
    void chatItemRemoved(int row);
    void chatItemRenamed(int row, const QString& name);
    void headerActionTriggered(QAction* action);
    void contextMenuRequested(const QModelIndex& index, const QPoint& globalPos);
    void clicked(const QModelIndex& index);
    void doubleClicked(const QModelIndex& index);
    void pressed(const QModelIndex& index);
    void activated(const QModelIndex& index);
    void entered(const QModelIndex& index);
    void viewportEntered();
    void currentChanged(const QModelIndex& current, const QModelIndex& previous);
    void selectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

private:
    void setupUi();
    void applyFilterText(const QString& text);
    void wireSelectionSignals();
    void ensureContextMenu();
    QModelIndex sourceIndexFor(const QModelIndex& index) const;
    void renameCurrentItem();
    void removeCurrentItem();

    QLineEdit* m_searchBar;
    ChatListView* m_listView;
    QToolButton* m_moreButton;
    QMenu* m_moreMenu;
    QMenu* m_contextMenu = nullptr;
    QAction* m_renameAction = nullptr;
    QAction* m_removeAction = nullptr;
    QModelIndex m_contextIndex;
    bool m_customContextActions = false;
    class ChatListFilterModel* m_filterModel;
    class QItemSelectionModel* m_selectionModel = nullptr;
    bool m_filterEnabled = false;
    QVBoxLayout* m_layout;
};

#endif // CHAT_LIST_WIDGET_H
