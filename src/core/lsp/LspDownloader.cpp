#include "LspDownloader.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSysInfo>
#include <QStandardPaths>
#include <QProcess>
#include <QDebug>

LspDownloader::LspDownloader(QObject *parent) : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    m_storageDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/lsp_bin";
    QDir().mkpath(m_storageDir);
}

void LspDownloader::checkAndDownloadClangd()
{
    if (QFile::exists(getLocalClangdPath())) return;

    qDebug() << "LspDownloader: 正在获取 clangd 最新版本信息...";
    QUrl url("https://api.github.com/repos/clangd/clangd/releases/latest");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "TmAgent-Qt/1.0");

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, &LspDownloader::onReleaseInfoReceived);
}

QString LspDownloader::getLocalClangdPath() const
{
#ifdef Q_OS_WIN
    return m_storageDir + "/clangd/bin/clangd.exe";
#else
    return m_storageDir + "/clangd/bin/clangd";
#endif
}

void LspDownloader::onReleaseInfoReceived()
{
    auto reply = qobject_cast<QNetworkReply*>(sender());
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "LspDownloader: 获取版本信息失败" << reply->errorString();
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    parseReleaseJson(data);
}

void LspDownloader::parseReleaseJson(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject obj = doc.object();
    QJsonArray assets = obj["assets"].toArray();

    QString platform;
#ifdef Q_OS_WIN
    platform = "windows";
#elif defined(Q_OS_MAC)
    platform = "mac";
#else
    platform = "linux";
#endif

    QString downloadUrl;
    for (const auto &val : assets) {
        QString name = val.toObject()["name"].toString();
        if (name.contains(platform) && (name.endsWith(".zip") || name.endsWith(".tar.xz"))) {
            downloadUrl = val.toObject()["browser_download_url"].toString();
            break;
        }
    }

    if (downloadUrl.isEmpty()) {
        qWarning() << "LspDownloader: 未发现匹配当前平台的资产";
        return;
    }

    qDebug() << "LspDownloader: 开始从" << downloadUrl << "下载...";
    QNetworkRequest request(downloadUrl);
    QNetworkReply *reply = m_network->get(request);
    
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString archivePath = m_storageDir + "/" + QFileInfo(reply->url().path()).fileName();
            QFile file(archivePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reply->readAll());
                file.close();
                qDebug() << "LspDownloader: 下载完成，准备解压" << archivePath;
                extractArchive(archivePath, m_storageDir + "/clangd_extracted");
            }
        }
        reply->deleteLater();
    });
}

void LspDownloader::extractArchive(const QString &filePath, const QString &destDir)
{
    QDir().mkpath(destDir);
    QProcess *proc = new QProcess(this);
    
    QString program;
    QStringList arguments;

#ifdef Q_OS_WIN
    // Windows: 使用 PowerShell 的 Expand-Archive
    program = "powershell";
    arguments << "-NoProfile" << "-ExecutionPolicy" << "Bypass" << "-Command"
              << QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(filePath, destDir);
#else
    // Linux/Mac: 使用 tar
    program = "tar";
    arguments << "-xf" << filePath << "-C" << destDir;
#endif

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), [this, proc, filePath, destDir](int exitCode) {
        if (exitCode == 0) {
            qDebug() << "LspDownloader: 解压成功";
            
            // 将解压后的目录重命名为最终的 'clangd' 目录
            // 提示：clangd 压缩包通常包含一个顶层文件夹如 clangd_18.1.3
            QDir dir(destDir);
            QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            if (!subdirs.isEmpty()) {
                QString source = destDir + "/" + subdirs.first();
                QString target = m_storageDir + "/clangd";
                
                // 清理旧目录
                QDir(target).removeRecursively();
                
                if (QDir().rename(source, target)) {
                    qDebug() << "LspDownloader: 安装完成" << target;
                    emit downloadFinished(true, target);
                }
            }
        } else {
            qWarning() << "LspDownloader: 解压失败，错误码:" << exitCode;
            emit downloadFinished(false, "");
        }
        
        // 清理临时文件
        QFile::remove(filePath);
        QDir(destDir).removeRecursively();
        proc->deleteLater();
    });

    proc->start(program, arguments);
}
