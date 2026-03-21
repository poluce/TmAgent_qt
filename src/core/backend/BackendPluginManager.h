#ifndef BACKENDPLUGINMANAGER_H
#define BACKENDPLUGINMANAGER_H

#include "IBackendPlugin.h"
#include <QHash>
#include <QMutex>
#include <QObject>

class QPluginLoader;

class BackendPluginManager : public QObject {
    Q_OBJECT
public:
    static BackendPluginManager* instance();

    void initialize();
    bool isInitialized() const;

    QStringList delegateBackendIds();
    QStringList teammateBackendIds();
    BackendDescriptor backendDescriptor(const QString& backendId);

    DelegateBackendInternal::IDelegateBackend* delegateBackend(const QString& backendId);
    ITeammateBackend* teammateBackend(const QString& backendId);

private:
    explicit BackendPluginManager(QObject* parent = nullptr);
    Q_DISABLE_COPY(BackendPluginManager)

    struct LoadedPlugin {
        QString path;
        BackendDescriptor descriptor;
        QPluginLoader* loader = nullptr;
        QObject* instance = nullptr;
        IBackendPlugin* plugin = nullptr;
        DelegateBackendInternal::IDelegateBackend* delegateBackend = nullptr;
        ITeammateBackend* teammateBackend = nullptr;
    };

    void ensureInitialized();
    QStringList candidatePluginDirs() const;
    void discoverPluginsLocked();
    void tryLoadPluginLocked(const QString& filePath);
    static QStringList sortedIds(const QList<LoadedPlugin>& plugins);

    mutable QMutex m_mutex;
    bool m_initialized = false;
    QHash<QString, LoadedPlugin> m_plugins;
};

#endif // BACKENDPLUGINMANAGER_H
