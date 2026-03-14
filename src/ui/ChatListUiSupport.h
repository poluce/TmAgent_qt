#ifndef CHATLISTUISUPPORT_H
#define CHATLISTUISUPPORT_H

#include <QString>

class ChatListWidget;
class QModelIndex;

namespace ChatListUiSupport {

int sourceRowForIndex(ChatListWidget* widget, const QModelIndex& index);
int currentSourceRow(ChatListWidget* widget);
void selectSourceRow(ChatListWidget* widget, int row);
void clearCurrentSelection(ChatListWidget* widget);
QString shortenPreview(const QString& preview, int maxChars = 80);
bool updateChatItemPreview(ChatListWidget* widget,
                           int row,
                           const QString& name,
                           const QString& preview,
                           const QString& timeText,
                           const QString& avatarPath = QString());

} // namespace ChatListUiSupport

#endif // CHATLISTUISUPPORT_H
