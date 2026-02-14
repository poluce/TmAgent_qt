#ifndef LSPSERVERMANAGER_H
#define LSPSERVERMANAGER_H

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QSharedPointer>
#include <QList>
#include <functional>

class LspClient;
class LspDownloader;

/**
 * @brief LSP 服务管理器
 *
 * 探测系统中可用的 LSP 服务器，管理不同项目根目录下的客户端实例。
 */
class LspServerManager : public QObject
{
    Q_OBJECT
public:
    explicit LspServerManager(QObject *parent = nullptr);
    ~LspServerManager();

    static LspServerManager* instance();

    LspClient* getClientForFile(const QString &filePath);
    void registerServer(const QString &languageId, const QString &serverPath, const QStringList &args = {});
    bool isClangdAvailable();
    QString clangdPath();
    bool isClangdDownloadInProgress() const;
    void addClangdWaiter(const std::function<void(bool, const QString&)> &callback);

signals:
    void clangdDownloadFinished(bool success, const QString &path);

private:
    QString findClangdPath(bool allowDownload);
    QString findProjectRoot(const QString &filePath);
    bool ensureServerConfig(const QString &languageId);
    void ensureClangdAsync();
    void notifyClangdWaiters(bool success, const QString &path);
    
    QHash<QString, LspClient*> m_clients;  // key: rootPath:languageId

    struct ServerConfig {
        QString path;
        QStringList args;
    };
    QHash<QString, ServerConfig> m_serverConfigs;
    QList<std::function<void(bool, const QString&)>> m_clangdWaiters;
    bool m_clangdDownloadInProgress = false;
    LspDownloader *m_downloader = nullptr;
};

#endif // LSPSERVERMANAGER_H
