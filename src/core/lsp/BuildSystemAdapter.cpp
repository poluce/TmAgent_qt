#include "BuildSystemAdapter.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

BuildSystemAdapter::BuildSystemAdapter(QObject* parent) : QObject(parent)
{
}

bool BuildSystemAdapter::prepareCompileCommands(const QString& rootPath)
{
    QDir dir(rootPath);

    // 1. 优先检查 CMake
    if (dir.exists("CMakeLists.txt")) {
        return handleCmake(rootPath);
    }

    // 2. 检查 qmake
    QStringList proFiles = dir.entryList({ "*.pro" }, QDir::Files);
    if (!proFiles.isEmpty()) {
        return handleQmake(rootPath);
    }

    return false;
}

bool BuildSystemAdapter::handleQmake(const QString& rootPath)
{
    qDebug() << "BuildSystemAdapter: 检测到 qmake 项目，正在尝试生成编译数据库...";

    // 方案：使用 compiledb 工具（需环境中有 python + pip install compiledb）
    // compiledb 可以在不修改 .pro 的情况下通过解析 Makefile 生成 JSON
    return runCompiledb(rootPath);
}

bool BuildSystemAdapter::handleCmake(const QString& rootPath)
{
    qDebug() << "BuildSystemAdapter: 检测到 cmake 项目，正在启用导出开关...";

    QProcess proc;
    proc.setWorkingDirectory(rootPath);
    // 运行 cmake 生成阶段并导出命令
    proc.start("cmake", { "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "." });
    return proc.waitForFinished() && proc.exitCode() == 0;
}

bool BuildSystemAdapter::runCompiledb(const QString& rootPath)
{
    // 首先尝试直接运行 compiledb
    QProcess proc;
    proc.setWorkingDirectory(rootPath);

    // 1. 先确保 Makefile 存在
    QProcess qmakeProc;
    qmakeProc.setWorkingDirectory(rootPath);
    qmakeProc.start("qmake");
    if (!qmakeProc.waitForFinished() || qmakeProc.exitCode() != 0) {
        qWarning() << "BuildSystemAdapter: qmake 失败";
        return false;
    }

    // 2. 运行 compiledb 捕获构建结构
    // -n 表示 dry-run，不实际编译，只解析工程
    proc.start("compiledb", { "-n", "qmake" });
    if (proc.waitForFinished() && proc.exitCode() == 0) {
        qDebug() << "BuildSystemAdapter: compile_commands.json 生成成功";
        return true;
    }

    // 备选方案：如果是 Windows 环境，尝试 python -m compiledb
    proc.start("python", { "-m", "compiledb", "-n", "qmake" });
    return proc.waitForFinished() && proc.exitCode() == 0;
}
