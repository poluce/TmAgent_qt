#ifndef COMMANDPOLICYDIALOG_H
#define COMMANDPOLICYDIALOG_H

class QWidget;
class ChatService;

namespace CommandPolicyDialog {

void show(QWidget* parent, ChatService* chatService);

} // namespace CommandPolicyDialog

#endif // COMMANDPOLICYDIALOG_H
