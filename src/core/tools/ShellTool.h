#ifndef SHELLTOOL_H
#define SHELLTOOL_H

#include <QString>
#include <QProcess>
#include <QFile>
#include <QRegExp>
#include <QDebug>

class ShellTool {
public:
    // 执行 Shell 命令
    static QString executeCommand(const QString& command, 
                                  const QString& workingDir = "") {
        QProcess process;
        
        // 设置工作目录
        if (!workingDir.isEmpty()) {
            process.setWorkingDirectory(workingDir);
        }
        
        // Windows 使用 Git Bash (正确处理 MSYS 路径), Linux/Mac 使用 sh -c
        #ifdef Q_OS_WIN
            // 尝试使用 Git Bash (支持 /e/xxx 格式路径)
            // Git Bash 通常安装在 C:\Program Files\Git\bin\bash.exe
            QString bashPath = "C:/Program Files/Git/bin/bash.exe";
            if (QFile::exists(bashPath)) {
                process.start(bashPath, QStringList() << "-c" << command);
            } else {
                // 回退到 cmd.exe，但需要转换路径
                QString winCommand = convertMsysPathInCommand(command);
                process.start("cmd.exe", QStringList() << "/c" << winCommand);
            }
        #else
            process.start("sh", QStringList() << "-c" << command);
        #endif
        
        // 等待执行完成 (最多 30 秒)
        if (!process.waitForFinished(30000)) {
            return QString("错误: 命令执行超时 (30秒)");
        }
        
        // 获取输出
        QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
        QString error = QString::fromLocal8Bit(process.readAllStandardError());
        int exitCode = process.exitCode();
        
        // 构造结果
        QString result;
        result += QString("退出码: %1\n").arg(exitCode);
        
        if (!output.isEmpty()) {
            result += QString("标准输出:\n%1\n").arg(output);
        }
        
        if (!error.isEmpty()) {
            result += QString("错误输出:\n%1\n").arg(error);
        }
        
        if (output.isEmpty() && error.isEmpty()) {
            result += "命令执行完成,无输出\n";
        }
        
        return result;
    }
    
    // 安全检查 (可选,防止危险命令)
    static bool isSafeCommand(const QString& command) {
        // 危险命令黑名单
        QStringList dangerousCommands = {
            "rm -rf", "del /f", "format", "shutdown", 
            "reboot", "mkfs", "dd if=", "> /dev/"
        };
        
        QString lowerCmd = command.toLower();
        for (const QString& dangerous : dangerousCommands) {
            if (lowerCmd.contains(dangerous.toLower())) {
                return false;
            }
        }
        
        return true;
    }
    
private:
    // 转换命令中的 MSYS 路径为 Windows 路径
    // 例如: cd /e/Document/xxx -> cd E:/Document/xxx
    static QString convertMsysPathInCommand(const QString& command) {
        QString result = command;
        
        // 使用正则表达式匹配所有 /盘符/ 格式的路径
        QRegExp msysPattern("(/([a-zA-Z])/)");
        int pos = 0;
        while ((pos = msysPattern.indexIn(result, pos)) != -1) {
            QString driveLetter = msysPattern.cap(2).toUpper();
            result.replace(pos, msysPattern.matchedLength(), QString("%1:/").arg(driveLetter));
            pos += 3; // 跳过替换后的 "X:/"
        }
        
        return result;
    }
};

#endif // SHELLTOOL_H
