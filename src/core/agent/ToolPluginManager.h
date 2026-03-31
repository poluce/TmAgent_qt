#ifndef TOOLPLUGINMANAGER_H
#define TOOLPLUGINMANAGER_H

#include "IToolPlugin.h"
#include <QHash>
#include <QObject>

class QPluginLoader;

class ToolPluginManager : public QObject {
    Q_OBJECT
public:
    struct ProviderBinding {
        QString providerName;
        IToolProvider* provider = nullptr;
    };

    explicit ToolPluginManager(IToolPluginHost* host, QObject* parent = nullptr);

    void initialize();
    void reload();

    QJsonObject defaultConfigObject() const;
    QJsonObject normalizeConfigObject(const QJsonObject& raw) const;
    void setConfigObject(const QJsonObject& raw);
    QJsonObject configObject() const;
    bool setPluginEnabled(const QString& pluginId, bool enabled);
    bool setPluginConfig(const QString& pluginId, const QJsonObject& config);

    QList<ToolPluginInfo> pluginInfos() const;
    QList<ProviderBinding> activeProviders() const;

private:
    struct LoadedPlugin {
        QString path;
        ToolPluginDescriptor descriptor;
        QPluginLoader* loader = nullptr;
        QObject* instance = nullptr;
        IToolPlugin* plugin = nullptr;
        IToolProvider* provider = nullptr;
        ToolPluginHealth health;
        QJsonObject config;
        bool enabled = true;
        bool loaded = false;
        QString lastError;
    };

    QStringList candidatePluginDirs() const;
    QString runtimePluginDirPath() const;
    void discoverPlugins();
    bool tryLoadPlugin(const QString& filePath);
    void applyConfigToLoadedPlugins();
    static QString canonicalDirPath(const QString& path);

    IToolPluginHost* m_host = nullptr;
    bool m_initialized = false;
    QJsonObject m_configObject;
    QHash<QString, LoadedPlugin> m_plugins;
};

#endif // TOOLPLUGINMANAGER_H
