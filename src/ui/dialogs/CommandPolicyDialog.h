#ifndef COMMANDPOLICYDIALOG_H
#define COMMANDPOLICYDIALOG_H

#include "ChatCapabilityInterfaces.h"

class QWidget;

namespace CommandPolicyDialog {

void show(QWidget* parent,
          IGovernanceCommands* governanceCommands,
          const IGovernanceQueries* governanceQueries);

} // namespace CommandPolicyDialog

#endif // COMMANDPOLICYDIALOG_H

