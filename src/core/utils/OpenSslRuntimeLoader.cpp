#include "OpenSslRuntimeLoader.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

#include <memory>
#include <vector>

namespace {

#ifdef Q_OS_WIN
std::vector<std::unique_ptr<QLibrary>> s_loadedLibraries;
bool s_openSslInitialized = false;

void prependOpenSslDirToProcessPath(const QString& opensslDir)
{
    const QString nativeOpenSslDir = QDir::toNativeSeparators(opensslDir);
    const QString currentPath = QString::fromLocal8Bit(qgetenv("PATH"));
    const QStringList entries = currentPath.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& entry : entries) {
        if (QDir::cleanPath(entry).compare(QDir::cleanPath(nativeOpenSslDir), Qt::CaseInsensitive)
            == 0) {
            return;
        }
    }

    QString updatedPath = nativeOpenSslDir;
    if (!currentPath.isEmpty())
        updatedPath += QLatin1Char(';') + currentPath;
    qputenv("PATH", updatedPath.toLocal8Bit());
}
#endif

} // namespace

void OpenSslRuntimeLoader::initialize()
{
#ifndef Q_OS_WIN
    return;
#else
    if (s_openSslInitialized)
        return;

    s_openSslInitialized = true;

    const QString opensslDir =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources/openssl"));
    prependOpenSslDirToProcessPath(opensslDir);

    const auto loadLibrary = [&opensslDir](const QString& fileName) {
        const QString libraryPath = QDir(opensslDir).filePath(fileName);
        if (!QFileInfo::exists(libraryPath)) {
            qWarning().noquote() << "OpenSSL runtime DLL not found:" << libraryPath;
            return false;
        }

        auto library = std::make_unique<QLibrary>(libraryPath);
        if (!library->load()) {
            qWarning().noquote()
                << "Failed to load OpenSSL runtime DLL:" << libraryPath << library->errorString();
            return false;
        }

        s_loadedLibraries.push_back(std::move(library));
        return true;
    };

    if (!loadLibrary(QStringLiteral("libcrypto-1_1.dll")))
        return;

    loadLibrary(QStringLiteral("libssl-1_1.dll"));
#endif
}
