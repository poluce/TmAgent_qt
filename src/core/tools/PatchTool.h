#ifndef PATCHTOOL_H
#define PATCHTOOL_H

#include "core/agent/ToolTypes.h"
#include <QJsonObject>
#include <QString>

/**
 * @brief 补丁工具 (对标 opencode 的 apply_patch)
 *
 * 解析 *** Begin Patch 格式并应用文件修改。
 */
class PatchTool {
public:
    static constexpr const char* APPLY_PATCH = "apply_patch";
    static Tool toolSchema();

    /**
     * @brief 应用补丁
     * @param input {patchText}
     */
    static QString execute(const QJsonObject& input);

private:
    static QString resolveWorkspaceDir(const QJsonObject& input);
    static QString resolvePathUnderWorkspace(const QString& path, const QString& workspaceDir);
    static bool isPathInsideWorkspace(const QString& targetPath, const QString& workspaceDir);
    static QString ensurePathUnderWorkspace(const QString& path, const QString& workspaceDir, const QString& fieldName);
    static bool writeFile(const QString& path, const QString& content);
};

#endif // PATCHTOOL_H
