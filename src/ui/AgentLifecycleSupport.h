#ifndef AGENTLIFECYCLESUPPORT_H
#define AGENTLIFECYCLESUPPORT_H

#include <QString>

class QWidget;
class ChatService;

namespace AgentLifecycleSupport {

QString createAgentWithDialog(QWidget* parent, ChatService* chatService);
bool deleteAgentWithConfirmation(QWidget* parent, ChatService* chatService, const QString& agentIdentityId);

} // namespace AgentLifecycleSupport

#endif // AGENTLIFECYCLESUPPORT_H
