#include "ShellTool.h"
#include <tmagent/support/ToolSchemaBuilder.h>

#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

// ==================== 确认回调（静态存储） ====================
static ShellTool::ConfirmCallback s_confirmCallback = nullptr;

TmAgent::Tool ShellTool::toolSchema()
{
    TmAgent::Tool tool;
    tool.name = QString::fromLatin1(EXECUTE_COMMAND);
    tool.description = QStringLiteral("执行终端命令并返回结果。");
    tool.inputSchema = TmAgent::makeToolSchema(
        tool.name,
        tool.description,
        QJsonObject {
            { QStringLiteral("command"), TmAgent::makePropertySchema(QStringLiteral("string"), QStringLiteral("要执行的命令，例如: dir, git status, qmake")) },
            { QStringLiteral("working_directory"), TmAgent::makePropertySchema(QStringLiteral("string"), QStringLiteral("工作目录（可选）")) }
        },
        QStringList { QStringLiteral("command") });
    return tool;
}

void ShellTool::setConfirmCallback(ConfirmCallback callback)
{
    s_confirmCallback = std::move(callback);
}

QString ShellTool::policyFilePath()
{
    return QDir::home().filePath(QStringLiteral(".tmagent/config/command_policy.json"));
}

QJsonObject ShellTool::defaultPolicyObject()
{
    const auto toArray = [](const QStringList& list) {
        QJsonArray arr;
        for (const QString& item : list)
            arr.append(item);
        return arr;
    };

    // 默认策略偏向"可用但可控"：
    // 1) 白名单允许常见开发命令（含 git clone/fetch/pull）
    // 2) 写命令仍默认限制在助手工作空间内
    // 3) 黑名单仍保留高危模式拦截
    QJsonObject obj;
    obj.insert(QStringLiteral("schema_version"), 3);
    obj.insert(QStringLiteral("allow_outside_workspace"), false);
    obj.insert(QStringLiteral("confirm_executable"), true);
    // 默认采用"黑名单优先"模式：仅拦截危险模式，不强制白名单。
    // 如需严格控制，可在配置里将该项改为 true。
    obj.insert(QStringLiteral("enforce_safe_prefixes"), false);
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
        // 危险重定向/磁盘写入
        QStringLiteral("> /dev/sd"), QStringLiteral(">/dev/sd"),
        QStringLiteral("\\\\.\\physicaldrive"),
        QStringLiteral("dd if="),
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

QJsonObject ShellTool::normalizePolicyObject(const QJsonObject& raw)
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
    out.insert(QStringLiteral("schema_version"), 3);

    if (raw.contains(QStringLiteral("allow_outside_workspace")))
        out.insert(QStringLiteral("allow_outside_workspace"),
                   raw.value(QStringLiteral("allow_outside_workspace")).toBool(false));

    if (raw.contains(QStringLiteral("confirm_executable")))
        out.insert(QStringLiteral("confirm_executable"),
                   raw.value(QStringLiteral("confirm_executable")).toBool(true));
    if (raw.contains(QStringLiteral("enforce_safe_prefixes")))
        out.insert(QStringLiteral("enforce_safe_prefixes"),
                   raw.value(QStringLiteral("enforce_safe_prefixes")).toBool(false));

    if (raw.contains(QStringLiteral("command_timeout_ms"))) {
        int timeout = raw.value(QStringLiteral("command_timeout_ms")).toInt(30000);
        timeout = qBound(1000, timeout, 300000);
        out.insert(QStringLiteral("command_timeout_ms"), timeout);
    }

    QJsonArray dangerousPatterns =
        normalizeStrArray(raw.value(QStringLiteral("dangerous_patterns")),
                          out.value(QStringLiteral("dangerous_patterns")).toArray());
    // 迁移旧版误伤规则：允许 2>/dev/null 等常见无害重定向，不再用 "> /dev/" 全局拦截。
    QJsonArray sanitizedDangerousPatterns;
    QSet<QString> seenLower;
    for (const QJsonValue& v : dangerousPatterns) {
        const QString s = v.toString().trimmed();
        const QString lower = s.toLower();
        if (lower.isEmpty())
            continue;
        if (lower == QStringLiteral("> /dev/") || lower == QStringLiteral(">/dev/"))
            continue;
        if (seenLower.contains(lower))
            continue;
        seenLower.insert(lower);
        sanitizedDangerousPatterns.append(s);
    }
    out.insert(QStringLiteral("dangerous_patterns"), sanitizedDangerousPatterns);
    out.insert(QStringLiteral("safe_command_prefixes"),
               normalizeStrArray(raw.value(QStringLiteral("safe_command_prefixes")),
                                 out.value(QStringLiteral("safe_command_prefixes")).toArray()));
    out.insert(QStringLiteral("write_command_prefixes"),
               normalizeStrArray(raw.value(QStringLiteral("write_command_prefixes")),
                                 out.value(QStringLiteral("write_command_prefixes")).toArray()));
    return out;
}

QJsonObject ShellTool::loadPolicyObject()
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
    const QJsonObject normalized = normalizePolicyObject(doc.object());
    if (doc.object() != normalized)
        savePolicyObject(normalized);
    return normalized;
}

bool ShellTool::savePolicyObject(const QJsonObject& raw, QString* error)
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

QString ShellTool::execute(const QJsonObject& input) {
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

QStringList ShellTool::jsonArrayToStringList(const QJsonArray& arr)
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

ShellTool::CommandPolicy ShellTool::effectivePolicy()
{
    const QJsonObject obj = loadPolicyObject();
    CommandPolicy policy;
    policy.allowOutsideWorkspace =
        obj.value(QStringLiteral("allow_outside_workspace")).toBool(false);
    policy.confirmExecutable =
        obj.value(QStringLiteral("confirm_executable")).toBool(true);
    policy.enforceSafePrefixes =
        obj.value(QStringLiteral("enforce_safe_prefixes")).toBool(false);
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

QString ShellTool::executeCommand(const QString& command,
                                  const QString& workingDir,
                                  const QString& workspaceRoot) {
    const CommandPolicy policy = effectivePolicy();
    // NOTE: 安全检查内置，无法绕过
    if (!isSafeCommand(command, policy)) {
        return "错误: 命令被安全策略拒绝 (触发黑名单或命令校验失败)";
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
        bool allowed = false;
        if (s_confirmCallback) {
            allowed = s_confirmCallback(command, effectiveWorkDir);
        } else {
            qWarning() << "[ShellTool] 未设置确认回调，默认拒绝执行:" << command;
        }

        if (!allowed) {
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

bool ShellTool::isWriteCommand(const QString& command, const CommandPolicy& policy) {
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

bool ShellTool::isWriteCommand(const QString& command)
{
    return isWriteCommand(command, effectivePolicy());
}

bool ShellTool::isExecutableCommand(const QString& command) {
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

bool ShellTool::isSafeCommand(const QString& command)
{
    return isSafeCommand(command, effectivePolicy());
}

bool ShellTool::isSafeCommand(const QString& command, const CommandPolicy& policy) {
    QString lowerCmd = command.toLower().trimmed();

    // ========== 1. 黑名单检查（优先级最高）==========
    for (const QString& pattern : policy.dangerousPatterns) {
        if (lowerCmd.contains(pattern.toLower())) {
            qDebug() << "[ShellTool] WARNING: 命令被黑名单拒绝:" << command;
            return false;
        }
    }

    // ========== 2. 可选白名单检查 ==========
    if (!policy.enforceSafePrefixes) {
        return true;
    }

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

QString ShellTool::convertMsysPath(const QString& path) {
    QRegularExpression msysPattern("^/([a-zA-Z])/(.*)$");
    QRegularExpressionMatch match = msysPattern.match(path);
    if (match.hasMatch()) {
        QString driveLetter = match.captured(1).toUpper();
        QString restPath = match.captured(2);
        return QString("%1:/%2").arg(driveLetter, restPath);
    }
    return path;
}

QString ShellTool::resolveWorkspaceDir(const QJsonObject& input) {
    QString workspace = input.value("_agent_workspace").toString().trimmed();
    if (workspace.isEmpty())
        workspace = QDir::currentPath();
    workspace = QDir::cleanPath(convertMsysPath(workspace));
    if (!QDir().exists(workspace))
        QDir().mkpath(workspace);
    return workspace;
}

QString ShellTool::resolvePathUnderWorkspace(const QString& path, const QString& workspaceDir) {
    const QString normalized = convertMsysPath(path.trimmed());
    if (normalized.isEmpty())
        return workspaceDir;

    QFileInfo info(normalized);
    if (info.isAbsolute())
        return QDir::cleanPath(normalized);
    return QDir::cleanPath(QDir(workspaceDir).absoluteFilePath(normalized));
}

bool ShellTool::isPathInsideWorkspace(const QString& targetPath, const QString& workspaceDir) {
    const QString workspaceCanonical = QFileInfo(workspaceDir).canonicalFilePath();
    const QString workspaceAbs = workspaceCanonical.isEmpty()
        ? QDir(workspaceDir).absolutePath()
        : workspaceCanonical;

    const QString targetCanonical = QFileInfo(targetPath).canonicalFilePath();
    const QString targetAbs = targetCanonical.isEmpty()
        ? QFileInfo(targetPath).absoluteFilePath()
        : targetCanonical;

    QString normalizedWorkspace = QDir::cleanPath(QDir::fromNativeSeparators(workspaceAbs));
    QString normalizedTarget = QDir::cleanPath(QDir::fromNativeSeparators(targetAbs));
#ifdef Q_OS_WIN
    normalizedWorkspace = normalizedWorkspace.toLower();
    normalizedTarget = normalizedTarget.toLower();
#endif

    if (normalizedTarget == normalizedWorkspace)
        return true;
    const QString prefix = normalizedWorkspace.endsWith(QLatin1Char('/'))
        ? normalizedWorkspace
        : (normalizedWorkspace + QLatin1Char('/'));
    return normalizedTarget.startsWith(prefix);
}

QStringList ShellTool::splitSubCommands(const QString& lowerCmd) {
    return lowerCmd.split(QRegularExpression("\\s*&&\\s*|\\s*\\|\\|\\s*"));
}

QString ShellTool::findGitBash() {
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

QString ShellTool::convertMsysPathInCommand(const QString& command) {
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
