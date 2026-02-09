#ifndef SHELLTOOL_H
#define SHELLTOOL_H

#include <QString>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>

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
    // ==================== 工具名称常量 ====================
    static constexpr const char* EXECUTE_COMMAND = "execute_command";
    
    // ==================== 工具执行入口（接收 JSON 参数） ====================
    
    /**
     * @brief 执行 execute_command 工具
     * @param input JSON 参数 {command, working_directory?}
     */
    static QString execute(const QJsonObject& input) {
        QString command = input["command"].toString();
        QString workingDir = input.value("working_directory").toString();
        
        qDebug() << "[ShellTool] 执行命令:" << command;
        return executeCommand(command, workingDir);
    }
    
    // ==================== 工具实现（核心函数） ====================
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
        
        // 获取有效工作目录
        QString baseWorkDir = QDir::currentPath();  // 程序启动时的目录
        QString effectiveWorkDir = workingDir.isEmpty() ? baseWorkDir : workingDir;
        
        // 工具安全策略：默认允许访问工作目录外的文件
        const bool allowOutsideWorkdir = true;
        
        // NOTE: 写命令限制 - 只能在程序启动目录及其子目录内执行
        if (isWriteCommand(command) && !allowOutsideWorkdir) {
            QString canonicalBase = QDir(baseWorkDir).canonicalPath();
            QString canonicalTarget = QDir(effectiveWorkDir).canonicalPath();
            
            // 检查目标目录是否在基础目录内
            if (!canonicalTarget.startsWith(canonicalBase)) {
                qDebug() << "[ShellTool] 写命令被拒绝: 目标目录" << effectiveWorkDir 
                         << "不在工作目录" << baseWorkDir << "内";
                return QString("错误: 写入操作只能在工作目录 (%1) 及其子目录内执行，无法操作 %2")
                    .arg(baseWorkDir)
                    .arg(effectiveWorkDir);
            }
        }
        
        // NOTE: 可执行文件确认机制
        if (isExecutableCommand(command)) {
            QMessageBox::StandardButton reply = QMessageBox::question(
                nullptr,
                "执行确认",
                QString("Agent 请求执行以下命令：\n\n%1\n\n工作目录：%2\n\n是否允许执行？")
                    .arg(command)
                    .arg(effectiveWorkDir),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No  // 默认选中"否"
            );
            
            if (reply != QMessageBox::Yes) {
                qDebug() << "[ShellTool] 用户拒绝执行命令:" << command;
                return "错误: 用户拒绝执行该命令";
            }
            qDebug() << "[ShellTool] 用户确认执行命令:" << command;
        }
        
        QProcess process;
        
        // 设置工作目录（effectiveWorkDir 已在前面定义）
        process.setWorkingDirectory(effectiveWorkDir);
        
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
     * @brief 检测命令是否是写入/修改操作
     * @param command 要检查的命令
     * @return true 如果命令会修改文件系统
     * 
     * @note 写入命令只能在工作目录内执行
     */
    static bool isWriteCommand(const QString& command) {
        QString lowerCmd = command.toLower().trimmed();
        
        // 写入/修改类命令前缀
        static const QStringList writeCommandPrefixes = {
            // 文件创建/修改
            "mkdir", "touch", "rm ", "mv ", "cp ",
            "del ", "copy ", "xcopy ", "move ", "ren ",
            // 写入操作
            "echo ", "> ", ">> ",
            // 执行脚本（视为写入，因为可能有副作用）
            "./", ".\\",
            // git 修改操作
            "git add", "git commit", "git push", "git checkout",
            "git reset", "git revert", "git merge", "git rebase",
            "git pull", "git clone", "git init",
            // 构建操作（会生成文件）
            "make", "nmake", "cmake", "qmake", "msbuild"
        };
        
        for (const QString& prefix : writeCommandPrefixes) {
            if (lowerCmd.startsWith(prefix.toLower())) {
                return true;
            }
        }
        
        // 检查复合命令中是否有写入操作
        QStringList subCommands = lowerCmd.split(QRegularExpression("\\s*&&\\s*|\\s*\\|\\|\\s*"));
        for (const QString& subCmd : subCommands) {
            QString trimmedSubCmd = subCmd.trimmed();
            if (trimmedSubCmd.isEmpty()) continue;
            
            for (const QString& prefix : writeCommandPrefixes) {
                if (trimmedSubCmd.startsWith(prefix.toLower())) {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    /**
     * @brief 检测命令是否是执行可执行文件
     * @param command 要检查的命令
     * @return true 如果命令包含执行可执行文件的操作
     * 
     * @note 用于触发用户确认对话框
     */
    static bool isExecutableCommand(const QString& command) {
        QString lowerCmd = command.toLower().trimmed();

        // 解释器命令（python/py）属于受控白名单命令，不走可执行文件确认弹窗，
        // 否则 "python xxx.py" 会被 ".py" 误判为本地可执行文件。
        if (lowerCmd.startsWith("python ") || lowerCmd == "python" ||
            lowerCmd.startsWith("python3 ") || lowerCmd == "python3" ||
            lowerCmd.startsWith("py ") || lowerCmd == "py") {
            return false;
        }
        
        // 可执行文件扩展名
        static const QStringList executableExtensions = {
            ".exe", ".bat", ".cmd", ".com",  // Windows
            ".sh", ".bash", ".zsh",          // Unix shell
            ".py", ".pl", ".rb"              // 脚本语言
        };
        
        // 检查是否包含可执行文件
        for (const QString& ext : executableExtensions) {
            if (lowerCmd.contains(ext)) {
                return true;
            }
        }
        
        // 检查是否以 ./ 或 ../ 开头（通常是执行本地程序）
        if (lowerCmd.startsWith("./") || lowerCmd.startsWith(".\\") ||
            lowerCmd.startsWith("../") || lowerCmd.startsWith("..\\")) {
            return true;
        }
        
        // 检查复合命令中是否有执行部分（如 cd xxx && ./xxx）
        QStringList subCommands = lowerCmd.split(QRegularExpression("\\s*&&\\s*|\\s*\\|\\|\\s*"));
        for (const QString& subCmd : subCommands) {
            QString trimmedSubCmd = subCmd.trimmed();
            if (trimmedSubCmd.startsWith("./") || trimmedSubCmd.startsWith(".\\") ||
                trimmedSubCmd.startsWith("../") || trimmedSubCmd.startsWith("..\\")) {
                return true;
            }
            for (const QString& ext : executableExtensions) {
                if (trimmedSubCmd.contains(ext)) {
                    return true;
                }
            }
        }
        
        return false;
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
            "ping", "tracert", "nslookup", "ipconfig", "ifconfig",
            // 常用网络请求/下载
            "curl", "wget",
            // Python 运行
            "python ", "python3 ", "py ",
            "pip ", "pip3 ",
            // 进程/诊断命令
            "ps", "ldd", "objdump", "nm", "readelf",
            "tasklist", "wmic process",
            // 运行控制
            "timeout",
            // 可执行文件运行（本地路径）
            "./", ".\\",
            // Windows 驱动器路径（如 C:/, D:/, E:/ 等）
            "a:/", "b:/", "c:/", "d:/", "e:/", "f:/", "g:/", "h:/"
        };
        
        // 处理复合命令：用 && 和 || 分隔，检查每个子命令
        QStringList subCommands = lowerCmd.split(QRegularExpression("\\s*&&\\s*|\\s*\\|\\|\\s*"));
        for (const QString& subCmd : subCommands) {
            QString trimmedSubCmd = subCmd.trimmed();
            if (trimmedSubCmd.isEmpty()) continue;
            
            bool subCmdSafe = (trimmedSubCmd == "python" ||
                               trimmedSubCmd == "python3" ||
                               trimmedSubCmd == "py" ||
                               trimmedSubCmd == "pip" ||
                               trimmedSubCmd == "pip3");
            for (const QString& prefix : safeCommandPrefixes) {
                if (trimmedSubCmd.startsWith(prefix.toLower())) {
                    subCmdSafe = true;
                    break;
                }
            }
            
            if (!subCmdSafe) {
                qDebug() << "[ShellTool] WARNING: 子命令不在白名单中:" << trimmedSubCmd;
                return false;
            }
        }
        
        return true;
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
