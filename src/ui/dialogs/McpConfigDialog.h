#ifndef MCPCONFIGDIALOG_H
#define MCPCONFIGDIALOG_H

#include "ChatCapabilityInterfaces.h"

class QWidget;

namespace McpConfigDialog {

void show(QWidget* parent, const McpConfigDialogCapabilities& capabilities);

} // namespace McpConfigDialog

#endif // MCPCONFIGDIALOG_H

