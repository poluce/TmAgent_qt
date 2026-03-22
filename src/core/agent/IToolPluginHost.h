#ifndef ITOOLPLUGINHOST_H
#define ITOOLPLUGINHOST_H

#include "ToolTypes.h"
#include <QJsonObject>
#include <QString>

class IToolPluginHost {
public:
    virtual ~IToolPluginHost() = default;

    virtual Tool resolveToolSchema(const QString& toolName,
                                   const QString& fallbackDescription) const = 0;
    virtual ToolResult executeHostedTool(const QString& toolName,
                                         const QString& fallbackDescription,
                                         const QJsonObject& args) = 0;
};

#endif // ITOOLPLUGINHOST_H
