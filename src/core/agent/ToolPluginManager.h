#ifndef TOOLPLUGINMANAGER_H
#define TOOLPLUGINMANAGER_H

#include "IToolPlugin.h"
#include <tmagent/plugin/IToolPlugin.h>
#include <QHash>
#include <QObject>

class QPluginLoader;
class LegacyPluginAdapter;

class ToolPluginManager : public QObject {
    Q_OBJECT
public:
    struct ProviderBinding {
        QString providerName;
        IToolProvider* provider = nullptr;
    };
    
    struct FailedPluginInfo {
        QString path;
        QString pluginId;
        QString error;
        QString timestamp;
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
    QList<FailedPluginInfo> failedPlugins() const;
    bool retryLoadPlugin(const QString& pluginId);

private:
    struct LoadedPlugin {
        QString path;
        ToolPluginDescriptor descriptor;
        QPluginLoader* loader = nullptr;
        QObject* instance = nullptr;
        IToolPlugin* plugin = nullptr;  // 旧接口插件（已弃用）
        TmAgent::IToolPlugin* sdkPlugin = nullptr;  // SDK 接口插件（推荐）
        LegacyPluginAdapter* adapter = nullptr;  // 旧插件适配器
        IToolProvider* provider = nullptr;
        ToolPluginHealth health;
        QJsonObject config;
        bool enabled = true;
        bool loaded = false;
        bool isLegacy = false;  // 标记是否使用旧接口
        QString lastError;
    };

    QStringList candidatePluginDirs() const;
    QString runtimePluginDirPath() const;
    void discoverPlugins();
    bool tryLoadPlugin(const QString& filePath);
    void applyConfigToLoadedPlugins();
    bool isCompatible(const TmAgent::ToolPluginDescriptor& descriptor) const;
    void recordFailedPlugin(const QString& path, const QString& pluginId, const QString& error);
    static QString canonicalDirPath(const QString& path);

    IToolPluginHost* m_host = nullptr;
    bool m_initialized = false;
    QJsonObject m_configObject;
    QHash<QString, LoadedPlugin> m_plugins;
    QList<FailedPluginInfo> m_failedPlugins;
};

#endif // TOOLPLUGINMANAGER_H
