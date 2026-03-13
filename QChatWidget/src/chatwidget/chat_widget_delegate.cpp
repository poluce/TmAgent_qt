#include "chat_widget_delegate.h"
#include "chat_widget_markdown_utils.h"
#include "chat_widget_model.h"
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTextDocument>
#include <QtMath>

namespace {
const int kLineSpacing = 6;
const int kReplyPadding = 6;
const int kReactionPaddingH = 8;
const int kReactionPaddingV = 2;
const int kAttachmentWidth = 180;
const int kAttachmentHeight = 120;
const int kImageMaxWidth = 240;
const int kImageMaxHeight = 240;
const int kImageMinSize = 60;
const int kVoiceBarWidth = 160;
const int kVoiceBarHeight = 36;
const int kFileCardHeight = 56;
const int kSystemPaddingH = 16;
const int kSystemPaddingV = 6;
const int kFooterTextHPadding = 4;
const int kFooterTextVPadding = 2;
const int kFooterBottomSafety = 3;

QString formatTimestamp(const QDateTime& timestamp)
{
    if (!timestamp.isValid()) {
        return QString();
    }
    return timestamp.toString("HH:mm");
}

QString applyHighlights(const QString& html, const QStringList& mentions, const QString& keyword,
                        const ChatWidgetDelegate::Style& style)
{
    QString highlighted = html;
    const QString mentionColor = style.mentionHighlightColor.name();
    const QString searchColor = style.searchHighlightColor.name();

    for (const QString& mention : mentions) {
        if (mention.trimmed().isEmpty()) {
            continue;
        }
        const QString replacement =
            QString("<span style=\"background-color:%1;\">%2</span>").arg(mentionColor, mention);
        highlighted.replace(mention, replacement, Qt::CaseSensitive);
    }

    if (!keyword.trimmed().isEmpty()) {
        const QString replacement =
            QString("<span style=\"background-color:%1;\">%2</span>").arg(searchColor, keyword);
        highlighted.replace(keyword, replacement, Qt::CaseInsensitive);
    }

    return highlighted;
}

QList<ChatWidgetReaction> reactionsFromVariant(const QVariant& value)
{
    QList<ChatWidgetReaction> reactions;
    const QVariantList list = value.toList();
    reactions.reserve(list.size());
    for (const QVariant& item : list) {
        const QVariantMap map = item.toMap();
        const QString emoji = map.value("emoji").toString();
        if (emoji.isEmpty()) {
            continue;
        }
        ChatWidgetReaction reaction;
        reaction.emoji = emoji;
        reaction.count = map.value("count").toInt();
        reactions.append(reaction);
    }
    return reactions;
}

bool isSystemType(ChatWidgetMessage::MessageType type)
{
    return type == ChatWidgetMessage::MessageType::System ||
           type == ChatWidgetMessage::MessageType::DateSeparator;
}

int textPixelWidth(const QFontMetrics& metrics, const QString& text)
{
    return qMax(metrics.horizontalAdvance(text), metrics.boundingRect(text).width());
}

int effectiveItemWidth(const QStyleOptionViewItem& option)
{
    int width = 0;
    if (option.widget) {
        width = option.widget->width();
        if (width <= 0 && option.widget->parentWidget())
            width = option.widget->parentWidget()->width();
    }
    if (width <= 0)
        width = option.rect.width();
    return qMax(120, width);
}

int calcReplyHeight(const QFont& replyFont, bool hasReply, bool hasForward)
{
    if (!hasReply && !hasForward)
        return 0;
    QFontMetrics metrics(replyFont);
    const int lines = (hasForward ? 1 : 0) + (hasReply ? 1 : 0);
    return lines * metrics.height() + kReplyPadding * 2;
}

int calcReactionHeight(const QFont& reactionFont, const QList<ChatWidgetReaction>& reactions)
{
    if (reactions.isEmpty())
        return 0;
    QFontMetrics metrics(reactionFont);
    return metrics.height() + kReactionPaddingV * 2;
}

int calcAttachmentHeight(bool hasImage, bool hasFile, bool hasVoice,
                         int imageWidth = 0, int imageHeight = 0)
{
    if (hasImage) {
        if (imageWidth > 0 && imageHeight > 0) {
            double scale = qMin(static_cast<double>(kImageMaxWidth) / imageWidth,
                                static_cast<double>(kImageMaxHeight) / imageHeight);
            if (scale > 1.0) scale = 1.0;
            int h = qRound(imageHeight * scale);
            return qMax(kImageMinSize, h);
        }
        return kAttachmentHeight;
    }
    if (hasVoice)
        return kVoiceBarHeight;
    if (hasFile)
        return kFileCardHeight;
    return 0;
}

int calcAttachmentWidth(bool hasImage, bool hasFile, bool hasVoice,
                        int imageWidth = 0, int imageHeight = 0)
{
    if (hasImage) {
        if (imageWidth > 0 && imageHeight > 0) {
            double scale = qMin(static_cast<double>(kImageMaxWidth) / imageWidth,
                                static_cast<double>(kImageMaxHeight) / imageHeight);
            if (scale > 1.0) scale = 1.0;
            int w = qRound(imageWidth * scale);
            return qMax(kImageMinSize, w);
        }
        return kAttachmentWidth;
    }
    if (hasVoice)
        return kVoiceBarWidth;
    if (hasFile)
        return 0; // determined by doc width
    return 0;
}

int calcMaxBubbleWidth(const QStyleOptionViewItem& option)
{
    const int w = effectiveItemWidth(option) * 0.6;
    return w > 0 ? w : 400;
}

struct IndexData {
    ChatWidgetMessage::MessageType type;
    bool isMine;
    QString content;
    QDateTime timestamp;
    QString senderName;
    QString senderId;
    QString avatarPath;
    QString imagePath;
    QString imageThumbnailPath;
    int imageWidth;
    int imageHeight;
    ChatWidgetMessage::ImageLoadState imageLoadState;
    QString fileName;
    qint64 fileSize;
    QString voicePath;
    int voiceDuration;
    ChatWidgetMessage::VoicePlayState voicePlayState;
    int voiceProgress;
    QString replySender;
    QString replyPreview;
    QString replyId;
    bool isForwarded;
    QString forwardedFrom;
    QStringList mentions;
    QString searchKeyword;
    QList<ChatWidgetReaction> reactions;

    bool hasImage;
    bool hasFile;
    bool hasVoice;
    bool hasReply;
    bool hasForward;

    static IndexData fromIndex(const QModelIndex& index)
    {
        IndexData d;
        d.type = static_cast<ChatWidgetMessage::MessageType>(
            index.data(ChatWidgetModel::ChatWidgetMessageTypeRole).toInt());
        d.isMine = index.data(ChatWidgetModel::ChatWidgetIsMineRole).toBool();
        d.content = index.data(ChatWidgetModel::ChatWidgetContentRole).toString();
        d.timestamp = index.data(ChatWidgetModel::ChatWidgetTimestampRole).toDateTime();
        d.senderName = index.data(ChatWidgetModel::ChatWidgetSenderRole).toString();
        d.senderId = index.data(ChatWidgetModel::ChatWidgetSenderIdRole).toString();
        d.avatarPath = index.data(ChatWidgetModel::ChatWidgetAvatarRole).toString();
        d.imagePath = index.data(ChatWidgetModel::ChatWidgetImagePathRole).toString();
        d.imageThumbnailPath = index.data(ChatWidgetModel::ChatWidgetImageThumbnailRole).toString();
        d.imageWidth = index.data(ChatWidgetModel::ChatWidgetImageWidthRole).toInt();
        d.imageHeight = index.data(ChatWidgetModel::ChatWidgetImageHeightRole).toInt();
        d.imageLoadState = static_cast<ChatWidgetMessage::ImageLoadState>(
            index.data(ChatWidgetModel::ChatWidgetImageLoadStateRole).toInt());
        d.fileName = index.data(ChatWidgetModel::ChatWidgetFileNameRole).toString();
        d.fileSize = index.data(ChatWidgetModel::ChatWidgetFileSizeRole).toLongLong();
        d.voicePath = index.data(ChatWidgetModel::ChatWidgetVoicePathRole).toString();
        d.voiceDuration = index.data(ChatWidgetModel::ChatWidgetVoiceDurationRole).toInt();
        d.voicePlayState = static_cast<ChatWidgetMessage::VoicePlayState>(
            index.data(ChatWidgetModel::ChatWidgetVoicePlayStateRole).toInt());
        d.voiceProgress = index.data(ChatWidgetModel::ChatWidgetVoicePlayProgressRole).toInt();
        d.replySender = index.data(ChatWidgetModel::ChatWidgetReplySenderRole).toString();
        d.replyPreview = index.data(ChatWidgetModel::ChatWidgetReplyPreviewRole).toString();
        d.replyId = index.data(ChatWidgetModel::ChatWidgetReplyToMessageIdRole).toString();
        d.isForwarded = index.data(ChatWidgetModel::ChatWidgetIsForwardedRole).toBool();
        d.forwardedFrom = index.data(ChatWidgetModel::ChatWidgetForwardedFromRole).toString();
        d.mentions = index.data(ChatWidgetModel::ChatWidgetMentionsRole).toStringList();
        d.searchKeyword = index.data(ChatWidgetModel::ChatWidgetSearchKeywordRole).toString();
        d.reactions = reactionsFromVariant(index.data(ChatWidgetModel::ChatWidgetReactionsRole));

        d.hasImage = !d.imagePath.isEmpty() || d.type == ChatWidgetMessage::MessageType::Image;
        d.hasFile = !d.fileName.isEmpty() || d.type == ChatWidgetMessage::MessageType::File;
        d.hasVoice = !d.voicePath.isEmpty() || d.type == ChatWidgetMessage::MessageType::Voice;
        d.hasReply = !d.replySender.isEmpty() || !d.replyPreview.isEmpty() || !d.replyId.isEmpty();
        d.hasForward = d.isForwarded || !d.forwardedFrom.isEmpty();
        return d;
    }
};
} // namespace

ChatWidgetDelegate::ChatWidgetDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ChatWidgetDelegate::setStyle(const Style& style)
{
    m_style = style;
}

ChatWidgetDelegate::Style ChatWidgetDelegate::style() const
{
    return m_style;
}

QSize ChatWidgetDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const IndexData d = IndexData::fromIndex(index);

    if (isSystemType(d.type)) {
        const QString text = d.content.isEmpty() && d.timestamp.isValid()
            ? d.timestamp.toString("yyyy-MM-dd")
            : d.content;
        QFontMetrics sysMetrics(m_style.systemFont);
        const int height = sysMetrics.height() + kSystemPaddingV * 2 + m_style.margin * 2;
        return QSize(0, height);
    }

    QString html = ChatWidgetMarkdownUtils::renderMarkdown(d.content);
    html = applyHighlights(html, d.mentions, d.searchKeyword, m_style);

    const int maxWidth = calcMaxBubbleWidth(option);
    QTextDocument doc;
    doc.setDefaultFont(m_style.messageFont);
    doc.setHtml(html);
    doc.setTextWidth(maxWidth);

    const int docHeight = qCeil(doc.size().height());
    const int replyHeight = calcReplyHeight(m_style.replyFont, d.hasReply, d.hasForward);
    const int attachmentHeight = calcAttachmentHeight(d.hasImage, d.hasFile, d.hasVoice,
                                                       d.imageWidth, d.imageHeight);
    const int reactionHeight = calcReactionHeight(m_style.reactionFont, d.reactions);

    int contentHeight = docHeight;
    if (replyHeight > 0)
        contentHeight += replyHeight + kLineSpacing;
    if (attachmentHeight > 0)
        contentHeight += attachmentHeight + kLineSpacing;
    if (reactionHeight > 0)
        contentHeight += reactionHeight + kLineSpacing;

    int totalHeight = contentHeight + (m_style.bubblePadding * 2) + (m_style.margin * 2);
    if (!d.isMine && !d.senderId.isEmpty() && !d.senderName.isEmpty()) {
        QFontMetrics nameMetrics(m_style.nameFont);
        totalHeight += nameMetrics.height() + m_style.nameSpacing;
    }

    const QString timestampText = formatTimestamp(d.timestamp);
    if (!timestampText.isEmpty()) {
        QFontMetrics timestampMetrics(m_style.timestampFont);
        const int footerTextHeight = timestampMetrics.height() + kFooterTextVPadding;
        totalHeight += footerTextHeight + kLineSpacing + kFooterBottomSafety;
    }

    return QSize(0, qMax(totalHeight, m_style.avatarSize + m_style.margin * 2));
}

void ChatWidgetDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const IndexData d = IndexData::fromIndex(index);

    if (isSystemType(d.type)) {
        const QString text = d.content.isEmpty() && d.timestamp.isValid()
            ? d.timestamp.toString("yyyy-MM-dd")
            : d.content;
        QFontMetrics sysMetrics(m_style.systemFont);
        const int textWidth = sysMetrics.horizontalAdvance(text);
        const int maxWidth = effectiveItemWidth(option) * 0.8;
        const int bubbleWidth = qMin(maxWidth, textWidth + kSystemPaddingH * 2);
        const int bubbleHeight = sysMetrics.height() + kSystemPaddingV * 2;
        const int centerX = option.rect.center().x() - bubbleWidth / 2;
        const int centerY = option.rect.center().y() - bubbleHeight / 2;
        QRect bubbleRect(centerX, centerY, bubbleWidth, bubbleHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_style.systemBubbleColor);
        painter->drawRoundedRect(bubbleRect, 10, 10);
        painter->setPen(m_style.systemTextColor);
        painter->setFont(m_style.systemFont);
        painter->drawText(bubbleRect, Qt::AlignCenter, text);
        painter->restore();
        return;
    }

    QString html = ChatWidgetMarkdownUtils::renderMarkdown(d.content);
    html = applyHighlights(html, d.mentions, d.searchKeyword, m_style);

    const QRect rect = option.rect;
    const int maxWidth = calcMaxBubbleWidth(option);

    QTextDocument doc;
    doc.setDefaultFont(m_style.messageFont);
    doc.setHtml(html);
    doc.setTextWidth(maxWidth);

    const int docWidth = qMin(maxWidth, qCeil(doc.idealWidth()));
    const int docHeight = qCeil(doc.size().height());
    const QSize docSize(docWidth, docHeight);

    int attachmentWidth = 0;
    int attachmentHeight = 0;
    if (d.hasImage) {
        attachmentWidth = calcAttachmentWidth(true, false, false, d.imageWidth, d.imageHeight);
        attachmentHeight = calcAttachmentHeight(true, false, false, d.imageWidth, d.imageHeight);
    } else if (d.hasVoice) {
        attachmentWidth = kVoiceBarWidth;
        attachmentHeight = kVoiceBarHeight;
    } else if (d.hasFile) {
        attachmentWidth = qMax(160, docWidth);
        attachmentHeight = kFileCardHeight;
    }

    const int replyHeight = calcReplyHeight(m_style.replyFont, d.hasReply, d.hasForward);
    const int reactionHeight = calcReactionHeight(m_style.reactionFont, d.reactions);

    QRect avatarRect = this->avatarRect(option, index);

    painter->setPen(Qt::NoPen);
    bool drawAvatarText = true;
    if (!d.avatarPath.isEmpty()) {
        QPixmap avatarPixmap(d.avatarPath);
        if (!avatarPixmap.isNull()) {
            QPainterPath clipPath;
            clipPath.addEllipse(avatarRect);
            painter->setClipPath(clipPath);
            painter->drawPixmap(avatarRect, avatarPixmap);
            painter->setClipping(false);
            drawAvatarText = false;
        }
    }
    if (drawAvatarText) {
        painter->setBrush(d.isMine ? m_style.myAvatarColor : m_style.otherAvatarColor);
        painter->drawEllipse(avatarRect);
        painter->setPen(Qt::white);
        painter->setFont(m_style.avatarFont);
        const QString avatarText = d.senderName.isEmpty() ? (d.isMine ? "Me" : "U") : d.senderName.left(1);
        painter->drawText(avatarRect, Qt::AlignCenter, avatarText);
    }

    // 绘制气泡
    QRect bubbleRect;
    const int contentWidth = qMax(docSize.width(), attachmentWidth);
    int contentHeight = docSize.height();
    if (replyHeight > 0)
        contentHeight += replyHeight + kLineSpacing;
    if (attachmentHeight > 0)
        contentHeight += attachmentHeight + kLineSpacing;
    if (reactionHeight > 0)
        contentHeight += reactionHeight + kLineSpacing;

    const int bubbleWidth = contentWidth + m_style.bubblePadding * 2;
    const int bubbleHeight = contentHeight + m_style.bubblePadding * 2;

    int contentTop = rect.top() + m_style.margin;
    if (!d.isMine && !d.senderId.isEmpty() && !d.senderName.isEmpty()) {
        painter->setPen(m_style.nameColor);
        painter->setFont(m_style.nameFont);
        QFontMetrics nameMetrics(m_style.nameFont);
        const int nameHeight = nameMetrics.height();
        QRect nameRect(avatarRect.right() + m_style.margin, contentTop,
                       rect.right() - avatarRect.right() - m_style.margin * 2, nameHeight);
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, d.senderName);
        contentTop += nameHeight + m_style.nameSpacing;
    }

    if (d.isMine) {
        bubbleRect = QRect(avatarRect.left() - m_style.margin - bubbleWidth, contentTop, bubbleWidth, bubbleHeight);
        painter->setBrush(m_style.myBubbleColor);
    } else {
        bubbleRect = QRect(avatarRect.right() + m_style.margin, contentTop, bubbleWidth, bubbleHeight);
        painter->setBrush(m_style.otherBubbleColor);
    }

    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(bubbleRect, m_style.bubbleRadius, m_style.bubbleRadius);

    if (option.state & QStyle::State_Selected) {
        QPen selectionPen(m_style.selectionBorderColor);
        selectionPen.setWidth(1);
        painter->setPen(selectionPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(bubbleRect.adjusted(1, 1, -1, -1), m_style.bubbleRadius, m_style.bubbleRadius);
    }

    QRect innerRect = bubbleRect.adjusted(m_style.bubblePadding, m_style.bubblePadding,
                                          -m_style.bubblePadding, -m_style.bubblePadding);
    int cursorY = innerRect.top();

    if (replyHeight > 0) {
        QRect replyRect(innerRect.left(), cursorY, innerRect.width(), replyHeight);
        painter->setPen(QPen(m_style.replyBorderColor));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(replyRect, 6, 6);
        painter->setPen(m_style.replyTextColor);
        painter->setFont(m_style.replyFont);
        int textY = replyRect.top() + kReplyPadding;
        QFontMetrics replyMetrics(m_style.replyFont);
        const int textWidth = replyRect.width() - kReplyPadding * 2;
        if (d.hasForward) {
            const QString forwardLabel = d.forwardedFrom.isEmpty()
                ? tr("转发")
                : tr("转发自 %1").arg(d.forwardedFrom);
            painter->drawText(QRect(replyRect.left() + kReplyPadding, textY, textWidth, replyMetrics.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              replyMetrics.elidedText(forwardLabel, Qt::ElideRight, textWidth));
            textY += replyMetrics.height();
        }
        if (d.hasReply) {
            const QString replyText = d.replySender.isEmpty()
                ? tr("回复: %1").arg(d.replyPreview)
                : tr("回复 %1: %2").arg(d.replySender, d.replyPreview);
            painter->drawText(QRect(replyRect.left() + kReplyPadding, textY, textWidth, replyMetrics.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              replyMetrics.elidedText(replyText, Qt::ElideRight, textWidth));
        }
        cursorY += replyHeight + kLineSpacing;
    }

    if (attachmentHeight > 0) {
        QRect attachRect(innerRect.left(), cursorY, qMin(innerRect.width(), attachmentWidth), attachmentHeight);

        if (d.hasImage) {
            // 加载状态判断
            if (d.imageLoadState == ChatWidgetMessage::ImageLoadState::Loading) {
                painter->setPen(QPen(m_style.fileBorderColor));
                painter->setBrush(m_style.fileCardColor);
                painter->drawRoundedRect(attachRect, 6, 6);
                painter->setPen(m_style.replyTextColor);
                painter->setFont(m_style.replyFont);
                painter->drawText(attachRect, Qt::AlignCenter, tr("加载中..."));
            } else if (d.imageLoadState == ChatWidgetMessage::ImageLoadState::Failed) {
                painter->setPen(QPen(m_style.fileBorderColor));
                painter->setBrush(m_style.fileCardColor);
                painter->drawRoundedRect(attachRect, 6, 6);
                painter->setPen(m_style.statusColor);
                painter->setFont(m_style.replyFont);
                painter->drawText(attachRect, Qt::AlignCenter, tr("加载失败"));
            } else {
                // 优先使用缩略图
                const QString& imgSrc = d.imageThumbnailPath.isEmpty() ? d.imagePath : d.imageThumbnailPath;
                QPixmap image(imgSrc);
                if (!image.isNull()) {
                    QPainterPath clipPath;
                    clipPath.addRoundedRect(attachRect, 6, 6);
                    painter->save();
                    painter->setClipPath(clipPath);
                    painter->drawPixmap(attachRect, image.scaled(attachRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    painter->restore();
                } else {
                    painter->setPen(QPen(m_style.fileBorderColor));
                    painter->setBrush(m_style.fileCardColor);
                    painter->drawRoundedRect(attachRect, 6, 6);
                    painter->setPen(m_style.replyTextColor);
                    painter->setFont(m_style.replyFont);
                    painter->drawText(attachRect, Qt::AlignCenter, QStringLiteral("Image"));
                }
            }
        } else if (d.hasVoice) {
            // 语音消息绘制：圆角背景 + 波形条 + 时长
            painter->setPen(Qt::NoPen);
            painter->setBrush(d.isMine ? m_style.myBubbleColor.darker(108) : m_style.otherBubbleColor.darker(108));
            painter->drawRoundedRect(attachRect, attachRect.height() / 2, attachRect.height() / 2);

            // 播放/暂停图标区域
            const int iconSize = 20;
            const int iconX = attachRect.left() + 8;
            const int iconY = attachRect.center().y() - iconSize / 2;
            painter->setPen(d.isMine ? m_style.myTextColor : m_style.otherTextColor);
            painter->setFont(m_style.replyFont);
            const bool isPlaying = (d.voicePlayState == ChatWidgetMessage::VoicePlayState::Playing);
            painter->drawText(QRect(iconX, iconY, iconSize, iconSize), Qt::AlignCenter,
                              isPlaying ? QStringLiteral("||") : QStringLiteral("▶"));

            // 波形条（简化为若干竖线）
            const int barStartX = iconX + iconSize + 6;
            const int barEndX = attachRect.right() - 40;
            const int barCount = qMin(20, (barEndX - barStartX) / 4);
            const int barCenterY = attachRect.center().y();
            QPen barPen(d.isMine ? m_style.myTextColor : m_style.otherTextColor);
            barPen.setWidth(2);
            barPen.setCapStyle(Qt::RoundCap);
            painter->setPen(barPen);
            for (int i = 0; i < barCount; ++i) {
                const int x = barStartX + i * 4;
                // 伪波形高度
                const int h = 4 + (i % 3 == 0 ? 8 : (i % 2 == 0 ? 5 : 3));
                // 已播放部分用深色，未播放用浅色
                const int progressBars = (d.voiceProgress > 0 && d.voiceDuration > 0)
                    ? (barCount * d.voiceProgress / d.voiceDuration) : 0;
                if (i >= progressBars) {
                    QPen lightPen(barPen);
                    lightPen.setColor(barPen.color().lighter(160));
                    painter->setPen(lightPen);
                }
                painter->drawLine(x, barCenterY - h / 2, x, barCenterY + h / 2);
                painter->setPen(barPen);
            }

            // 时长文字
            const int durSec = d.voiceDuration > 0 ? d.voiceDuration : 0;
            const QString durText = QStringLiteral("%1:%2")
                .arg(durSec / 60, 1, 10, QChar('0'))
                .arg(durSec % 60, 2, 10, QChar('0'));
            painter->setPen(d.isMine ? m_style.myTextColor : m_style.otherTextColor);
            painter->setFont(m_style.timestampFont);
            QFontMetrics durMetrics(m_style.timestampFont);
            painter->drawText(QRect(attachRect.right() - 36, attachRect.top(),
                                    34, attachRect.height()),
                              Qt::AlignCenter, durText);
        } else if (d.hasFile) {
            painter->setPen(QPen(m_style.fileBorderColor));
            painter->setBrush(m_style.fileCardColor);
            painter->drawRoundedRect(attachRect, 6, 6);
            painter->setPen(m_style.replyTextColor);
            painter->setFont(m_style.replyFont);
            QFontMetrics fileMetrics(m_style.replyFont);
            const int textWidth = attachRect.width() - kReplyPadding * 2;
            const QString nameText = fileMetrics.elidedText(d.fileName, Qt::ElideRight, textWidth);
            const QString sizeText = d.fileSize > 0
                ? QStringLiteral("%1 KB").arg(d.fileSize / 1024)
                : tr("文件");
            painter->drawText(QRect(attachRect.left() + kReplyPadding, attachRect.top() + kReplyPadding,
                                    textWidth, fileMetrics.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, nameText);
            painter->drawText(QRect(attachRect.left() + kReplyPadding,
                                    attachRect.bottom() - kReplyPadding - fileMetrics.height(),
                                    textWidth, fileMetrics.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, sizeText);
        }
        cursorY += attachmentHeight + kLineSpacing;
    }

    if (docSize.height() > 0) {
        painter->save();
        painter->translate(innerRect.left(), cursorY);
        QRectF clip(0, 0, innerRect.width(), docSize.height());
        doc.drawContents(painter, clip);
        painter->restore();
        cursorY += docSize.height();
    }

    if (!d.reactions.isEmpty()) {
        cursorY += kLineSpacing;
        painter->setFont(m_style.reactionFont);
        QFontMetrics reactionMetrics(m_style.reactionFont);
        int x = innerRect.left();
        const int chipHeight = reactionMetrics.height() + kReactionPaddingV * 2;
        for (const ChatWidgetReaction& reaction : d.reactions) {
            const QString label = reaction.count > 0
                ? QString("%1 %2").arg(reaction.emoji).arg(reaction.count)
                : reaction.emoji;
            const int chipWidth = reactionMetrics.horizontalAdvance(label) + kReactionPaddingH * 2;
            QRect chipRect(x, cursorY, chipWidth, chipHeight);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_style.reactionChipColor);
            painter->drawRoundedRect(chipRect, chipHeight / 2, chipHeight / 2);
            painter->setPen(m_style.reactionTextColor);
            painter->drawText(chipRect, Qt::AlignCenter, label);
            x += chipWidth + 6;
            if (x + chipWidth > innerRect.right()) {
                break;
            }
        }
    }

    const QString timestampText = formatTimestamp(d.timestamp);
    if (!timestampText.isEmpty()) {
        const int footerY = bubbleRect.bottom() + kLineSpacing + 1;
        if (d.isMine) {
            painter->setFont(m_style.timestampFont);
            painter->setPen(m_style.timestampColor);
            QFontMetrics tsMetrics(m_style.timestampFont);
            const int tsWidth = textPixelWidth(tsMetrics, timestampText) + kFooterTextHPadding;
            const int tsHeight = tsMetrics.height() + kFooterTextVPadding;
            QRect tsRect(bubbleRect.right() + 1 - tsWidth, footerY, tsWidth, tsHeight);
            painter->drawText(tsRect, Qt::AlignRight | Qt::AlignVCenter, timestampText);
        } else {
            painter->setFont(m_style.timestampFont);
            painter->setPen(m_style.timestampColor);
            QFontMetrics tsMetrics(m_style.timestampFont);
            const int tsWidth = textPixelWidth(tsMetrics, timestampText) + kFooterTextHPadding;
            const int tsHeight = tsMetrics.height() + kFooterTextVPadding;
            QRect tsRect(bubbleRect.left(), footerY, tsWidth, tsHeight);
            painter->drawText(tsRect, Qt::AlignLeft | Qt::AlignVCenter, timestampText);
        }
    }

    painter->restore();
}

QRect ChatWidgetDelegate::avatarRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const bool isMine = index.data(ChatWidgetModel::ChatWidgetIsMineRole).toBool();
    const QRect rect = option.rect;
    if (isMine) {
        return QRect(rect.right() - m_style.margin - m_style.avatarSize, rect.top() + m_style.margin, m_style.avatarSize, m_style.avatarSize);
    }
    return QRect(rect.left() + m_style.margin, rect.top() + m_style.margin, m_style.avatarSize, m_style.avatarSize);
}

QRect ChatWidgetDelegate::fileCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const IndexData d = IndexData::fromIndex(index);
    if (isSystemType(d.type))
        return QRect();
    if (!d.hasImage && !d.hasFile && !d.hasVoice)
        return QRect();

    QString html = ChatWidgetMarkdownUtils::renderMarkdown(d.content);
    const int maxWidth = calcMaxBubbleWidth(option);

    QTextDocument doc;
    doc.setDefaultFont(m_style.messageFont);
    doc.setHtml(html);
    doc.setTextWidth(maxWidth);
    const int docWidth = qMin(maxWidth, qCeil(doc.idealWidth()));

    const int attW = calcAttachmentWidth(d.hasImage, d.hasFile, d.hasVoice, d.imageWidth, d.imageHeight);
    const int attachmentWidth = (attW > 0) ? attW : qMax(160, docWidth);
    const int attachmentHeight = calcAttachmentHeight(d.hasImage, d.hasFile, d.hasVoice,
                                                       d.imageWidth, d.imageHeight);

    const int contentWidth = qMax(docWidth, attachmentWidth);
    const int bubbleWidth = contentWidth + m_style.bubblePadding * 2;

    const QRect rect = option.rect;
    const QRect avRect = avatarRect(option, index);
    int contentTop = rect.top() + m_style.margin;
    if (!d.isMine && !d.senderId.isEmpty() && !d.senderName.isEmpty()) {
        QFontMetrics nameMetrics(m_style.nameFont);
        contentTop += nameMetrics.height() + m_style.nameSpacing;
    }

    const int bubbleLeft = d.isMine
        ? (avRect.left() - m_style.margin - bubbleWidth)
        : (avRect.right() + m_style.margin);

    const int innerLeft = bubbleLeft + m_style.bubblePadding;
    int cursorY = contentTop + m_style.bubblePadding;

    const int replyHeight = calcReplyHeight(m_style.replyFont, d.hasReply, d.hasForward);
    if (replyHeight > 0)
        cursorY += replyHeight + kLineSpacing;

    return QRect(innerLeft, cursorY, qMin(contentWidth, attachmentWidth), attachmentHeight);
}
