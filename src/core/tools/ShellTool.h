#ifndef SHELLTOOL_H
#define SHELLTOOL_H

#include "core/agent/ToolTypes.h"
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <functional>

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
    static Tool toolSchema();

    // ==================== 确认回调 ====================
    // 参数: (command, workingDir) → 返回 true 表示允许执行
    using ConfirmCallback = std::function<bool(const QString& command, const QString& workDir)>;
    static void setConfirmCallback(ConfirmCallback callback);

    // ==================== 内部类型 ====================

    struct CommandPolicy {
        bool allowOutsideWorkspace = false;
        bool confirmExecutable = true;
        bool enforceSafePrefixes = false;
        int commandTimeoutMs = 30000;
        QStringList safeCommandPrefixes;
        QStringList dangerousPatterns;
        QStringList writeCommandPrefixes;
    };

    // ==================== 策略管理 ====================

    static QString policyFilePath();
    static QJsonObject defaultPolicyObject();
    static QJsonObject normalizePolicyObject(const QJsonObject& raw);
    static QJsonObject loadPolicyObject();
    static bool savePolicyObject(const QJsonObject& raw, QString* error = nullptr);

    // ==================== 工具执行入口（接收 JSON 参数） ====================

    /**
     * @brief 执行 execute_command 工具
     * @param input JSON 参数 {command, working_directory?}
     */
    static QString execute(const QJsonObject& input);

    /**
     * @brief 执行 Shell 命令
     * @param command 要执行的命令
     * @param workingDir 工作目录（可选）
     * @param workspaceRoot 工作空间根目录（可选）
     * @return 命令输出结果，或错误信息
     *
     * @note 安全检查已内置，危险命令会被自动拒绝
     */
    static QString executeCommand(const QString& command,
                                  const QString& workingDir = "",
                                  const QString& workspaceRoot = "");

    /**
     * @brief 检测命令是否是写入/修改操作
     * @param command 要检查的命令
     * @param policy 命令策略
     * @return true 如果命令会修改文件系统
     *
     * @note 写入命令只能在工作目录内执行
     */
    static bool isWriteCommand(const QString& command, const CommandPolicy& policy);

    /**
     * @brief 检测命令是否是执行可执行文件
     * @param command 要检查的命令
     * @return true 如果命令包含执行可执行文件的操作
     *
     * @note 用于触发用户确认对话框
     */
    static bool isExecutableCommand(const QString& command);

    /**
     * @brief 安全检查：黑名单优先；可选启用白名单前缀约束
     * @param command 要检查的命令
     * @param policy 命令策略
     * @return true 如果命令安全，false 如果危险
     */
    static bool isSafeCommand(const QString& command, const CommandPolicy& policy);

private:
    static QStringList jsonArrayToStringList(const QJsonArray& arr);
    static CommandPolicy effectivePolicy();
    static QString convertMsysPath(const QString& path);
    static QString resolveWorkspaceDir(const QJsonObject& input);
    static QString resolvePathUnderWorkspace(const QString& path, const QString& workspaceDir);
    static bool isPathInsideWorkspace(const QString& targetPath, const QString& workspaceDir);
    static QStringList splitSubCommands(const QString& lowerCmd);
    static QString findGitBash();
    static QString convertMsysPathInCommand(const QString& command);
};

#endif // SHELLTOOL_H
