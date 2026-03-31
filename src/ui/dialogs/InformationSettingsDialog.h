#ifndef INFORMATIONSETTINGSDIALOG_H
#define INFORMATIONSETTINGSDIALOG_H

#include "AppFacade.h"
#include <QString>

class QWidget;

namespace InformationSettingsDialog {

void show(QWidget* parent, IAppFacade& app, const QString& activeIdentityId);

} // namespace InformationSettingsDialog

#endif // INFORMATIONSETTINGSDIALOG_H

