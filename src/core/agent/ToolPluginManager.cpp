#include "ToolPluginManager.h"
#include "LegacyPluginAdapter.h"
#include "tmagent/version.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>
#include <QProcessEnvironment>
#include <QDebug>
#include <QSet>
#include <algorithm>

namespace {

QString metaString(const QJsonObject& meta, const QString& key)
{
    return meta.value(key).toString().trimmed();
}

#ifndef QT_NO_DEBUG
QString findRepoRoot(const QString& startDir)
{
    QDir dir(startDir);
    while (dir.exists()) {
        if (dir.exists(QStringLiteral("TmAgent.pro")))
            return dir.canonicalPath();
        if (!dir.cdUp())
            break;
    }
    return QString();
}

QStringList pluginSearchDirsFromRepoRoot(const QString& repoRoot)
{
    if (repoRoot.trimmed().isEmpty())
        return {};

    QStringList out;
#ifdef QT_NO_DEBUG
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/release/plugins/tools"));
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/debug/plugins/tools"));
#else
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/debug/plugins/tools"));
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/release/plugins/tools"));
#endif
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/plugins/tools"));
    return out;
}
#endif

bool isLoadablePluginFile(const QString& filePath)
{
    if (!QLibrary::isLibrary(filePath))
        return false;

    const QString fileName = QFileInfo(filePath).fileName().toLower();
#ifdef Q_OS_WIN
    return fileName.endsWith(QStringLiteral(".dll"));
#elif defined(Q_OS_MACOS)
    return fileName.endsWith(QStringLiteral(".dylib")) || fileName.endsWith(QStringLiteral(".so"));
#else
    return fileName.endsWith(QStringLiteral(".so"));
#endif
}

ToolPluginDescriptor descriptorFromMeta(const QJsonObject& meta)
{
    ToolPluginDescriptor descriptor;
    descriptor.pluginId = metaString(meta, QStringLiteral("plugin_id"));
    descriptor.displayName = metaString(meta, QStringLiteral("display_name"));
    descriptor.version = metaString(meta, QStringLiteral("version"));
    descriptor.category = metaString(meta, QStringLiteral("category"));
    return descriptor;
}

QJsonObject pluginEntriesObject(const QJsonObject& root)
{
    const QJsonValue pluginsValue = root.value(QStringLiteral("plugins"));
    return pluginsValue.isObject() ? pluginsValue.toObject() : QJsonObject();
}

QJsonObject normalizedPluginEntry(const QJsonValue& value)
{
    QJsonObject out;
    const QJsonObject obj = value.toObject();
    out.insert(QStringLiteral("enabled"), obj.value(QStringLiteral("enabled")).toBool(true));
    out.insert(QStringLiteral("config"),
               obj.value(QStringLiteral("config")).isObject()
                   ? obj.value(QStringLiteral("config")).toObject()
                   : QJsonObject());
    out.insert(QStringLiteral("last_health"),
               obj.value(QStringLiteral("last_health")).isObject()
                   ? obj.value(QStringLiteral("last_health")).toObject()
                   : QJsonObject());
    return out;
}

} // namespace

ToolPluginManager::ToolPluginManager(IToolPluginHost* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
{
}

void ToolPluginManager::initialize()
{
    if (m_initialized)
        return;

    m_configObject = normalizeConfigObject(m_configObject);
    m_failedPlugins.clear();  // 清空失败列表
    discoverPlugins();
    applyConfigToLoadedPlugins();
    m_initialized = true;
}

void ToolPluginManager::reload()
{
    if (!m_initialized) {
        initialize();
        return;
    }

    m_configObject = normalizeConfigObject(m_configObject);
    m_failedPlugins.clear();  // 清空失败列表
    discoverPlugins();
    applyConfigToLoadedPlugins();
}

QJsonObject ToolPluginManager::defaultConfigObject() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("search_dirs"), QJsonArray());
    root.insert(QStringLiteral("plugins"), QJsonObject());
    return root;
}

QJsonObject ToolPluginManager::normalizeConfigObject(const QJsonObject& raw) const
{
    QJsonObject out = defaultConfigObject();
    out.insert(QStringLiteral("schema_version"), 1);

    QJsonArray searchDirs;
    const QJsonArray rawDirs = raw.value(QStringLiteral("search_dirs")).toArray();
    QSet<QString> seenDirs;
    for (const QJsonValue& value : rawDirs) {
        const QString dir = value.toString().trimmed();
        if (dir.isEmpty() || seenDirs.contains(dir))
            continue;
        seenDirs.insert(dir);
        searchDirs.append(dir);
    }
    out.insert(QStringLiteral("search_dirs"), searchDirs);

    QJsonObject pluginsOut;
    const QJsonObject pluginsIn = pluginEntriesObject(raw);
    for (auto it = pluginsIn.constBegin(); it != pluginsIn.constEnd(); ++it)
        pluginsOut.insert(it.key(), normalizedPluginEntry(it.value()));
    out.insert(QStringLiteral("plugins"), pluginsOut);
    return out;
}

void ToolPluginManager::setConfigObject(const QJsonObject& raw)
{
    m_configObject = normalizeConfigObject(raw);
}

QJsonObject ToolPluginManager::configObject() const
{
    return m_configObject;
}

bool ToolPluginManager::setPluginEnabled(const QString& pluginId, bool enabled)
{
    const QString key = pluginId.trimmed();
    if (key.isEmpty())
        return false;

    QJsonObject root = m_configObject;
    QJsonObject plugins = pluginEntriesObject(root);
    QJsonObject entry = normalizedPluginEntry(plugins.value(key));
    entry.insert(QStringLiteral("enabled"), enabled);
    plugins.insert(key, entry);
    root.insert(QStringLiteral("plugins"), plugins);
    m_configObject = normalizeConfigObject(root);
    applyConfigToLoadedPlugins();
    return true;
}

bool ToolPluginManager::setPluginConfig(const QString& pluginId, const QJsonObject& config)
{
    const QString key = pluginId.trimmed();
    if (key.isEmpty())
        return false;

    QJsonObject root = m_configObject;
    QJsonObject plugins = pluginEntriesObject(root);
    QJsonObject entry = normalizedPluginEntry(plugins.value(key));
    entry.insert(QStringLiteral("config"), config);
    plugins.insert(key, entry);
    root.insert(QStringLiteral("plugins"), plugins);
    m_configObject = normalizeConfigObject(root);
    applyConfigToLoadedPlugins();
    return true;
}

QList<ToolPluginInfo> ToolPluginManager::pluginInfos() const
{
    QList<ToolPluginInfo> infos;
    infos.reserve(m_plugins.size());
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        const LoadedPlugin& loaded = it.value();
        ToolPluginInfo info;
        info.descriptor = loaded.descriptor;
        info.health = loaded.health;
        info.enabled = loaded.enabled;
        info.loaded = loaded.loaded;
        info.sourcePath = loaded.path;
        info.config = loaded.config;
        infos.append(info);
    }

    std::sort(infos.begin(), infos.end(), [](const ToolPluginInfo& a, const ToolPluginInfo& b) {
        return a.descriptor.displayName.compare(b.descriptor.displayName, Qt::CaseInsensitive) < 0;
    });
    return infos;
}

QList<ToolPluginManager::ProviderBinding> ToolPluginManager::activeProviders() const
{
    QList<ProviderBinding> bindings;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        const LoadedPlugin& loaded = it.value();
        if (!loaded.enabled || !loaded.loaded || !loaded.provider)
            continue;
        ProviderBinding binding;
        binding.providerName = loaded.descriptor.pluginId;
        binding.provider = loaded.provider;
        bindings.append(binding);
    }
    return bindings;
}

QStringList ToolPluginManager::candidatePluginDirs() const
{
    QStringList out;
    out << runtimePluginDirPath();

    const QJsonArray configuredDirs = m_configObject.value(QStringLiteral("search_dirs")).toArray();
    for (const QJsonValue& value : configuredDirs)
        out << value.toString().trimmed();

    const QString envRaw = QProcessEnvironment::systemEnvironment()
                               .value(QStringLiteral("TMAGENT_TOOL_PLUGIN_DIRS"))
                               .trimmed();
    if (!envRaw.isEmpty())
        out.append(envRaw.split(QDir::listSeparator(), Qt::SkipEmptyParts));

#ifndef QT_NO_DEBUG
    QSet<QString> repoRoots;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString repoFromApp = findRepoRoot(appDir);
    if (!repoFromApp.isEmpty())
        repoRoots.insert(repoFromApp);
    const QString repoFromCwd = findRepoRoot(QDir::currentPath());
    if (!repoFromCwd.isEmpty())
        repoRoots.insert(repoFromCwd);
    for (const QString& repoRoot : repoRoots)
        out.append(pluginSearchDirsFromRepoRoot(repoRoot));
#endif

    QStringList normalized;
    QSet<QString> seen;
    for (const QString& candidate : out) {
        const QString canonical = canonicalDirPath(candidate);
        if (canonical.isEmpty() || seen.contains(canonical))
            continue;
        seen.insert(canonical);
        normalized.append(canonical);
    }
    return normalized;
}

QString ToolPluginManager::runtimePluginDirPath() const
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("resources/plugins/tools"));
}

void ToolPluginManager::discoverPlugins()
{
    const QString runtimeDirPath = runtimePluginDirPath();
    const QString runtimeCanonical = canonicalDirPath(runtimeDirPath);
    const QStringList searchDirs = candidatePluginDirs();
    if (searchDirs.isEmpty()) {
        qWarning().noquote() << QStringLiteral(
            "[ToolPluginManager] no plugin directories found; expected runtime dir: %1")
                                     .arg(QDir::toNativeSeparators(runtimeDirPath));
        return;
    }

    qInfo() << "[ToolPluginManager] plugin search dirs:" << searchDirs;
    int runtimeLoadedCount = 0;
    for (const QString& dirPath : searchDirs) {
        qInfo() << "[ToolPluginManager] scanning plugin dir:" << dirPath;
        QDir dir(dirPath);
        const QStringList files = dir.entryList(QDir::Files);
        for (const QString& fileName : files) {
            const QString filePath = dir.filePath(fileName);
            if (!isLoadablePluginFile(filePath))
                continue;
            if (tryLoadPlugin(filePath) && !runtimeCanonical.isEmpty() && dirPath == runtimeCanonical)
                ++runtimeLoadedCount;
        }
    }

    if (runtimeLoadedCount == 0) {
        qWarning().noquote() << QStringLiteral(
            "[ToolPluginManager] no first-party tool plugins loaded from runtime dir: %1")
                                     .arg(QDir::toNativeSeparators(runtimeDirPath));
    }
}

bool ToolPluginManager::tryLoadPlugin(const QString& filePath)
{
    QPluginLoader* loader = new QPluginLoader(filePath, this);
    const QJsonObject rootMeta = loader->metaData();
    const QJsonObject meta = rootMeta.value(QStringLiteral("MetaData")).toObject();
    const ToolPluginDescriptor metaDescriptor = descriptorFromMeta(meta);
    if (!metaDescriptor.isValid()) {
        recordFailedPlugin(filePath, QString(), QStringLiteral("Invalid plugin metadata"));
        delete loader;
        return false;
    }
    if (m_plugins.contains(metaDescriptor.pluginId)) {
        recordFailedPlugin(filePath, metaDescriptor.pluginId, 
                          QStringLiteral("Duplicate plugin ID (already loaded from: %1)")
                              .arg(m_plugins.value(metaDescriptor.pluginId).path));
        delete loader;
        return false;
    }

    QObject* instance = loader->instance();
    if (!instance) {
        const QString error = loader->errorString();
        recordFailedPlugin(filePath, metaDescriptor.pluginId, 
                          QStringLiteral("Failed to load: %1").arg(error));
        delete loader;
        return false;
    }

    LoadedPlugin loaded;
    loaded.path = filePath;
    loaded.loader = loader;
    loaded.instance = instance;
    
    // 首先尝试 SDK 接口（TmAgent::IToolPlugin）
    auto* sdkPlugin = qobject_cast<TmAgent::IToolPlugin*>(instance);
    if (sdkPlugin) {
        qInfo() << "[ToolPluginManager] detected SDK interface plugin:" << metaDescriptor.pluginId;
        loaded.sdkPlugin = sdkPlugin;
        loaded.isLegacy = false;
        loaded.descriptor = sdkPlugin->descriptor();
        
        // 版本兼容性检查
        if (!isCompatible(loaded.descriptor)) {
            const QString error = QStringLiteral("SDK version incompatible: plugin requires %1.%2, host has %3.%4")
                                      .arg(loaded.descriptor.sdkVersionMajor)
                                      .arg(loaded.descriptor.sdkVersionMinor)
                                      .arg(TMAGENT_SDK_VERSION_MAJOR)
                                      .arg(TMAGENT_SDK_VERSION_MINOR);
            recordFailedPlugin(filePath, loaded.descriptor.pluginId, error);
            loader->unload();
            delete loader;
            return false;
        }
    } else {
        // 回退到旧接口（IToolPlugin）
        auto* legacyPlugin = qobject_cast<::IToolPlugin*>(instance);
        if (!legacyPlugin) {
            recordFailedPlugin(filePath, metaDescriptor.pluginId, 
                              QStringLiteral("Does not implement IToolPlugin or TmAgent::IToolPlugin interface"));
            loader->unload();
            delete loader;
            return false;
        }
        
        qWarning() << "[ToolPluginManager] detected legacy interface plugin:" << metaDescriptor.pluginId
                   << "- This plugin should be migrated to the SDK interface";
        
        // 创建适配器包装旧插件
        loaded.adapter = new LegacyPluginAdapter(legacyPlugin, this);
        loaded.plugin = legacyPlugin;
        loaded.sdkPlugin = loaded.adapter;  // 使用适配器作为 SDK 接口
        loaded.isLegacy = true;
        loaded.descriptor = loaded.adapter->descriptor();
        
        // 旧插件标记为 sdkVersionMajor=0，始终兼容
        qInfo() << "[ToolPluginManager] wrapped legacy plugin with adapter:" << metaDescriptor.pluginId;
    }
    
    loaded.loaded = loaded.descriptor.isValid() && m_host;
    loaded.enabled = true;

    if (loaded.loaded) {
        loaded.provider = loaded.sdkPlugin->createProvider(m_host, this);
        if (!loaded.provider) {
            loaded.loaded = false;
            loaded.lastError = QStringLiteral("provider creation failed");
            recordFailedPlugin(filePath, loaded.descriptor.pluginId, loaded.lastError);
        }
    }

    m_plugins.insert(loaded.descriptor.pluginId.trimmed().isEmpty()
                         ? metaDescriptor.pluginId
                         : loaded.descriptor.pluginId,
                     loaded);
    qInfo() << "[ToolPluginManager] loaded tool plugin:"
            << (loaded.descriptor.pluginId.trimmed().isEmpty()
                    ? metaDescriptor.pluginId
                    : loaded.descriptor.pluginId)
            << "from" << filePath
            << (loaded.isLegacy ? "(legacy interface)" : "(SDK interface)");
    return true;
}

void ToolPluginManager::applyConfigToLoadedPlugins()
{
    QJsonObject plugins = pluginEntriesObject(m_configObject);
    for (auto it = m_plugins.begin(); it != m_plugins.end(); ++it) {
        LoadedPlugin& loaded = it.value();
        QJsonObject entry = normalizedPluginEntry(plugins.value(it.key()));
        loaded.enabled = entry.value(QStringLiteral("enabled")).toBool(true);
        loaded.config = entry.value(QStringLiteral("config")).toObject();

        ToolPluginHealth healthInfo;
        if (!loaded.loaded || !loaded.sdkPlugin || !loaded.provider) {
            healthInfo.state = QStringLiteral("error");
            healthInfo.message = loaded.lastError.isEmpty()
                ? QStringLiteral("plugin load failed")
                : loaded.lastError;
            healthInfo.checkedAtUtc = QDateTime::currentDateTimeUtc();
            loaded.health = healthInfo;
            continue;
        }

        QString configureError;
        const bool configured =
            loaded.sdkPlugin->configureProvider(loaded.provider, loaded.config, &configureError);

        if (!loaded.enabled) {
            healthInfo.state = QStringLiteral("disabled");
            healthInfo.message = QStringLiteral("disabled by config");
            healthInfo.toolCount = loaded.provider->listTools().size();
        } else if (!configured) {
            healthInfo.state = QStringLiteral("error");
            healthInfo.message = configureError.isEmpty()
                ? QStringLiteral("provider configuration failed")
                : configureError;
            healthInfo.toolCount = loaded.provider->listTools().size();
        } else {
            healthInfo = loaded.sdkPlugin->health(loaded.provider);
            if (healthInfo.state.trimmed().isEmpty())
                healthInfo.state = QStringLiteral("ok");
        }

        if (!loaded.enabled)
            loaded.lastError.clear();
        else if (healthInfo.state == QLatin1String("error"))
            loaded.lastError = healthInfo.message;
        else
            loaded.lastError.clear();

        if (!healthInfo.checkedAtUtc.isValid())
            healthInfo.checkedAtUtc = QDateTime::currentDateTimeUtc();
        loaded.health = healthInfo;

        QJsonObject lastHealth;
        lastHealth.insert(QStringLiteral("state"), healthInfo.state);
        lastHealth.insert(QStringLiteral("message"), healthInfo.message);
        lastHealth.insert(QStringLiteral("tool_count"), healthInfo.toolCount);
        lastHealth.insert(QStringLiteral("checked_at_utc"),
                          healthInfo.checkedAtUtc.toString(Qt::ISODateWithMs));
        entry.insert(QStringLiteral("last_health"), lastHealth);
        plugins.insert(it.key(), entry);
    }
    m_configObject.insert(QStringLiteral("plugins"), plugins);
}

QString ToolPluginManager::canonicalDirPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return QString();
    return QDir(path).canonicalPath();
}

bool ToolPluginManager::isCompatible(const TmAgent::ToolPluginDescriptor& descriptor) const
{
    // 旧版本插件（sdkVersionMajor=0）始终兼容
    if (descriptor.sdkVersionMajor == 0) {
        qInfo() << "[ToolPluginManager] legacy plugin detected (sdkVersionMajor=0):"
                << descriptor.pluginId << "- always compatible";
        return true;
    }
    
    // 主版本号必须匹配
    if (descriptor.sdkVersionMajor != TMAGENT_SDK_VERSION_MAJOR) {
        return false;
    }
    
    // 次版本号：插件可以使用旧版本 SDK（向前兼容）
    // 但不能使用比主应用更新的 SDK 版本
    if (descriptor.sdkVersionMinor > TMAGENT_SDK_VERSION_MINOR) {
        return false;
    }
    
    return true;
}

void ToolPluginManager::recordFailedPlugin(const QString& path, const QString& pluginId, const QString& error)
{
    FailedPluginInfo info;
    info.path = path;
    info.pluginId = pluginId.isEmpty() ? QStringLiteral("<unknown>") : pluginId;
    info.error = error;
    info.timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    m_failedPlugins.append(info);
    
    qWarning() << "[ToolPluginManager] plugin load failed:" << info.pluginId 
               << "from" << path << "-" << error;
}

QList<ToolPluginManager::FailedPluginInfo> ToolPluginManager::failedPlugins() const
{
    return m_failedPlugins;
}

bool ToolPluginManager::retryLoadPlugin(const QString& pluginId)
{
    const QString key = pluginId.trimmed();
    if (key.isEmpty())
        return false;
    
    // 查找失败列表中的插件
    QString pluginPath;
    for (const FailedPluginInfo& info : m_failedPlugins) {
        if (info.pluginId == key) {
            pluginPath = info.path;
            break;
        }
    }
    
    if (pluginPath.isEmpty()) {
        qWarning() << "[ToolPluginManager] plugin not found in failed list:" << key;
        return false;
    }
    
    // 从失败列表中移除
    m_failedPlugins.erase(
        std::remove_if(m_failedPlugins.begin(), m_failedPlugins.end(),
                      [&key](const FailedPluginInfo& info) { return info.pluginId == key; }),
        m_failedPlugins.end());
    
    // 尝试重新加载
    qInfo() << "[ToolPluginManager] retrying plugin load:" << key << "from" << pluginPath;
    const bool success = tryLoadPlugin(pluginPath);
    
    if (success) {
        applyConfigToLoadedPlugins();
        qInfo() << "[ToolPluginManager] plugin retry successful:" << key;
    }
    
    return success;
}
