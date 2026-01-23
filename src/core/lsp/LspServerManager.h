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
 * @brief LSP 服务管理器 (参考 opencode LSPServer 设计)
 * 
 * 负责：
 * 1. 探测系统中可用的各种 LSP 服务器 (clangd, pyright 等)
 * 2. 自动管理不同项目根目录下的客户端实例 (单例管理)
 * 3. 跨平台兼容性处理
 */
class LspServerManager : public QObject
{
    Q_OBJECT
public:
    explicit LspServerManager(QObject *parent = nullptr);
    ~LspServerManager();

    static LspServerManager* instance();

    /**
     * @brief 为指定文件获取或创建一个 LSP 客户端
     * @param filePath 文件绝对路径
     * @return 对应的 LspClient 指针，若不支持提供该语言则返回 nullptr
     */
    LspClient* getClientForFile(const QString &filePath);

    /**
     * @brief 手动探测并添加 LSP 服务器路径
     */
    void registerServer(const QString &languageId, const QString &serverPath, const QStringList &args = {});

    /**
     * @brief clangd 是否已可用
     */
    bool isClangdAvailable();

    /**
     * @brief 获取 clangd 路径（若不存在则返回空）
     */
    QString clangdPath();

    /**
     * @brief 是否正在下载 clangd
     */
    bool isClangdDownloadInProgress() const;

    /**
     * @brief 确保 clangd 后台下载，并在完成时回调
     */
    void addClangdWaiter(const std::function<void(bool, const QString&)> &callback);

signals:
    void clangdDownloadFinished(bool success, const QString &path);

private:
    QString findClangdPath(bool allowDownload);
    QString findProjectRoot(const QString &filePath);
    bool ensureServerConfig(const QString &languageId);
    void ensureClangdAsync();
    void notifyClangdWaiters(bool success, const QString &path);
    
    // key: rootPath + languageId
    QHash<QString, LspClient*> m_clients;
    
    // key: languageId -> server config
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
