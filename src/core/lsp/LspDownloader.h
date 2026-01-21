#ifndef LSPDOWNLOADER_H
#define LSPDOWNLOADER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QDir>

/**
 * @brief LSP 自动下载器
 * 
 * 类似 opencode server.ts 中的下载逻辑：
 * 1. 探测操作系统和架构
 * 2. 从 GitHub 获取最新版本的下载 URL
 * 3. 下载并解压到特定目录 (~/.tmagent/bin)
 */
class LspDownloader : public QObject
{
    Q_OBJECT
public:
    explicit LspDownloader(QObject *parent = nullptr);

    /**
     * @brief 检查并下载 clangd
     * @return 若已存在或启动下载返回 true
     */
    void checkAndDownloadClangd();

    QString getLocalClangdPath() const;

signals:
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(bool success, QString path);

private slots:
    void onReleaseInfoReceived();

private:
    void parseReleaseJson(const QByteArray &data);
    void extractArchive(const QString &filePath, const QString &destDir);

    QNetworkAccessManager *m_network;
    QString m_storageDir;
};

#endif // LSPDOWNLOADER_H
