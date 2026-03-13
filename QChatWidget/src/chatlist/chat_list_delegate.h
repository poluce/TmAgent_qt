#ifndef CHAT_LIST_DELEGATE_H
#define CHAT_LIST_DELEGATE_H

#include "chat_list_roles.h"
#include <QColor>
#include <QFont>
#include <QStyledItemDelegate>

class ChatListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    enum AvatarShape {
        AvatarCircle,
        AvatarRoundedRect,
        AvatarSquare
    };

    struct Style {
        int itemHeight = 84;
        int avatarSize = 54;
        int margin = 10;
        int badgeSize = 18;
        int itemCornerRadius = 14;
        int itemInsetX = 12;
        int itemInsetY = 4;
        int itemBorderWidth = 1;
        int avatarCornerRadius = 12;
        AvatarShape avatarShape = AvatarRoundedRect;

        QColor backgroundColor = Qt::white;
        QColor hoverColor = Qt::white;
        QColor selectedColor = Qt::white;
        QColor borderColor = QColor(229, 231, 235);
        QColor hoverBorderColor = QColor(214, 220, 228);
        QColor selectedBorderColor = QColor(185, 201, 229);
        QColor nameColor = QColor(25, 25, 25);
        QColor messageColor = QColor(168, 174, 183);
        QColor timeColor = QColor(176, 182, 190);
        QColor separatorColor = QColor(230, 230, 230);
        QColor badgeColor = Qt::red;
        QColor badgeTextColor = Qt::white;

        QFont nameFont = QFont("Microsoft YaHei UI", 14, QFont::Normal);
        QFont messageFont = QFont("Microsoft YaHei UI", 11);
        QFont timeFont = QFont("Microsoft YaHei UI", 11);
        QFont badgeFont = QFont("Microsoft YaHei UI", 10);

        bool showSeparator = true;
        bool showUnreadBadge = true;
    };

    explicit ChatListDelegate(QObject* parent = nullptr);

    void setStyle(const Style& style);
    Style style() const;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    Style m_style;
};

#endif // CHAT_LIST_DELEGATE_H
