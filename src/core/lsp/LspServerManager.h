#ifndef LSPSERVERMANAGER_H
#define LSPSERVERMANAGER_H

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QSharedPointer>

class LspClient;

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

private:
    QString findClangdPath();
    QString findProjectRoot(const QString &filePath);
    
    // key: rootPath + languageId
    QHash<QString, LspClient*> m_clients;
    
    // key: languageId -> server config
    struct ServerConfig {
        QString path;
        QStringList args;
    };
    QHash<QString, ServerConfig> m_serverConfigs;
};

#endif // LSPSERVERMANAGER_H
