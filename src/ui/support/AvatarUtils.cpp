#include "AvatarUtils.h"
#include <QFont>
#include <QPainter>
#include <QPainterPath>

namespace AvatarUtils {

QColor colorFromId(const QString& identityId)
{
    const uint h = qHash(identityId);
    return QColor::fromHsv(static_cast<int>(h % 360), 120, 212);
}

QPixmap makeFallbackAvatar(const QString& text, int side, const QColor& bgColor, int cornerRadius)
{
    const int avatarSide = qMax(16, side);
    const int radius = qMax(0, cornerRadius);

    QPixmap pixmap(avatarSide, avatarSide);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(pixmap.rect(), radius, radius);

    QString avatarText = text.trimmed();
    if (avatarText.isEmpty())
        avatarText = QStringLiteral("A");
    avatarText = avatarText.left(1).toUpper();

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(14, avatarSide / 2));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, avatarText);
    return pixmap;
}

QIcon makeAvatarIcon(const QString& identityId, const QString& displayName, const QString& avatarPath, int side, int cornerRadius)
{
    const int avatarSide = qMax(16, side);
    const int radius = qMax(0, cornerRadius);
    const QString normalizedPath = avatarPath.trimmed();

    if (!normalizedPath.isEmpty()) {
        QPixmap source(normalizedPath);
        if (!source.isNull()) {
            QPixmap avatar(avatarSide, avatarSide);
            avatar.fill(Qt::transparent);
            QPainter painter(&avatar);
            painter.setRenderHint(QPainter::Antialiasing, true);
            QPainterPath path;
            path.addRoundedRect(QRectF(0, 0, avatarSide, avatarSide), radius, radius);
            painter.setClipPath(path);
            painter.drawPixmap(0, 0, source.scaled(avatarSide, avatarSide, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            return QIcon(avatar);
        }
    }

    return QIcon(makeFallbackAvatar(displayName, avatarSide, colorFromId(identityId), radius));
}

QIcon makeGlyphIcon(const QString& glyph, const QColor& bgColor, int side, int cornerRadius)
{
    const int iconSide = qMax(16, side);
    const int radius = qMax(0, cornerRadius);

    QPixmap pixmap(iconSide, iconSide);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(pixmap.rect(), radius, radius);

    QString iconText = glyph.trimmed();
    if (iconText.isEmpty())
        iconText = QStringLiteral("T");
    iconText = iconText.left(1);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(16, iconSide / 2));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, iconText);
    return QIcon(pixmap);
}

} // namespace AvatarUtils
