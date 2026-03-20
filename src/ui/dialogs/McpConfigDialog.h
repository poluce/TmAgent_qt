#ifndef MCPCONFIGDIALOG_H
#define MCPCONFIGDIALOG_H

#include "AppFacade.h"

class QWidget;

namespace McpConfigDialog {

void show(QWidget* parent, IAppFacade& app);

} // namespace McpConfigDialog

#endif // MCPCONFIGDIALOG_H

