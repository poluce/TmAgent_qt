#ifndef MODELCONFIGDIALOG_H
#define MODELCONFIGDIALOG_H

#include "ChatCapabilityInterfaces.h"

class QWidget;

namespace ModelConfigDialog {

void show(QWidget* parent, const ModelConfigDialogCapabilities& capabilities);

} // namespace ModelConfigDialog

#endif // MODELCONFIGDIALOG_H

