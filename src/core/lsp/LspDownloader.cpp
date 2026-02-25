#include "LspDownloader.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

static bool copyRecursively(const QString& srcPath, const QString& dstPath)
{
    QDir srcDir(srcPath);
    if (!srcDir.exists())
        return false;

    QDir dstDir(dstPath);
    if (!dstDir.exists() && !dstDir.mkpath("."))
        return false;

    const QFileInfoList entries = srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files);
    for (const QFileInfo& entry : entries) {
        const QString src = entry.absoluteFilePath();
        const QString dst = dstDir.filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyRecursively(src, dst))
                return false;
        } else {
            QFile::remove(dst);
            if (!QFile::copy(src, dst))
                return false;
        }
    }
    return true;
}

LspDownloader::LspDownloader(QObject* parent) : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    const QString appDir = QCoreApplication::applicationDirPath();
    QString preferred = appDir + "/lsp_bin";
    QDir dir(preferred);
    if (!dir.exists() && !dir.mkpath(".")) {
        qWarning() << "LspDownloader: 无法在程序目录创建 lsp_bin，回退到 AppDataLocation";
        m_storageDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/lsp_bin";
        QDir().mkpath(m_storageDir);
    } else {
        m_storageDir = preferred;
    }
}

void LspDownloader::checkAndDownloadClangd()
{
    if (QFile::exists(getLocalClangdPath()))
        return;

    qDebug() << "LspDownloader: 正在获取 clangd 最新版本信息...";
    QUrl url("https://api.github.com/repos/clangd/clangd/releases/latest");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "TmAgent-Qt/1.0");

    QNetworkReply* reply = m_network->get(request);
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
        emit downloadFinished(false, "");
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    parseReleaseJson(data);
}

void LspDownloader::parseReleaseJson(const QByteArray& data)
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
    for (const auto& val : assets) {
        QString name = val.toObject()["name"].toString();
        if (name.contains(platform) && (name.endsWith(".zip") || name.endsWith(".tar.xz"))) {
            downloadUrl = val.toObject()["browser_download_url"].toString();
            break;
        }
    }

    if (downloadUrl.isEmpty()) {
        qWarning() << "LspDownloader: 未发现匹配当前平台的资产";
        emit downloadFinished(false, "");
        return;
    }

    startDownload(QUrl(downloadUrl), 0);
}

static QString filenameFromContentDisposition(const QNetworkReply* reply)
{
    const QVariant header = reply->header(QNetworkRequest::ContentDispositionHeader);
    const QString disposition = header.toString();
    if (disposition.isEmpty())
        return QString();

    // e.g. attachment; filename="clangd-windows-21.1.8.zip"
    static const QRegularExpression re("filename\\*?=\\s*\"?([^\";]+)\"?", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = re.match(disposition);
    if (match.hasMatch()) {
        return match.captured(1);
    }
    return QString();
}

void LspDownloader::extractArchive(const QString& filePath, const QString& destDir)
{
    QDir().mkpath(destDir);
    QProcess* proc = new QProcess(this);

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
            QString source;
            if (!subdirs.isEmpty()) {
                source = destDir + "/" + subdirs.first();
            } else {
#ifdef Q_OS_WIN
                const QString exeName = "clangd.exe";
#else
                const QString exeName = "clangd";
#endif
                QDirIterator it(destDir, QStringList() << exeName, QDir::Files, QDirIterator::Subdirectories);
                if (it.hasNext()) {
                    QFileInfo exeInfo(it.next());
                    QDir binDir = exeInfo.absoluteDir();
                    binDir.cdUp();
                    source = binDir.absolutePath();
                }
            }

            if (source.isEmpty() || !QDir(source).exists()) {
                qWarning() << "LspDownloader: 未找到 clangd 解压目录" << destDir;
                emit downloadFinished(false, "");
                return;
            }

            QString target = m_storageDir + "/clangd";
            QDir(target).removeRecursively();
            if (QDir().rename(source, target)) {
                qDebug() << "LspDownloader: 安装完成" << target;
                emit downloadFinished(true, target);
            } else {
                qWarning() << "LspDownloader: 重命名失败，尝试拷贝安装" << source << "->" << target;
                if (copyRecursively(source, target)) {
                    qDebug() << "LspDownloader: 拷贝安装完成" << target;
                    emit downloadFinished(true, target);
                } else {
                    qWarning() << "LspDownloader: 安装失败";
                    emit downloadFinished(false, "");
                    return;
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

void LspDownloader::startDownload(const QUrl& url, int redirectCount)
{
    static constexpr int kMaxRedirects = 5;
    if (!url.isValid()) {
        qWarning() << "LspDownloader: 无效的下载地址";
        emit downloadFinished(false, "");
        return;
    }
    if (redirectCount > kMaxRedirects) {
        qWarning() << "LspDownloader: 重定向次数过多";
        emit downloadFinished(false, "");
        return;
    }

    qDebug() << "LspDownloader: 开始从" << url.toString() << "下载...";
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "TmAgent-Qt/1.0");
    QNetworkReply* reply = m_network->get(request);

    connect(reply, &QNetworkReply::finished, [this, reply, redirectCount]() {
        const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectTarget.isValid()) {
            QUrl newUrl = reply->url().resolved(redirectTarget.toUrl());
            reply->deleteLater();
            startDownload(newUrl, redirectCount + 1);
            return;
        }

        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            if (data.isEmpty()) {
                qWarning() << "LspDownloader: 下载内容为空";
                emit downloadFinished(false, "");
            } else {
                QString fileName = filenameFromContentDisposition(reply);
                if (fileName.isEmpty()) {
                    fileName = QFileInfo(reply->url().path()).fileName();
                }
                if (fileName.isEmpty()) {
                    fileName = "clangd-download";
                }
                if (!fileName.contains('.') && data.size() > 4 && data.startsWith("PK")) {
                    fileName += ".zip";
                }
                QString archivePath = m_storageDir + "/" + fileName;
                QFile file(archivePath);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(data);
                    file.close();
                    qDebug() << "LspDownloader: 下载完成，准备解压" << archivePath;
                    extractArchive(archivePath, m_storageDir + "/clangd_extracted");
                } else {
                    qWarning() << "LspDownloader: 无法写入下载文件" << archivePath;
                    emit downloadFinished(false, "");
                }
            }
        } else {
            qWarning() << "LspDownloader: 下载失败" << reply->errorString();
            emit downloadFinished(false, "");
        }
        reply->deleteLater();
    });
}
