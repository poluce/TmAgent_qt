#ifndef COMMANDPOLICYDIALOG_H
#define COMMANDPOLICYDIALOG_H

#include "AppFacade.h"

class QWidget;

namespace CommandPolicyDialog {

void show(QWidget* parent, IAppFacade& app);

} // namespace CommandPolicyDialog

#endif // COMMANDPOLICYDIALOG_H

