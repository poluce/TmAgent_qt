#ifndef MCPCONFIGDIALOG_H
#define MCPCONFIGDIALOG_H

class QWidget;
class ChatService;

namespace McpConfigDialog {

void show(QWidget* parent, ChatService* chatService);

} // namespace McpConfigDialog

#endif // MCPCONFIGDIALOG_H
