#ifndef AGENTLIFECYCLESUPPORT_H
#define AGENTLIFECYCLESUPPORT_H

#include "ChatCapabilityInterfaces.h"
#include <QString>

class QWidget;

namespace AgentLifecycleSupport {

QString createAgentWithDialog(QWidget* parent, const AgentLifecycleCapabilities& capabilities);
bool deleteAgentWithConfirmation(QWidget* parent,
                                 ISessionCommands* sessionCommands,
                                 IMemoryCommands* memoryCommands,
                                 IWorkspacePersistence* workspacePersistence,
                                 const QString& agentIdentityId);

} // namespace AgentLifecycleSupport

#endif // AGENTLIFECYCLESUPPORT_H

