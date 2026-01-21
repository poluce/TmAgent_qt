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
    // 初始化默认配置
    QString clangd = findClangdPath();
    if (!clangd.isEmpty()) {
        registerServer("cpp", clangd, {"--background-index", "--clang-tidy"});
    }
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
    
    // 简单映射
    QString langId;
    if (ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc") langId = "cpp";
    
    if (langId.isEmpty() || !m_serverConfigs.contains(langId)) return nullptr;
    
    QString root = findProjectRoot(filePath);

    // 在启动 Client 之前，确保 C++ 编译数据库已就绪
    if (langId == "cpp") {
        BuildSystemAdapter adapter;
        adapter.prepareCompileCommands(root);
    }
    
    QString key = root + ":" + langId;
    
    if (m_clients.contains(key)) return m_clients[key];
    
    auto config = m_serverConfigs[langId];
    LspClient* client = new LspClient(config.path, config.args, this);
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

QString LspServerManager::findClangdPath()
{
    // 1. 尝试环境变量
    QString path = QProcessEnvironment::systemEnvironment().value("CLANGD_PATH");
    if (!path.isEmpty() && QFileInfo::exists(path)) return path;
    
    // 2. 尝试系统搜索 (Windows 下可能需要 .exe)
#ifdef Q_OS_WIN
    QString executable = "clangd.exe";
#else
    QString executable = "clangd";
#endif

    // 使用 QProcess 搜索路径
    QProcess proc;
    QString cmd = "where"; // Windows
#ifndef Q_OS_WIN
    cmd = "which";
#endif
    proc.start(cmd, {executable});
    if (proc.waitForFinished() && proc.exitCode() == 0) {
        return QString(proc.readAllStandardOutput()).trimmed();
    }
    
    // 3. 尝试检查下载目录
    LspDownloader downloader;
    QString local = downloader.getLocalClangdPath();
    if (QFileInfo::exists(local)) return local;

    // 4. 如果都找不到，触发下载
    downloader.checkAndDownloadClangd();
    
    return QString();
}

QString LspServerManager::findProjectRoot(const QString &filePath)
{
    QDir dir = QFileInfo(filePath).absoluteDir();
    while (dir.exists() && !dir.isRoot()) {
        // 寻找标识文件
        if (dir.exists("compile_commands.json") || 
            dir.exists("TmAgent.pro") || 
            dir.exists("CMakeLists.txt") ||
            dir.exists(".git")) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) break;
    }
    return QFileInfo(filePath).absolutePath();
}
