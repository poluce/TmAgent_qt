#ifndef ITOOLPLUGIN_H
#define ITOOLPLUGIN_H

#include "IToolPluginHost.h"
#include "IToolProvider.h"
#include "ToolPluginTypes.h"
#include <QtPlugin>

class IToolPlugin {
public:
    virtual ~IToolPlugin() = default;

    virtual ToolPluginDescriptor descriptor() const = 0;
    virtual IToolProvider* createProvider(IToolPluginHost* host, QObject* parent) = 0;

    virtual bool configureProvider(IToolProvider*,
                                   const QJsonObject&,
                                   QString* error)
    {
        if (error)
            error->clear();
        return true;
    }

    virtual ToolPluginHealth health(const IToolProvider* provider) const
    {
        ToolPluginHealth healthInfo;
        healthInfo.state = QStringLiteral("ok");
        healthInfo.checkedAtUtc = QDateTime::currentDateTimeUtc();
        if (provider)
            healthInfo.toolCount = provider->listTools().size();
        return healthInfo;
    }
};

#define TMAGENT_TOOL_PLUGIN_IID "org.tmagent.ToolPlugin/1.0"
Q_DECLARE_INTERFACE(IToolPlugin, TMAGENT_TOOL_PLUGIN_IID)

#endif // ITOOLPLUGIN_H
