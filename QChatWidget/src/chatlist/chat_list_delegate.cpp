#include "chat_list_delegate.h"
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

ChatListDelegate::ChatListDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void ChatListDelegate::setStyle(const Style &style)
{
    m_style = style;
}

ChatListDelegate::Style ChatListDelegate::style() const
{
    return m_style;
}

QSize ChatListDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(index);
    const QFontMetrics fmName(m_style.nameFont);
    const QFontMetrics fmMsg(m_style.messageFont);
    const int textSpacing = qMax(4, fmMsg.leading());
    const int textBlockHeight = fmName.height() + fmMsg.height() + textSpacing;
    const int contentHeight = qMax(m_style.avatarSize, textBlockHeight);
    const int minHeight = contentHeight + 2 * m_style.margin;
    const int height = (m_style.itemHeight > 0) ? qMax(m_style.itemHeight, minHeight) : minHeight;
    return QSize(option.rect.width(), height);
}

void ChatListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 1. 获取数据
    const QString name = index.data(ChatListNameRole).toString();
    const QString message = index.data(ChatListMessageRole).toString();
    const QString time = index.data(ChatListTimeRole).toString();
    const QColor avatarColor = index.data(ChatListAvatarColorRole).value<QColor>();
    const QString avatarPath = index.data(ChatListAvatarPathRole).toString().trimmed();
    const int unreadCount = index.data(ChatListUnreadCountRole).toInt();

    // 2. 绘制卡片背景（圆角 + 边框）
    QRect rect = option.rect.adjusted(m_style.itemInsetX,
                                      m_style.itemInsetY,
                                      -m_style.itemInsetX,
                                      -m_style.itemInsetY);
    QColor fillColor = m_style.backgroundColor;
    QColor borderColor = m_style.borderColor;
    if (option.state & QStyle::State_Selected) {
        fillColor = m_style.selectedColor;
        borderColor = m_style.selectedBorderColor;
    } else if (option.state & QStyle::State_MouseOver) {
        fillColor = m_style.hoverColor;
        borderColor = m_style.hoverBorderColor;
    }
    painter->setPen(QPen(borderColor, m_style.itemBorderWidth));
    painter->setBrush(fillColor);
    const QRectF cardRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    painter->drawRoundedRect(cardRect, m_style.itemCornerRadius, m_style.itemCornerRadius);

    // 3. 布局参数
    const int avatarSize = m_style.avatarSize;
    const int margin = m_style.margin;
    const int textLeftMargin = margin + avatarSize + margin;
    const QFontMetrics fmName(m_style.nameFont);
    const QFontMetrics fmMsg(m_style.messageFont);
    const QFontMetrics fmTime(m_style.timeFont);
    const int textSpacing = qMax(4, fmMsg.leading());
    const int contentTop = rect.top() + margin;
    const int contentBottom = rect.bottom() - margin;

    // 4. 绘制头像
    QRect avatarRect(rect.left() + margin,
                     rect.top() + (rect.height() - avatarSize) / 2,
                     avatarSize,
                     avatarSize);
    QPainterPath avatarClipPath;
    if (m_style.avatarShape == AvatarSquare) {
        avatarClipPath.addRoundedRect(avatarRect, 6, 6);
    } else if (m_style.avatarShape == AvatarRoundedRect) {
        const int radius = qMax(0, m_style.avatarCornerRadius);
        avatarClipPath.addRoundedRect(avatarRect, radius, radius);
    } else {
        avatarClipPath.addEllipse(avatarRect);
    }

    bool drewAvatarImage = false;
    if (!avatarPath.isEmpty()) {
        QPixmap avatarPixmap(avatarPath);
        if (!avatarPixmap.isNull()) {
            painter->save();
            painter->setClipPath(avatarClipPath);
            painter->drawPixmap(
                avatarRect,
                avatarPixmap.scaled(avatarRect.size(),
                                    Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation));
            painter->restore();
            drewAvatarImage = true;
        }
    }

    if (!drewAvatarImage) {
        painter->save();
        painter->setClipPath(avatarClipPath);
        painter->fillRect(avatarRect, avatarColor);
        painter->restore();
    }

    // 5. 绘制未读红点
    if (m_style.showUnreadBadge && unreadCount > 0) {
        const int badgeSize = m_style.badgeSize;
        QRect badgeRect(avatarRect.right() - 6, avatarRect.top() - 6, badgeSize, badgeSize);
        painter->setBrush(m_style.badgeColor);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(badgeRect);

        painter->setPen(m_style.badgeTextColor);
        painter->setFont(m_style.badgeFont);
        painter->drawText(badgeRect, Qt::AlignCenter, QString::number(unreadCount));
    }

    // 6. 绘制昵称
    painter->setPen(m_style.nameColor);
    painter->setFont(m_style.nameFont);

    const int timeWidth = fmTime.horizontalAdvance(time) + margin;
    const int nameTop = contentTop;
    const int minMsgTop = nameTop + fmName.height() + textSpacing;
    const int msgTop = qMax(contentBottom - fmMsg.height(), minMsgTop);

    const int nameWidth = qMax(0, rect.width() - textLeftMargin - timeWidth - margin);
    QRect nameRect(rect.left() + textLeftMargin,
                   nameTop,
                   nameWidth,
                   fmName.height());
    QString elidedName = painter->fontMetrics().elidedText(name, Qt::ElideRight, nameRect.width());
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    // 7. 绘制时间
    painter->setPen(m_style.timeColor);
    painter->setFont(m_style.timeFont);
    QRect timeRect(rect.right() - timeWidth,
                   nameTop,
                   timeWidth - margin,
                   fmTime.height());
    painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter, time);

    // 8. 绘制消息内容
    painter->setPen(m_style.messageColor);
    painter->setFont(m_style.messageFont);

    const int msgWidth = qMax(0, rect.width() - textLeftMargin - margin);
    QRect msgRect(rect.left() + textLeftMargin,
                  msgTop,
                  msgWidth,
                  fmMsg.height());
    QString elidedMsg = painter->fontMetrics().elidedText(message, Qt::ElideRight, msgRect.width());
    painter->drawText(msgRect, Qt::AlignLeft | Qt::AlignVCenter, elidedMsg);

    // 9. 分隔线（卡片样式下默认建议关闭）
    if (m_style.showSeparator) {
        painter->setPen(m_style.separatorColor);
        painter->drawLine(textLeftMargin, rect.bottom(), rect.right(), rect.bottom());
    }

    painter->restore();
}
