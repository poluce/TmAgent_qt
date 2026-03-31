#ifndef AGENTLIFECYCLESUPPORT_H
#define AGENTLIFECYCLESUPPORT_H

#include "AppFacade.h"
#include <QString>

class QWidget;

namespace AgentLifecycleSupport {

QString createAgentWithDialog(QWidget* parent, IAppFacade& app);
bool deleteAgentWithConfirmation(QWidget* parent, IAppFacade& app, const QString& agentIdentityId);

} // namespace AgentLifecycleSupport

#endif // AGENTLIFECYCLESUPPORT_H

