#ifndef SHELLTOOL_H
#define SHELLTOOL_H

#include <QString>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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

    static QString policyFilePath()
    {
        return QDir::home().filePath(QStringLiteral(".tmagent/config/command_policy.json"));
    }

    static QJsonObject defaultPolicyObject()
    {
        const auto toArray = [](const QStringList& list) {
            QJsonArray arr;
            for (const QString& item : list)
                arr.append(item);
            return arr;
        };

        // 默认策略偏向“可用但可控”：
        // 1) 白名单允许常见开发命令（含 git clone/fetch/pull）
        // 2) 写命令仍默认限制在助手工作空间内
        // 3) 黑名单仍保留高危模式拦截
        QJsonObject obj;
        obj.insert(QStringLiteral("schema_version"), 1);
        obj.insert(QStringLiteral("allow_outside_workspace"), false);
        obj.insert(QStringLiteral("confirm_executable"), true);
        obj.insert(QStringLiteral("command_timeout_ms"), 30000);

        obj.insert(QStringLiteral("dangerous_patterns"), toArray(QStringList{
            // 删除类
            QStringLiteral("rm -rf"), QStringLiteral("rm --recursive"), QStringLiteral("rm -r"), QStringLiteral("rmdir /s"),
            QStringLiteral("del /f"), QStringLiteral("del /s"), QStringLiteral("deltree"),
            // 格式化/分区
            QStringLiteral("format"), QStringLiteral("mkfs"), QStringLiteral("fdisk"), QStringLiteral("diskpart"),
            // 系统控制
            QStringLiteral("shutdown"), QStringLiteral("reboot"), QStringLiteral("halt"), QStringLiteral("poweroff"),
            QStringLiteral("init 0"), QStringLiteral("init 6"),
            // 危险重定向
            QStringLiteral("> /dev/"), QStringLiteral(">/dev/"), QStringLiteral("dd if="),
            // 权限提升
            QStringLiteral("chmod 777"), QStringLiteral("chmod -R 777"),
            // 网络危险操作
            QStringLiteral("wget -O-"), QStringLiteral("curl -o-"),
            // 注册表
            QStringLiteral("reg delete"), QStringLiteral("regedit")
        }));

        obj.insert(QStringLiteral("safe_command_prefixes"), toArray(QStringList{
            // 文件浏览
            QStringLiteral("dir"), QStringLiteral("ls"), QStringLiteral("pwd"), QStringLiteral("cd"), QStringLiteral("tree"),
            QStringLiteral("which"), QStringLiteral("where"),
            // 文件操作（只读）
            QStringLiteral("cat"), QStringLiteral("type"), QStringLiteral("head"), QStringLiteral("tail"),
            QStringLiteral("more"), QStringLiteral("less"),
            QStringLiteral("find"), QStringLiteral("grep"), QStringLiteral("wc"),
            // 文件信息
            QStringLiteral("stat"), QStringLiteral("file"), QStringLiteral("du"), QStringLiteral("df"),
            // 版本控制
            QStringLiteral("git status"), QStringLiteral("git log"), QStringLiteral("git diff"), QStringLiteral("git branch"),
            QStringLiteral("git show"), QStringLiteral("git ls-files"), QStringLiteral("git remote"),
            QStringLiteral("git rev-parse"), QStringLiteral("git fetch"), QStringLiteral("git pull"),
            QStringLiteral("git clone"), QStringLiteral("git checkout"), QStringLiteral("git restore"),
            QStringLiteral("git submodule"),
            // 构建工具
            QStringLiteral("qmake"), QStringLiteral("make"), QStringLiteral("nmake"), QStringLiteral("cmake"), QStringLiteral("msbuild"),
            // 系统信息
            QStringLiteral("echo"), QStringLiteral("date"), QStringLiteral("whoami"), QStringLiteral("hostname"),
            QStringLiteral("uname"), QStringLiteral("env"), QStringLiteral("set"),
            // 网络诊断
            QStringLiteral("ping"), QStringLiteral("tracert"), QStringLiteral("nslookup"), QStringLiteral("ipconfig"), QStringLiteral("ifconfig"),
            // 常用网络请求/下载
            QStringLiteral("curl"), QStringLiteral("wget"),
            // Python 运行
            QStringLiteral("python "), QStringLiteral("python3 "), QStringLiteral("py "),
            QStringLiteral("pip "), QStringLiteral("pip3 "),
            // 进程/诊断命令
            QStringLiteral("ps"), QStringLiteral("ldd"), QStringLiteral("objdump"), QStringLiteral("nm"), QStringLiteral("readelf"),
            QStringLiteral("tasklist"), QStringLiteral("wmic process"),
            // 运行控制
            QStringLiteral("timeout"),
            // 可执行文件运行（本地路径）
            QStringLiteral("./"), QStringLiteral(".\\"),
            // Windows 驱动器路径（如 C:/, D:/, E:/ 等）
            QStringLiteral("a:/"), QStringLiteral("b:/"), QStringLiteral("c:/"), QStringLiteral("d:/"),
            QStringLiteral("e:/"), QStringLiteral("f:/"), QStringLiteral("g:/"), QStringLiteral("h:/")
        }));

        obj.insert(QStringLiteral("write_command_prefixes"), toArray(QStringList{
            // 文件创建/修改
            QStringLiteral("mkdir"), QStringLiteral("touch"), QStringLiteral("rm "), QStringLiteral("mv "), QStringLiteral("cp "),
            QStringLiteral("del "), QStringLiteral("copy "), QStringLiteral("xcopy "), QStringLiteral("move "), QStringLiteral("ren "),
            // 写入操作
            QStringLiteral("echo "), QStringLiteral("> "), QStringLiteral(">> "),
            // 执行脚本（视为写入，因为可能有副作用）
            QStringLiteral("./"), QStringLiteral(".\\"),
            // git 修改操作
            QStringLiteral("git add"), QStringLiteral("git commit"), QStringLiteral("git push"),
            QStringLiteral("git checkout"), QStringLiteral("git reset"), QStringLiteral("git revert"),
            QStringLiteral("git merge"), QStringLiteral("git rebase"), QStringLiteral("git pull"),
            QStringLiteral("git clone"), QStringLiteral("git init"), QStringLiteral("git fetch"),
            QStringLiteral("git submodule"),
            // 构建操作（会生成文件）
            QStringLiteral("make"), QStringLiteral("nmake"), QStringLiteral("cmake"), QStringLiteral("qmake"), QStringLiteral("msbuild")
        }));

        return obj;
    }

    static QJsonObject normalizePolicyObject(const QJsonObject& raw)
    {
        const auto normalizeStrArray = [](const QJsonValue& value, const QJsonArray& fallback) {
            if (!value.isArray())
                return fallback;
            QStringList items;
            const QJsonArray arr = value.toArray();
            for (const QJsonValue& v : arr) {
                const QString s = v.toString().trimmed();
                if (!s.isEmpty())
                    items.append(s);
            }
            items.removeDuplicates();
            QJsonArray out;
            for (const QString& s : items)
                out.append(s);
            return out;
        };

        QJsonObject out = defaultPolicyObject();
        out.insert(QStringLiteral("schema_version"), 1);

        if (raw.contains(QStringLiteral("allow_outside_workspace")))
            out.insert(QStringLiteral("allow_outside_workspace"),
                       raw.value(QStringLiteral("allow_outside_workspace")).toBool(false));

        if (raw.contains(QStringLiteral("confirm_executable")))
            out.insert(QStringLiteral("confirm_executable"),
                       raw.value(QStringLiteral("confirm_executable")).toBool(true));

        if (raw.contains(QStringLiteral("command_timeout_ms"))) {
            int timeout = raw.value(QStringLiteral("command_timeout_ms")).toInt(30000);
            timeout = qBound(1000, timeout, 300000);
            out.insert(QStringLiteral("command_timeout_ms"), timeout);
        }

        out.insert(QStringLiteral("dangerous_patterns"),
                   normalizeStrArray(raw.value(QStringLiteral("dangerous_patterns")),
                                     out.value(QStringLiteral("dangerous_patterns")).toArray()));
        out.insert(QStringLiteral("safe_command_prefixes"),
                   normalizeStrArray(raw.value(QStringLiteral("safe_command_prefixes")),
                                     out.value(QStringLiteral("safe_command_prefixes")).toArray()));
        out.insert(QStringLiteral("write_command_prefixes"),
                   normalizeStrArray(raw.value(QStringLiteral("write_command_prefixes")),
                                     out.value(QStringLiteral("write_command_prefixes")).toArray()));
        return out;
    }

    static QJsonObject loadPolicyObject()
    {
        const QString filePath = policyFilePath();
        QFile file(filePath);
        if (!file.exists()) {
            const QJsonObject defaults = defaultPolicyObject();
            savePolicyObject(defaults);
            return defaults;
        }
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            qWarning() << "[ShellTool] 无法读取命令策略配置，使用默认策略:" << filePath;
            return defaultPolicyObject();
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "[ShellTool] 命令策略配置格式错误，回退默认策略:" << filePath
                       << "error=" << parseError.errorString();
            const QJsonObject defaults = defaultPolicyObject();
            savePolicyObject(defaults);
            return defaults;
        }
        return normalizePolicyObject(doc.object());
    }

    static bool savePolicyObject(const QJsonObject& raw, QString* error = nullptr)
    {
        if (error)
            error->clear();
        const QString filePath = policyFilePath();
        if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
            if (error)
                *error = QStringLiteral("创建目录失败");
            return false;
        }
        QFile file(filePath);
        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            if (error)
                *error = QStringLiteral("打开文件失败");
            return false;
        }
        const QJsonObject normalized = normalizePolicyObject(raw);
        const QByteArray bytes = QJsonDocument(normalized).toJson(QJsonDocument::Indented);
        const bool ok = (file.write(bytes) == bytes.size());
        file.close();
        if (!ok && error)
            *error = QStringLiteral("写入文件失败");
        return ok;
    }
    
    // ==================== 工具执行入口（接收 JSON 参数） ====================
    
    /**
     * @brief 执行 execute_command 工具
     * @param input JSON 参数 {command, working_directory?}
     */
    static QString execute(const QJsonObject& input) {
        QString command = input["command"].toString();
        QString workspaceDir = resolveWorkspaceDir(input);
        QString workingDir = input.value("working_directory").toString().trimmed();
        if (workingDir.isEmpty()) {
            workingDir = workspaceDir;
        } else {
            workingDir = resolvePathUnderWorkspace(workingDir, workspaceDir);
            if (!isPathInsideWorkspace(workingDir, workspaceDir)) {
                return QString("错误: working_directory 必须位于当前助手工作空间内: %1")
                    .arg(workspaceDir);
            }
        }
        
        qDebug() << "[ShellTool] 执行命令:" << command;
        return executeCommand(command, workingDir, workspaceDir);
    }
    
    // ==================== 工具实现（核心函数） ====================
private:
    struct CommandPolicy {
        bool allowOutsideWorkspace = false;
        bool confirmExecutable = true;
        int commandTimeoutMs = 30000;
        QStringList safeCommandPrefixes;
        QStringList dangerousPatterns;
        QStringList writeCommandPrefixes;
    };

    static QStringList jsonArrayToStringList(const QJsonArray& arr)
    {
        QStringList items;
        for (const QJsonValue& v : arr) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty())
                items.append(s);
        }
        items.removeDuplicates();
        return items;
    }

    static CommandPolicy effectivePolicy()
    {
        const QJsonObject obj = loadPolicyObject();
        CommandPolicy policy;
        policy.allowOutsideWorkspace =
            obj.value(QStringLiteral("allow_outside_workspace")).toBool(false);
        policy.confirmExecutable =
            obj.value(QStringLiteral("confirm_executable")).toBool(true);
        policy.commandTimeoutMs =
            qBound(1000, obj.value(QStringLiteral("command_timeout_ms")).toInt(30000), 300000);
        policy.safeCommandPrefixes =
            jsonArrayToStringList(obj.value(QStringLiteral("safe_command_prefixes")).toArray());
        policy.dangerousPatterns =
            jsonArrayToStringList(obj.value(QStringLiteral("dangerous_patterns")).toArray());
        policy.writeCommandPrefixes =
            jsonArrayToStringList(obj.value(QStringLiteral("write_command_prefixes")).toArray());
        return policy;
    }

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
                                  const QString& workingDir = "",
                                  const QString& workspaceRoot = "") {
        const CommandPolicy policy = effectivePolicy();
        // NOTE: 安全检查内置，无法绕过
        if (!isSafeCommand(command, policy)) {
            return "错误: 命令被安全策略拒绝 (包含危险操作或不在白名单中)";
        }
        
        // 获取有效工作目录
        QString baseWorkDir = workspaceRoot.trimmed().isEmpty()
            ? QDir::currentPath()
            : QDir::cleanPath(convertMsysPath(workspaceRoot.trimmed()));
        if (!QDir().exists(baseWorkDir))
            QDir().mkpath(baseWorkDir);
        QString effectiveWorkDir = workingDir.isEmpty() ? baseWorkDir : workingDir;
        effectiveWorkDir = QDir::cleanPath(convertMsysPath(effectiveWorkDir));
        if (!isPathInsideWorkspace(effectiveWorkDir, baseWorkDir)) {
            return QString("错误: 工作目录越界，必须位于当前助手工作空间内: %1")
                .arg(baseWorkDir);
        }
        
        // 工具安全策略：默认写命令仅允许在助手工作空间内执行
        const bool allowOutsideWorkdir = policy.allowOutsideWorkspace;
        
        // NOTE: 写命令限制 - 只能在程序启动目录及其子目录内执行
        if (isWriteCommand(command, policy) && !allowOutsideWorkdir) {
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
        if (policy.confirmExecutable && isExecutableCommand(command)) {
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
        
        // 等待执行完成（默认 30s，可配置）
        if (!process.waitForFinished(policy.commandTimeoutMs)) {
            process.kill();
            process.waitForFinished(1000);
            return QString("错误: 命令执行超时 (%1 ms)").arg(policy.commandTimeoutMs);
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
    static bool isWriteCommand(const QString& command, const CommandPolicy& policy) {
        QString lowerCmd = command.toLower().trimmed();
        for (const QString& prefix : policy.writeCommandPrefixes) {
            if (lowerCmd.startsWith(prefix.toLower())) {
                return true;
            }
        }
        
        // 检查复合命令中是否有写入操作
        for (const QString& subCmd : splitSubCommands(lowerCmd)) {
            QString trimmedSubCmd = subCmd.trimmed();
            if (trimmedSubCmd.isEmpty()) continue;

            for (const QString& prefix : policy.writeCommandPrefixes) {
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
        for (const QString& subCmd : splitSubCommands(lowerCmd)) {
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
    static bool isSafeCommand(const QString& command, const CommandPolicy& policy) {
        QString lowerCmd = command.toLower().trimmed();

        // ========== 1. 黑名单检查（优先级最高）==========
        for (const QString& pattern : policy.dangerousPatterns) {
            if (lowerCmd.contains(pattern.toLower())) {
                qDebug() << "[ShellTool] WARNING: 命令被黑名单拒绝:" << command;
                return false;
            }
        }

        // ========== 2. 白名单检查 ==========
        // 处理复合命令：用 && 和 || 分隔，检查每个子命令
        for (const QString& subCmd : splitSubCommands(lowerCmd)) {
            QString trimmedSubCmd = subCmd.trimmed();
            if (trimmedSubCmd.isEmpty()) continue;
            
            bool subCmdSafe = (trimmedSubCmd == "python" ||
                               trimmedSubCmd == "python3" ||
                               trimmedSubCmd == "py" ||
                               trimmedSubCmd == "pip" ||
                               trimmedSubCmd == "pip3");
            for (const QString& prefix : policy.safeCommandPrefixes) {
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
    static QString convertMsysPath(const QString& path) {
        QRegularExpression msysPattern("^/([a-zA-Z])/(.*)$");
        QRegularExpressionMatch match = msysPattern.match(path);
        if (match.hasMatch()) {
            QString driveLetter = match.captured(1).toUpper();
            QString restPath = match.captured(2);
            return QString("%1:/%2").arg(driveLetter, restPath);
        }
        return path;
    }

    static QString resolveWorkspaceDir(const QJsonObject& input) {
        QString workspace = input.value("_agent_workspace").toString().trimmed();
        if (workspace.isEmpty())
            workspace = QDir::currentPath();
        workspace = QDir::cleanPath(convertMsysPath(workspace));
        if (!QDir().exists(workspace))
            QDir().mkpath(workspace);
        return workspace;
    }

    static QString resolvePathUnderWorkspace(const QString& path, const QString& workspaceDir) {
        const QString normalized = convertMsysPath(path.trimmed());
        if (normalized.isEmpty())
            return workspaceDir;

        QFileInfo info(normalized);
        if (info.isAbsolute())
            return QDir::cleanPath(normalized);
        return QDir::cleanPath(QDir(workspaceDir).absoluteFilePath(normalized));
    }

    static bool isPathInsideWorkspace(const QString& targetPath, const QString& workspaceDir) {
        const QString workspaceCanonical = QFileInfo(workspaceDir).canonicalFilePath();
        const QString workspaceAbs = workspaceCanonical.isEmpty()
            ? QDir(workspaceDir).absolutePath()
            : workspaceCanonical;

        const QString targetCanonical = QFileInfo(targetPath).canonicalFilePath();
        const QString targetAbs = targetCanonical.isEmpty()
            ? QDir(targetPath).absolutePath()
            : targetCanonical;

        if (targetAbs == workspaceAbs)
            return true;
        return targetAbs.startsWith(workspaceAbs + QDir::separator());
    }

    static QStringList splitSubCommands(const QString& lowerCmd) {
        return lowerCmd.split(QRegularExpression("\\s*&&\\s*|\\s*\\|\\|\\s*"));
    }

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
