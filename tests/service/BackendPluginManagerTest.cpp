#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>

#include "TeammateManager.h"
#include "core/backend/BackendPluginManager.h"

namespace {

int fail(const QString& expected, const QString& actual)
{
    qDebug().noquote() << "  [期望]" << expected;
    qDebug().noquote() << "  [实际]" << actual;
    return 1;
}

QString runtimePluginDir()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("plugins/backends"));
}

QString runtimePluginBackupDir()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("plugins/backends.disabled-test"));
}

QString findRepoRoot(const QString& startDir)
{
    QDir dir(startDir);
    while (dir.exists()) {
        if (dir.exists(QStringLiteral("TmAgent.pro")))
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    return QString();
}

QStringList buildPluginDirs()
{
    const QString repoRoot = findRepoRoot(QCoreApplication::applicationDirPath());
    if (repoRoot.isEmpty())
        return {};

    return {
        QDir(repoRoot).filePath(QStringLiteral("build-plugins/release/plugins/backends")),
        QDir(repoRoot).filePath(QStringLiteral("build-plugins/debug/plugins/backends"))
    };
}

QStringList pluginDllFiles(const QString& dirPath)
{
    const QDir dir(dirPath);
    if (!dir.exists())
        return {};
    return dir.entryList({QStringLiteral("*.dll")}, QDir::Files, QDir::Name);
}

bool ensureRuntimePlugins(QString* error)
{
    const QString runtimeDirPath = runtimePluginDir();
    QDir runtimeDir(runtimeDirPath);
    if (!runtimeDir.exists() && !QDir().mkpath(runtimeDirPath)) {
        if (error)
            *error = QStringLiteral("failed to create runtime plugin dir: %1").arg(runtimeDirPath);
        return false;
    }

    const QStringList existingDlls = pluginDllFiles(runtimeDirPath);
    if (!existingDlls.isEmpty())
        return true;

    for (const QString& srcDirPath : buildPluginDirs()) {
        const QStringList dlls = pluginDllFiles(srcDirPath);
        if (dlls.isEmpty())
            continue;

        for (const QString& dll : dlls) {
            const QString srcPath = QDir(srcDirPath).filePath(dll);
            const QString destPath = runtimeDir.filePath(dll);
            QFile::remove(destPath);
            if (!QFile::copy(srcPath, destPath)) {
                if (error) {
                    *error = QStringLiteral("failed to copy plugin dll: %1 -> %2")
                                 .arg(srcPath, destPath);
                }
                return false;
            }
        }
        return true;
    }

    if (error)
        *error = QStringLiteral("no plugin dlls found under build-plugins outputs");
    return false;
}

bool renameDirPath(const QString& fromPath, const QString& toPath, QString* error)
{
    const QFileInfo fromInfo(fromPath);
    const QFileInfo toInfo(toPath);
    if (!fromInfo.exists() || !fromInfo.isDir()) {
        if (error)
            *error = QStringLiteral("source dir missing: %1").arg(fromPath);
        return false;
    }
    QDir parentDir = fromInfo.dir();
    if (parentDir.absolutePath() != toInfo.dir().absolutePath()) {
        if (error)
            *error = QStringLiteral("rename requires same parent: %1 -> %2").arg(fromPath, toPath);
        return false;
    }
    if (QDir(toPath).exists()) {
        if (!QDir(toPath).removeRecursively()) {
            if (error)
                *error = QStringLiteral("failed to clear existing target dir: %1").arg(toPath);
            return false;
        }
    }
    if (!parentDir.rename(fromInfo.fileName(), toInfo.fileName())) {
        if (error)
            *error = QStringLiteral("rename failed: %1 -> %2").arg(fromPath, toPath);
        return false;
    }
    return true;
}

int runSelfCheck(const QStringList& args, QString* output)
{
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments(args);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("TMAGENT_BACKEND_PLUGIN_DIRS"));
    process.setProcessEnvironment(env);
    process.start();
    if (!process.waitForStarted(5000)) {
        if (output)
            *output = QStringLiteral("failed to start child process");
        return -1;
    }
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(3000);
        if (output)
            *output = QStringLiteral("child process timeout");
        return -1;
    }

    const QString stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError());
    if (output)
        *output = stdoutText + stderrText;
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

int verifyRuntimePluginDiscovery()
{
    qunsetenv("TMAGENT_BACKEND_PLUGIN_DIRS");

    if (!QDir(runtimePluginDir()).exists())
        return fail(QStringLiteral("runtime plugins/backends directory exists"), runtimePluginDir());
    const QStringList runtimeDlls = pluginDllFiles(runtimePluginDir());
    if (!runtimeDlls.contains(QStringLiteral("CodexBackendPlugin.dll")))
        return fail(QStringLiteral("runtime plugins include CodexBackendPlugin.dll"),
                    runtimeDlls.join(QStringLiteral(", ")));
    if (!runtimeDlls.contains(QStringLiteral("TmagentBackendPlugin.dll")))
        return fail(QStringLiteral("runtime plugins include TmagentBackendPlugin.dll"),
                    runtimeDlls.join(QStringLiteral(", ")));
    if (!QDir(runtimePluginDir()).entryList({QStringLiteral("*.a")}, QDir::Files).isEmpty()) {
        return fail(QStringLiteral("runtime plugins exclude .a import libraries"),
                    QDir(runtimePluginDir()).entryList({QStringLiteral("*.a")}, QDir::Files).join(QStringLiteral(", ")));
    }

    auto* manager = BackendPluginManager::instance();
    manager->initialize();

    const QStringList delegateIds = manager->delegateBackendIds();
    const QStringList teammateIds = manager->teammateBackendIds();

    if (!delegateIds.contains(QStringLiteral("tmagent")))
        return fail(QStringLiteral("delegate backends contain tmagent"), delegateIds.join(QStringLiteral(", ")));
    if (!delegateIds.contains(QStringLiteral("codex")))
        return fail(QStringLiteral("delegate backends contain codex"), delegateIds.join(QStringLiteral(", ")));
    if (!teammateIds.contains(QStringLiteral("codex")))
        return fail(QStringLiteral("teammate backends contain codex"), teammateIds.join(QStringLiteral(", ")));
    if (teammateIds.contains(QStringLiteral("tmagent")))
        return fail(QStringLiteral("teammate backends exclude tmagent"), teammateIds.join(QStringLiteral(", ")));

    const BackendDescriptor codex = manager->backendDescriptor(QStringLiteral("codex"));
    if (!codex.isValid())
        return fail(QStringLiteral("valid codex descriptor"), QStringLiteral("invalid"));
    if (!codex.supportsDelegate || !codex.supportsTeammate)
        return fail(QStringLiteral("codex supports delegate+teammate"), QStringLiteral("false"));

    if (!manager->delegateBackend(QStringLiteral("codex")))
        return fail(QStringLiteral("non-null codex delegate backend"), QStringLiteral("null"));
    if (!manager->teammateBackend(QStringLiteral("codex")))
        return fail(QStringLiteral("non-null codex teammate backend"), QStringLiteral("null"));

    qDebug().noquote() << "BackendPluginManager runtime discovery smoke passed.";
    return 0;
}

int verifyMissingRuntimePlugins()
{
    qunsetenv("TMAGENT_BACKEND_PLUGIN_DIRS");

    auto* manager = BackendPluginManager::instance();
    manager->initialize();

    const QStringList delegateIds = manager->delegateBackendIds();
    const QStringList teammateIds = manager->teammateBackendIds();
    if (!delegateIds.isEmpty())
        return fail(QStringLiteral("no delegate backends"), delegateIds.join(QStringLiteral(", ")));
    if (!teammateIds.isEmpty())
        return fail(QStringLiteral("no teammate backends"), teammateIds.join(QStringLiteral(", ")));
    if (manager->delegateBackend(QStringLiteral("tmagent")))
        return fail(QStringLiteral("null tmagent delegate backend"), QStringLiteral("non-null"));
    if (manager->teammateBackend(QStringLiteral("codex")))
        return fail(QStringLiteral("null codex teammate backend"), QStringLiteral("non-null"));

    auto* teammateManager = TeammateManager::instance();
    if (!teammateManager->registeredBackendIds().isEmpty()) {
        return fail(QStringLiteral("teammate manager has no registered backends"),
                    teammateManager->registeredBackendIds().join(QStringLiteral(", ")));
    }

    Teammate::Config config;
    config.name = QStringLiteral("missing-backend");
    const TeammateManager::CreateResult createResult = teammateManager->createTeammate(config);
    if (createResult.success)
        return fail(QStringLiteral("createTeammate fails without registered backend"), QStringLiteral("success"));
    if (!createResult.error.contains(QStringLiteral("未注册的后端"))) {
        return fail(QStringLiteral("error contains 未注册的后端"),
                    createResult.error);
    }

    qDebug().noquote() << "BackendPluginManager missing-runtime smoke passed.";
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    if (args.contains(QStringLiteral("--verify-runtime-discovery")))
        return verifyRuntimePluginDiscovery();
    if (args.contains(QStringLiteral("--verify-missing-runtime")))
        return verifyMissingRuntimePlugins();

    QString ensureError;
    if (!ensureRuntimePlugins(&ensureError))
        return fail(QStringLiteral("runtime plugin dlls prepared"), ensureError);

    QString childOutput;
    const int runtimeRc = runSelfCheck({QStringLiteral("--verify-runtime-discovery")}, &childOutput);
    if (runtimeRc != 0) {
        qDebug().noquote() << childOutput;
        return fail(QStringLiteral("runtime-discovery child exits 0"), QString::number(runtimeRc));
    }

#ifdef QT_NO_DEBUG
    const QString pluginDir = runtimePluginDir();
    const QString backupDir = runtimePluginBackupDir();
    QString renameError;
    if (!renameDirPath(pluginDir, backupDir, &renameError))
        return fail(QStringLiteral("rename runtime plugin dir for missing-path test"), renameError);

    QString missingOutput;
    const int missingRc = runSelfCheck({QStringLiteral("--verify-missing-runtime")}, &missingOutput);

    QString restoreError;
    const bool restored = renameDirPath(backupDir, pluginDir, &restoreError);
    if (!restored)
        return fail(QStringLiteral("restore runtime plugin dir"), restoreError);

    if (missingRc != 0) {
        qDebug().noquote() << missingOutput;
        return fail(QStringLiteral("missing-runtime child exits 0"), QString::number(missingRc));
    }
#else
    qDebug().noquote() << "Skipping missing-runtime smoke in debug; repo fallback is intentionally enabled.";
#endif

    qDebug().noquote() << "BackendPluginManager smoke test passed.";
    return 0;
}
