#ifndef ITOOLPLUGINHOST_H
#define ITOOLPLUGINHOST_H

#include "ToolTypes.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>

class IToolPluginHost {
public:
    virtual ~IToolPluginHost() = default;

    virtual QStringList availableTeammateBackendIds() const = 0;
};

#endif // ITOOLPLUGINHOST_H
