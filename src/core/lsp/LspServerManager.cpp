#include "LspServerManager.h"
#include "LspClient.h"
#include "LspDownloader.h"
#include "BuildSystemAdapter.h"
#include <QFileInfo>
#include <QDir>
#include <QProcessEnvironment>
#include <QDebug>

LspServerManager::LspServerManager(QObject *parent) : QObject(parent)
{
    QString clangd = findClangdPath(false);
    if (!clangd.isEmpty())
        registerServer("cpp", clangd, {"--background-index", "--clang-tidy"});
}

LspServerManager::~LspServerManager()
{
}

LspServerManager* LspServerManager::instance()
{
    static LspServerManager manager;
    return &manager;
}

LspClient* LspServerManager::getClientForFile(const QString &filePath)
{
    QFileInfo info(filePath);
    QString ext = "." + info.suffix().toLower();

    QString langId;
    if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc")
        langId = "cpp";

    if (langId.isEmpty())
        return nullptr;
    if (!m_serverConfigs.contains(langId) && !ensureServerConfig(langId))
        return nullptr;

    QString root = findProjectRoot(filePath);

    if (langId == "cpp") {
        BuildSystemAdapter adapter;
        adapter.prepareCompileCommands(root);
    }

    QString key = root + ":" + langId;
    if (m_clients.contains(key))
        return m_clients[key];

    auto config = m_serverConfigs[langId];
    auto* client = new LspClient(config.path, config.args, this);
    if (client->start(root)) {
        m_clients[key] = client;
        return client;
    }

    delete client;
    return nullptr;
}

void LspServerManager::registerServer(const QString &languageId, const QString &serverPath, const QStringList &args)
{
    m_serverConfigs[languageId] = {serverPath, args};
}

bool LspServerManager::ensureServerConfig(const QString &languageId)
{
    if (m_serverConfigs.contains(languageId))
        return true;
    if (languageId == "cpp") {
        QString clangd = findClangdPath(false);
        if (!clangd.isEmpty()) {
            registerServer("cpp", clangd, {"--background-index", "--clang-tidy"});
            return true;
        }
        ensureClangdAsync();
    }
    return false;
}

bool LspServerManager::isClangdAvailable()
{
    return QFileInfo::exists(findClangdPath(false));
}

QString LspServerManager::clangdPath()
{
    return findClangdPath(false);
}

bool LspServerManager::isClangdDownloadInProgress() const
{
    return m_clangdDownloadInProgress;
}

void LspServerManager::addClangdWaiter(const std::function<void(bool, const QString&)> &callback)
{
    QString path = findClangdPath(false);
    if (!path.isEmpty()) {
        callback(true, path);
        return;
    }
    m_clangdWaiters.append(callback);
    ensureClangdAsync();
}

void LspServerManager::ensureClangdAsync()
{
    if (m_clangdDownloadInProgress) return;
    if (!findClangdPath(false).isEmpty()) return;

    if (!m_downloader) {
        m_downloader = new LspDownloader(this);
        connect(m_downloader, &LspDownloader::downloadFinished, this,
                [this](bool success, const QString &path) {
                    m_clangdDownloadInProgress = false;
                    QString finalPath = path;
                    if (finalPath.isEmpty()) {
                        finalPath = m_downloader->getLocalClangdPath();
                    }
                    if (!QFileInfo::exists(finalPath)) {
                        success = false;
                    } else {
                        registerServer("cpp", finalPath, {"--background-index", "--clang-tidy"});
                    }
                    notifyClangdWaiters(success, finalPath);
                    emit clangdDownloadFinished(success, finalPath);
                });
    }

    m_clangdDownloadInProgress = true;
    m_downloader->checkAndDownloadClangd();
}

void LspServerManager::notifyClangdWaiters(bool success, const QString &path)
{
    const auto waiters = m_clangdWaiters;
    m_clangdWaiters.clear();
    for (const auto &cb : waiters) {
        cb(success, path);
    }
}

QString LspServerManager::findClangdPath(bool allowDownload)
{
    QString path = QProcessEnvironment::systemEnvironment().value("CLANGD_PATH");
    if (!path.isEmpty() && QFileInfo::exists(path))
        return path;

#ifdef Q_OS_WIN
    QString executable = "clangd.exe";
    QString cmd = "where";
#else
    QString executable = "clangd";
    QString cmd = "which";
#endif

    QProcess proc;
    proc.start(cmd, {executable});
    if (proc.waitForFinished() && proc.exitCode() == 0)
        return QString(proc.readAllStandardOutput()).trimmed();

    LspDownloader downloader;
    QString local = downloader.getLocalClangdPath();
    if (QFileInfo::exists(local))
        return local;

    if (allowDownload)
        ensureClangdAsync();

    return QString();
}

QString LspServerManager::findProjectRoot(const QString &filePath)
{
    QDir dir = QFileInfo(filePath).absoluteDir();
    while (dir.exists() && !dir.isRoot()) {
        if (dir.exists("compile_commands.json") ||
            dir.exists("TmAgent.pro") ||
            dir.exists("CMakeLists.txt") ||
            dir.exists(".git"))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return QFileInfo(filePath).absolutePath();
}
