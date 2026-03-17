#ifndef INFORMATIONSETTINGSDIALOG_H
#define INFORMATIONSETTINGSDIALOG_H

#include "ChatCapabilityInterfaces.h"
#include <QString>

class QWidget;

namespace InformationSettingsDialog {

void show(QWidget* parent,
          const InformationSettingsCapabilities& capabilities,
          const QString& activeIdentityId);

} // namespace InformationSettingsDialog

#endif // INFORMATIONSETTINGSDIALOG_H

