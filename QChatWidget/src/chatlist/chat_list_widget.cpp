#include "chat_list_widget.h"
#include "chat_list_filter_model.h"
#include "chat_list_roles.h"
#include "chat_list_view.h"
#include "qss_utils.h"
#include <QAction>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

ChatListWidget::ChatListWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

ChatListWidget::~ChatListWidget() = default;

void ChatListWidget::setupUi()
{
    const int kOuterVerticalMargin = 2;

    setObjectName("chatListWidget");
    m_searchBar = new QLineEdit(this);
    m_searchBar->setObjectName("chatListSearchBar");
    m_searchBar->setPlaceholderText(tr("搜索"));

    m_listView = new ChatListView(this);
    m_filterModel = new ChatListFilterModel(this);
    m_filterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    applyDefaultStyle();

    m_moreButton = new QToolButton(this);
    m_moreButton->setObjectName("chatListMoreButton");
    m_moreButton->setText("+");
    m_moreButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    m_moreButton->setFixedWidth(44);

    m_moreMenu = new QMenu(this);
    m_moreMenu->setObjectName("chatListHeaderMenu");
    m_moreButton->setMenu(m_moreMenu);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(kOuterVerticalMargin);

    QWidget* headerBar = new QWidget(this);
    headerBar->setObjectName("chatListHeaderBar");

    QHBoxLayout* headerBarLayout = new QHBoxLayout(headerBar);
    headerBarLayout->setContentsMargins(10, 10, 10, 10);
    headerBarLayout->setSpacing(6);
    headerBarLayout->addWidget(m_searchBar);
    headerBarLayout->addWidget(m_moreButton);

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, kOuterVerticalMargin, 0, 0);
    headerLayout->setSpacing(0);
    headerLayout->addWidget(headerBar);

    m_layout->addLayout(headerLayout);
    m_layout->addWidget(m_listView);

    connect(m_searchBar, &QLineEdit::textChanged, this, &ChatListWidget::searchTextChanged);
    connect(m_searchBar, &QLineEdit::textChanged, this, &ChatListWidget::applyFilterText);
    connect(m_listView, &ChatListView::chatItemActivated, this, &ChatListWidget::chatItemActivated);
    connect(m_listView, &QListView::clicked, this, &ChatListWidget::clicked);
    connect(m_listView, &QListView::doubleClicked, this, &ChatListWidget::doubleClicked);
    connect(m_listView, &QListView::pressed, this, &ChatListWidget::pressed);
    connect(m_listView, &QListView::activated, this, &ChatListWidget::activated);
    connect(m_listView, &QListView::entered, this, &ChatListWidget::entered);
    connect(m_listView, &QListView::viewportEntered, this, &ChatListWidget::viewportEntered);

    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex index = m_listView->indexAt(pos);
        if (!index.isValid()) {
            return;
        }
        m_listView->setCurrentIndex(index);
        m_contextIndex = index;
        const QPoint globalPos = m_listView->viewport()->mapToGlobal(pos);
        emit contextMenuRequested(index, globalPos);
        ensureContextMenu();
        if (m_contextMenu)
            m_contextMenu->exec(globalPos);
    });

    wireSelectionSignals();
}

ChatListView* ChatListWidget::listView() const
{
    return m_listView;
}

QLineEdit* ChatListWidget::searchBar() const
{
    return m_searchBar;
}

void ChatListWidget::applyDefaultStyle()
{
    ChatListDelegate::Style style;
    style.showSeparator = false;
    m_listView->setStyle(style);
}

void ChatListWidget::ensureContextMenu()
{
    if (m_contextMenu)
        return;
    if (m_customContextActions)
        return; // 宿主已通过 setContextMenuActions() 接管
    m_contextMenu = new QMenu(m_listView);
    m_renameAction = m_contextMenu->addAction(tr("重命名"));
    connect(m_renameAction, &QAction::triggered, this, &ChatListWidget::renameCurrentItem);
    m_removeAction = m_contextMenu->addAction(tr("删除"));
    connect(m_removeAction, &QAction::triggered, this, &ChatListWidget::removeCurrentItem);
}

QModelIndex ChatListWidget::sourceIndexFor(const QModelIndex& index) const
{
    if (!index.isValid())
        return QModelIndex();
    QModelIndex sourceIndex = index;
    if (auto* proxy = qobject_cast<QSortFilterProxyModel*>(m_listView->model())) {
        if (index.model() == proxy) {
            sourceIndex = proxy->mapToSource(index);
        }
    }
    return sourceIndex;
}

void ChatListWidget::renameCurrentItem()
{
    if (!m_contextIndex.isValid())
        return;
    const QModelIndex sourceIndex = sourceIndexFor(m_contextIndex);
    if (!sourceIndex.isValid())
        return;
    const int sourceRow = sourceIndex.row();
    const QString currentName = sourceIndex.data(ChatListNameRole).toString();
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("重命名会话"), tr("名称"), QLineEdit::Normal, currentName, &ok);
    if (!ok)
        return;
    name = name.trimmed();
    if (name.isEmpty())
        return;
    updateChatItemData(sourceRow, ChatListNameRole, name);
    emit chatItemRenamed(sourceRow, name);
}

void ChatListWidget::removeCurrentItem()
{
    if (!m_contextIndex.isValid())
        return;
    int sourceRow = -1;
    const QModelIndex sourceIndex = sourceIndexFor(m_contextIndex);
    if (sourceIndex.isValid())
        sourceRow = sourceIndex.row();
    if (removeChatItem(m_contextIndex)) {
        emit chatItemRemoved(sourceRow);
    }
}

void ChatListWidget::setContextMenuActions(const QList<QAction*>& actions)
{
    m_customContextActions = true;
    delete m_contextMenu;
    m_contextMenu = new QMenu(m_listView);
    m_renameAction = nullptr;
    m_removeAction = nullptr;
    for (QAction* action : actions) {
        if (action)
            m_contextMenu->addAction(action);
    }
}

void ChatListWidget::clearContextMenuActions()
{
    m_customContextActions = false;
    delete m_contextMenu;
    m_contextMenu = nullptr;
    m_renameAction = nullptr;
    m_removeAction = nullptr;
}

QModelIndex ChatListWidget::contextIndex() const
{
    return m_contextIndex;
}

QModelIndex ChatListWidget::contextSourceIndex() const
{
    return sourceIndexFor(m_contextIndex);
}

bool ChatListWidget::applyStyleSheetFile(const QString& fileNameOrPath)
{
    if (fileNameOrPath.trimmed().isEmpty()) {
        return false;
    }
    return QssUtils::applyStyleSheetFromFile(this, fileNameOrPath);
}

void ChatListWidget::setSearchPlaceholder(const QString& text)
{
    m_searchBar->setPlaceholderText(text);
}

void ChatListWidget::setSearchVisible(bool visible)
{
    m_searchBar->setVisible(visible);
}

void ChatListWidget::setSearchStyleSheet(const QString& style)
{
    m_searchBar->setStyleSheet(style);
}

void ChatListWidget::enableSearchFiltering(bool enabled)
{
    m_filterEnabled = enabled;
    if (m_filterEnabled) {
        QAbstractItemModel* source = m_listView->standardModel();
        m_filterModel->setSourceModel(source);
        m_listView->setModel(m_filterModel);
        if (m_filterModel->searchRoles().isEmpty()) {
            m_filterModel->setSearchRoles(QList<int>() << ChatListNameRole << ChatListMessageRole);
        }
        applyFilterText(m_searchBar->text());
    } else {
        if (m_filterModel->sourceModel()) {
            m_listView->setModel(m_filterModel->sourceModel());
        }
    }
    wireSelectionSignals();
}

void ChatListWidget::setSearchRoles(const QList<int>& roles)
{
    m_filterModel->setSearchRoles(roles);
}

void ChatListWidget::setSearchCaseSensitivity(Qt::CaseSensitivity sensitivity)
{
    m_filterModel->setFilterCaseSensitivity(sensitivity);
}

void ChatListWidget::setItemHeight(int height)
{
    m_listView->setItemHeight(height);
}

void ChatListWidget::setAvatarSize(int size)
{
    m_listView->setAvatarSize(size);
}

void ChatListWidget::setAvatarShape(ChatListDelegate::AvatarShape shape)
{
    m_listView->setAvatarShape(shape);
}

void ChatListWidget::setAvatarCornerRadius(int radius)
{
    m_listView->setAvatarCornerRadius(radius);
}

void ChatListWidget::setItemMargins(int margin)
{
    m_listView->setMargins(margin);
}

void ChatListWidget::setBadgeSize(int size)
{
    m_listView->setBadgeSize(size);
}

void ChatListWidget::setItemSize(int height, int avatarSize, int margin)
{
    m_listView->setItemHeight(height);
    m_listView->setAvatarSize(avatarSize);
    m_listView->setMargins(margin);
}

QAction* ChatListWidget::addHeaderAction(const QString& text, const QVariant& data)
{
    QAction* action = m_moreMenu->addAction(text);
    action->setData(data);
    connect(action, &QAction::triggered, this, [this, action]() {
        emit headerActionTriggered(action);
    });
    return action;
}

void ChatListWidget::setHeaderActions(const QList<QAction*>& actions)
{
    m_moreMenu->clear();
    for (QAction* action : actions) {
        if (!action) {
            continue;
        }
        m_moreMenu->addAction(action);
        connect(action, &QAction::triggered, this, [this, action]() {
            emit headerActionTriggered(action);
        });
    }
}

void ChatListWidget::clearHeaderActions()
{
    m_moreMenu->clear();
}

int ChatListWidget::addChatItem(const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount)
{
    return m_listView->addChatItem(name, message, time, avatarColor, unreadCount);
}

void ChatListWidget::updateChatItem(int row, const QString& name, const QString& message, const QString& time, const QColor& avatarColor, int unreadCount)
{
    m_listView->updateChatItem(row, name, message, time, avatarColor, unreadCount);
}

bool ChatListWidget::updateChatItemData(int row, int role, const QVariant& value)
{
    return m_listView->updateChatItemData(row, role, value);
}

bool ChatListWidget::updateChatItemData(int row, const QHash<int, QVariant>& values)
{
    return m_listView->updateChatItemData(row, values);
}

int ChatListWidget::findRowByName(const QString& name) const
{
    return m_listView->findRowByName(name);
}

QList<int> ChatListWidget::findRowsByName(const QString& name) const
{
    return m_listView->findRowsByName(name);
}

bool ChatListWidget::updateChatItemByName(const QString& name, int role, const QVariant& value)
{
    return m_listView->updateChatItemByName(name, role, value);
}

bool ChatListWidget::updateChatItemByName(const QString& name, const QHash<int, QVariant>& values)
{
    return m_listView->updateChatItemByName(name, values);
}

int ChatListWidget::updateChatItemsByName(const QString& name, int role, const QVariant& value)
{
    return m_listView->updateChatItemsByName(name, role, value);
}

int ChatListWidget::updateChatItemsByName(const QString& name, const QHash<int, QVariant>& values)
{
    return m_listView->updateChatItemsByName(name, values);
}

bool ChatListWidget::removeChatItem(int row)
{
    return m_listView->removeChatItem(row);
}

bool ChatListWidget::removeChatItem(const QModelIndex& index)
{
    return m_listView->removeChatItem(index);
}

bool ChatListWidget::removeCurrentChat()
{
    return m_listView->removeChatItem(m_listView->currentIndex());
}

bool ChatListWidget::removeChatItemByName(const QString& name)
{
    return m_listView->removeChatItemByName(name);
}

int ChatListWidget::removeChatItemsByName(const QString& name)
{
    return m_listView->removeChatItemsByName(name);
}

void ChatListWidget::clearChats()
{
    m_listView->clearChats();
}

void ChatListWidget::applyFilterText(const QString& text)
{
    if (!m_filterEnabled) {
        return;
    }
    const auto options = (m_filterModel->filterCaseSensitivity() == Qt::CaseInsensitive)
                             ? QRegularExpression::CaseInsensitiveOption
                             : QRegularExpression::NoPatternOption;
    m_filterModel->setFilterRegularExpression(
        QRegularExpression(QRegularExpression::escape(text), options));
}

void ChatListWidget::wireSelectionSignals()
{
    QItemSelectionModel* current = m_listView->selectionModel();
    if (current == m_selectionModel) {
        return;
    }
    if (m_selectionModel) {
        disconnect(m_selectionModel, nullptr, this, nullptr);
    }
    m_selectionModel = current;
    if (!m_selectionModel) {
        return;
    }
    connect(m_selectionModel, &QItemSelectionModel::currentChanged, this, &ChatListWidget::currentChanged);
    connect(m_selectionModel, &QItemSelectionModel::selectionChanged, this, &ChatListWidget::selectionChanged);
}
