#ifndef AVATARUTILS_H
#define AVATARUTILS_H

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace AvatarUtils {

QColor colorFromId(const QString& identityId);

QIcon makeAvatarIcon(const QString& identityId, const QString& displayName, const QString& avatarPath = QString(), int side = 54, int cornerRadius = 12);

QIcon makeGlyphIcon(const QString& glyph, const QColor& bgColor, int side = 54, int cornerRadius = 12);

QPixmap makeFallbackAvatar(const QString& text, int side, const QColor& bgColor = QColor(99, 102, 241), int cornerRadius = 12);

} // namespace AvatarUtils

#endif // AVATARUTILS_H
