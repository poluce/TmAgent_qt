#include "BackendPluginManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLibrary>
#include <QMutexLocker>
#include <QPluginLoader>
#include <QProcessEnvironment>
#include <QSet>

namespace {

QString canonicalDirPath(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return QString();
    return QDir(path).canonicalPath();
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
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/release/plugins/backends"));
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/debug/plugins/backends"));
#else
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/debug/plugins/backends"));
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/release/plugins/backends"));
#endif
    out << QDir(repoRoot).filePath(QStringLiteral("build-plugins/plugins/backends"));
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

QStringList pluginSearchDirsFromEnv()
{
    const QString raw = QProcessEnvironment::systemEnvironment()
                            .value(QStringLiteral("TMAGENT_BACKEND_PLUGIN_DIRS"))
                            .trimmed();
    if (raw.isEmpty())
        return {};
    return raw.split(QDir::listSeparator(), Qt::SkipEmptyParts);
}

QString metaString(const QJsonObject& meta, const QString& key)
{
    return meta.value(key).toString().trimmed();
}

bool metaBool(const QJsonObject& meta, const QString& key)
{
    return meta.value(key).toBool(false);
}

BackendDescriptor descriptorFromMeta(const QJsonObject& meta)
{
    BackendDescriptor descriptor;
    descriptor.backendId = metaString(meta, QStringLiteral("backend_id"));
    descriptor.displayName = metaString(meta, QStringLiteral("display_name"));
    descriptor.version = metaString(meta, QStringLiteral("version"));
    descriptor.supportsDelegate = metaBool(meta, QStringLiteral("supports_delegate"));
    descriptor.supportsTeammate = metaBool(meta, QStringLiteral("supports_teammate"));
    return descriptor;
}

bool descriptorsMatch(const BackendDescriptor& a, const BackendDescriptor& b)
{
    return a.backendId == b.backendId
        && a.displayName == b.displayName
        && a.version == b.version
        && a.supportsDelegate == b.supportsDelegate
        && a.supportsTeammate == b.supportsTeammate;
}

} // namespace

BackendPluginManager* BackendPluginManager::instance()
{
    static BackendPluginManager manager(nullptr);
    return &manager;
}

BackendPluginManager::BackendPluginManager(QObject* parent)
    : QObject(parent)
{
}

void BackendPluginManager::initialize()
{
    ensureInitialized();
}

bool BackendPluginManager::isInitialized() const
{
    QMutexLocker locker(&m_mutex);
    return m_initialized;
}

QStringList BackendPluginManager::delegateBackendIds()
{
    ensureInitialized();
    QMutexLocker locker(&m_mutex);
    QList<LoadedPlugin> plugins;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        if (it.value().descriptor.supportsDelegate)
            plugins.append(it.value());
    }
    return sortedIds(plugins);
}

QStringList BackendPluginManager::teammateBackendIds()
{
    ensureInitialized();
    QMutexLocker locker(&m_mutex);
    QList<LoadedPlugin> plugins;
    for (auto it = m_plugins.constBegin(); it != m_plugins.constEnd(); ++it) {
        if (it.value().descriptor.supportsTeammate)
            plugins.append(it.value());
    }
    return sortedIds(plugins);
}

BackendDescriptor BackendPluginManager::backendDescriptor(const QString& backendId)
{
    ensureInitialized();
    const QString key = backendId.trimmed();
    QMutexLocker locker(&m_mutex);
    return m_plugins.value(key).descriptor;
}

DelegateBackendInternal::IDelegateBackend* BackendPluginManager::delegateBackend(const QString& backendId)
{
    ensureInitialized();
    const QString key = backendId.trimmed();
    QMutexLocker locker(&m_mutex);
    auto it = m_plugins.find(key);
    if (it == m_plugins.end() || !it->descriptor.supportsDelegate || !it->plugin)
        return nullptr;
    if (!it->delegateBackend)
        it->delegateBackend = it->plugin->createDelegateBackend(this);
    return it->delegateBackend;
}

ITeammateBackend* BackendPluginManager::teammateBackend(const QString& backendId)
{
    ensureInitialized();
    const QString key = backendId.trimmed();
    QMutexLocker locker(&m_mutex);
    auto it = m_plugins.find(key);
    if (it == m_plugins.end() || !it->descriptor.supportsTeammate || !it->plugin)
        return nullptr;
    if (!it->teammateBackend)
        it->teammateBackend = it->plugin->createTeammateBackend(this);
    return it->teammateBackend;
}

void BackendPluginManager::ensureInitialized()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized)
        return;
    discoverPluginsLocked();
    m_initialized = true;
}

QStringList BackendPluginManager::candidatePluginDirs() const
{
    QStringList out;
    out << runtimePluginDirPath();
    out.append(pluginSearchDirsFromEnv());

#ifndef QT_NO_DEBUG
    QSet<QString> repoRoots;
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

QString BackendPluginManager::runtimePluginDirPath() const
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("resources/plugins/backends"));
}

void BackendPluginManager::discoverPluginsLocked()
{
    const QString runtimeDirPath = runtimePluginDirPath();
    const QString runtimeCanonical = canonicalDirPath(runtimeDirPath);
    const QStringList searchDirs = candidatePluginDirs();
    if (searchDirs.isEmpty()) {
        qWarning().noquote() << QStringLiteral(
            "[BackendPluginManager] no plugin directories found; expected runtime dir: %1")
                                     .arg(QDir::toNativeSeparators(runtimeDirPath));
        return;
    }

    qInfo() << "[BackendPluginManager] plugin search dirs:" << searchDirs;
    int runtimeLoadedCount = 0;
    for (const QString& dirPath : searchDirs) {
        qInfo() << "[BackendPluginManager] scanning plugin dir:" << dirPath;
        QDir dir(dirPath);
        const QStringList files = dir.entryList(QDir::Files);
        for (const QString& fileName : files) {
            const QString filePath = dir.filePath(fileName);
            if (!isLoadablePluginFile(filePath))
                continue;
            if (tryLoadPluginLocked(filePath) && !runtimeCanonical.isEmpty() && dirPath == runtimeCanonical)
                ++runtimeLoadedCount;
        }
    }

    if (runtimeLoadedCount == 0) {
        qWarning().noquote() << QStringLiteral(
            "[BackendPluginManager] no first-party backend plugins loaded from runtime dir: %1")
                                     .arg(QDir::toNativeSeparators(runtimeDirPath));
    }
}

bool BackendPluginManager::tryLoadPluginLocked(const QString& filePath)
{
    QPluginLoader* loader = new QPluginLoader(filePath, this);
    const QJsonObject rootMeta = loader->metaData();
    const QJsonObject meta = rootMeta.value(QStringLiteral("MetaData")).toObject();
    const BackendDescriptor metaDescriptor = descriptorFromMeta(meta);
    if (!metaDescriptor.isValid()) {
        qWarning() << "[BackendPluginManager] skipping plugin with invalid metadata:" << filePath;
        delete loader;
        return false;
    }
    if (m_plugins.contains(metaDescriptor.backendId)) {
        const QString existingPath = m_plugins.value(metaDescriptor.backendId).path;
        qWarning() << "[BackendPluginManager] duplicate backend id, skipping plugin:"
                   << metaDescriptor.backendId << filePath
                   << "existing path:" << existingPath;
        delete loader;
        return false;
    }

    QObject* instance = loader->instance();
    if (!instance) {
        qWarning() << "[BackendPluginManager] failed to load plugin:" << filePath << loader->errorString();
        delete loader;
        return false;
    }

    auto* plugin = qobject_cast<IBackendPlugin*>(instance);
    if (!plugin) {
        qWarning() << "[BackendPluginManager] plugin does not implement IBackendPlugin:" << filePath;
        loader->unload();
        delete loader;
        return false;
    }

    const BackendDescriptor runtimeDescriptor = plugin->descriptor();
    if (!runtimeDescriptor.isValid() || !descriptorsMatch(metaDescriptor, runtimeDescriptor)) {
        qWarning() << "[BackendPluginManager] plugin descriptor mismatch, skipping:" << filePath;
        loader->unload();
        delete loader;
        return false;
    }

    LoadedPlugin loaded;
    loaded.path = filePath;
    loaded.descriptor = runtimeDescriptor;
    loaded.loader = loader;
    loaded.instance = instance;
    loaded.plugin = plugin;
    m_plugins.insert(runtimeDescriptor.backendId, loaded);
    qInfo() << "[BackendPluginManager] loaded backend plugin:"
            << runtimeDescriptor.backendId
            << "from" << filePath;
    return true;
}

QStringList BackendPluginManager::sortedIds(const QList<LoadedPlugin>& plugins)
{
    QStringList ids;
    ids.reserve(plugins.size());
    for (const LoadedPlugin& plugin : plugins)
        ids.append(plugin.descriptor.backendId);
    std::sort(ids.begin(), ids.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return ids;
}
