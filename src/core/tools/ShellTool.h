#ifndef SHELLTOOL_H
#define SHELLTOOL_H

#include <QString>
#include <QProcess>
#include <QFile>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include "core/agent/LLMAgent.h"

/**
 * @brief Shell 命令执行工具
 * 
 * 安全机制:
 *   - 内置白名单: 只允许执行预定义的安全命令
 *   - 内置黑名单: 检测并拒绝危险命令（包括变种）
 *   - 安全检查在 executeCommand 内部强制执行，无法绕过
 */
class ShellTool {
public:
    // ==================== 工具 Schema 定义 ====================
    
    /**
     * @brief 获取 execute_command 工具的 Schema 定义
     */
    static Tool getExecuteCommandSchema() {
        Tool tool;
        tool.name = "execute_command";
        tool.description = "执行终端命令并返回结果。可以执行 dir, git, qmake, make 等命令。同一目的的命令尽量合并为一次调用(例如: pwd && ls -la),避免连续多次查询目录/结构。";
        tool.inputSchema = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"command", QJsonObject{
                    {"type", "string"},
                    {"description", "要执行的命令,例如: dir, git status, qmake"}
                }},
                {"working_directory", QJsonObject{
                    {"type", "string"},
                    {"description", "工作目录 (可选),例如: E:/Document/metagpt_qt-1"}
                }}
            }},
            {"required", QJsonArray{"command"}}
        };
        return tool;
    }
    
    // ==================== 工具实现 ====================
public:
    /**
     * @brief 执行 Shell 命令
     * @param command 要执行的命令
     * @param workingDir 工作目录（可选）
     * @return 命令输出结果，或错误信息
     * 
     * @note 安全检查已内置，危险命令会被自动拒绝
     */
    static QString executeCommand(const QString& command, 
                                  const QString& workingDir = "") {
        // NOTE: 安全检查内置，无法绕过
        if (!isSafeCommand(command)) {
            return "错误: 命令被安全策略拒绝 (包含危险操作或不在白名单中)";
        }
        
        QProcess process;
        
        // 设置工作目录
        if (!workingDir.isEmpty()) {
            process.setWorkingDirectory(workingDir);
        }
        
        // Windows 使用 Git Bash, Linux/Mac 使用 sh -c
        #ifdef Q_OS_WIN
            // 从环境变量或常见路径查找 Git Bash
            QString bashPath = findGitBash();
            if (!bashPath.isEmpty()) {
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
    
    /**
     * @brief 安全检查：白名单 + 黑名单双重机制
     * @param command 要检查的命令
     * @return true 如果命令安全，false 如果危险
     */
    static bool isSafeCommand(const QString& command) {
        QString lowerCmd = command.toLower().trimmed();
        
        // ========== 1. 黑名单检查（优先级最高）==========
        // 危险命令关键词（支持变种）
        static const QStringList dangerousPatterns = {
            // 删除类
            "rm -rf", "rm --recursive", "rm -r", "rmdir /s",
            "del /f", "del /s", "deltree",
            // 格式化/分区
            "format", "mkfs", "fdisk", "diskpart",
            // 系统控制
            "shutdown", "reboot", "halt", "poweroff",
            "init 0", "init 6",
            // 危险重定向
            "> /dev/", ">/dev/", "dd if=",
            // 权限提升
            "chmod 777", "chmod -R 777",
            // 网络危险操作
            "wget -O-", "curl -o-",
            // 注册表
            "reg delete", "regedit"
        };
        
        for (const QString& pattern : dangerousPatterns) {
            if (lowerCmd.contains(pattern.toLower())) {
                qDebug() << "[ShellTool] WARNING: 命令被黑名单拒绝:" << command;
                return false;
            }
        }
        
        // ========== 2. 白名单检查 ==========
        // 允许的命令前缀
        static const QStringList safeCommandPrefixes = {
            // 文件浏览
            "dir", "ls", "pwd", "cd", "tree",
            // 文件操作（只读）
            "cat", "type", "head", "tail", "more", "less",
            "find", "grep", "wc",
            // 文件信息
            "stat", "file", "du", "df",
            // 版本控制
            "git status", "git log", "git diff", "git branch",
            "git show", "git ls-files", "git remote",
            // 构建工具
            "qmake", "make", "nmake", "cmake", "msbuild",
            // 系统信息
            "echo", "date", "whoami", "hostname",
            "uname", "env", "set",
            // 网络诊断
            "ping", "tracert", "nslookup", "ipconfig", "ifconfig"
        };
        
        for (const QString& prefix : safeCommandPrefixes) {
            if (lowerCmd.startsWith(prefix.toLower())) {
                return true;
            }
        }
        
        // 默认拒绝不在白名单中的命令
        qDebug() << "[ShellTool] WARNING: 命令不在白名单中:" << command;
        return false;
    }
    
private:
    /**
     * @brief 查找 Git Bash 路径
     * @return Git Bash 可执行文件路径，或空字符串
     */
    static QString findGitBash() {
        // 常见安装路径
        static const QStringList possiblePaths = {
            "C:/Program Files/Git/bin/bash.exe",
            "C:/Program Files (x86)/Git/bin/bash.exe",
            "D:/Program Files/Git/bin/bash.exe",
            "D:/Git/bin/bash.exe"
        };
        
        for (const QString& path : possiblePaths) {
            if (QFile::exists(path)) {
                return path;
            }
        }
        
        // 尝试从 PATH 环境变量查找
        QString pathEnv = qgetenv("PATH");
        QStringList paths = pathEnv.split(";", Qt::SkipEmptyParts);
        for (const QString& dir : paths) {
            QString bashPath = dir + "/bash.exe";
            if (QFile::exists(bashPath)) {
                return bashPath;
            }
        }
        
        return QString();
    }
    
    /**
     * @brief 转换命令中的 MSYS 路径为 Windows 路径
     * @param command 包含 MSYS 路径的命令
     * @return 转换后的命令
     */
    static QString convertMsysPathInCommand(const QString& command) {
        QString result = command;
        
        // 使用 QRegularExpression 匹配所有 /盘符/ 格式的路径
        QRegularExpression msysPattern("(/([a-zA-Z])/)");
        QRegularExpressionMatchIterator it = msysPattern.globalMatch(result);
        
        // 从后向前替换，避免位置偏移
        QList<QPair<int, QString>> replacements;
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString driveLetter = match.captured(2).toUpper();
            replacements.prepend(qMakePair(match.capturedStart(), 
                                           QString("%1:/").arg(driveLetter)));
        }
        
        for (const auto& rep : replacements) {
            result.replace(rep.first, 3, rep.second);  // /x/ -> X:/
        }
        
        return result;
    }
};

#endif // SHELLTOOL_H
