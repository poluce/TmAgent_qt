#ifndef TOOLPLUGINTYPES_H
#define TOOLPLUGINTYPES_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct ToolPluginDescriptor {
    QString pluginId;
    QString displayName;
    QString version;
    QString description;
    QString category;
    QStringList toolNames;
    QJsonObject configSchema;

    bool isValid() const
    {
        return !pluginId.trimmed().isEmpty();
    }
};

struct ToolPluginHealth {
    QString state = QStringLiteral("unknown");
    QString message;
    QDateTime checkedAtUtc;
    int toolCount = 0;
};

struct ToolPluginInfo {
    ToolPluginDescriptor descriptor;
    ToolPluginHealth health;
    bool enabled = false;
    bool loaded = false;
    bool externalProvider = false;
    QString sourcePath;
    QJsonObject config;
};

#endif // TOOLPLUGINTYPES_H
