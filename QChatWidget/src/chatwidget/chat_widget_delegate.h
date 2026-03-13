#ifndef CHAT_WIDGET_DELEGATE_H
#define CHAT_WIDGET_DELEGATE_H

#include <QColor>
#include <QFont>
#include <QStyledItemDelegate>

class ChatWidgetDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    struct Style {
        int avatarSize = 40;
        int margin = 10;
        int bubblePadding = 10;
        int bubbleRadius = 14;
        int nameSpacing = 4;

        QColor myBubbleColor = QColor(242, 244, 247);
        QColor otherBubbleColor = QColor(242, 244, 247);
        QColor myAvatarColor = QColor(0, 120, 215);
        QColor otherAvatarColor = QColor(200, 200, 200);
        QColor nameColor = QColor(120, 120, 120);
        QColor myTextColor = QColor(25, 25, 25);
        QColor otherTextColor = QColor(25, 25, 25);
        QColor backgroundColor = QColor(245, 245, 245);
        QColor timestampColor = QColor(140, 140, 140);
        QColor statusColor = QColor(120, 120, 120);
        QColor systemTextColor = QColor(110, 110, 110);
        QColor systemBubbleColor = QColor(230, 230, 230);
        QColor selectionBorderColor = QColor(59, 130, 246);
        QColor reactionChipColor = QColor(239, 239, 244);
        QColor reactionTextColor = QColor(60, 60, 60);
        QColor replyBorderColor = QColor(200, 200, 200);
        QColor replyTextColor = QColor(90, 90, 90);
        QColor mentionHighlightColor = QColor(255, 233, 198);
        QColor searchHighlightColor = QColor(255, 241, 118);
        QColor fileCardColor = QColor(250, 250, 252);
        QColor fileBorderColor = QColor(220, 220, 220);

        QFont messageFont = QFont("Microsoft YaHei", 9);
        QFont avatarFont = QFont("Microsoft YaHei", 10, QFont::Bold);
        QFont nameFont = QFont("Microsoft YaHei", 9);
        QFont timestampFont = QFont("Microsoft YaHei", 8);
        QFont statusFont = QFont("Microsoft YaHei", 8);
        QFont systemFont = QFont("Microsoft YaHei", 9);
        QFont reactionFont = QFont("Microsoft YaHei", 9);
        QFont replyFont = QFont("Microsoft YaHei", 9);
    };

    explicit ChatWidgetDelegate(QObject* parent = nullptr);

    void setStyle(const Style& style);
    Style style() const;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QRect avatarRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
    QRect fileCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;

private:
    Style m_style;
};

#endif // CHAT_WIDGET_DELEGATE_H
