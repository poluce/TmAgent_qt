#ifndef INFORMATIONSETTINGSDIALOG_H
#define INFORMATIONSETTINGSDIALOG_H

#include <QString>

class QWidget;
class ChatService;

namespace InformationSettingsDialog {

void show(QWidget* parent, ChatService* chatService, const QString& activeIdentityId);

} // namespace InformationSettingsDialog

#endif // INFORMATIONSETTINGSDIALOG_H
