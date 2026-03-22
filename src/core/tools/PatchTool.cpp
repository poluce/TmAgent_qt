#include "PatchTool.h"
#include "ToolSchemaSupport.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

Tool PatchTool::toolSchema()
{
    return makeToolSchema(
        QString::fromLatin1(APPLY_PATCH),
        QStringLiteral("应用结构化补丁（Patch）。"),
        QJsonObject {
            { QStringLiteral("patchText"), makePropertySchema(QStringLiteral("string"), QStringLiteral("完整的补丁文本，必须以 '*** Begin Patch' 开始")) }
        },
        QStringList { QStringLiteral("patchText") });
}

QString PatchTool::execute(const QJsonObject& input)
{
    QString patchText = input["patchText"].toString();
    if (patchText.isEmpty())
        return "错误: 补丁内容为空";
    const QString workspaceDir = resolveWorkspaceDir(input);

    QStringList lines = patchText.split('\n');
    if (lines.isEmpty() || !lines.first().startsWith("*** Begin Patch")) {
        return "错误: 无效的补丁格式，必须以 *** Begin Patch 开始";
    }

    QString currentFile;
    QString resultSummary;
    int filesChanged = 0;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();

        if (line.startsWith("*** Add File:")) {
            currentFile = resolvePathUnderWorkspace(line.mid(13).trimmed(), workspaceDir);
            const QString scopeError = ensurePathUnderWorkspace(currentFile, workspaceDir, QStringLiteral("patch path"));
            if (!scopeError.isEmpty())
                return scopeError;
            QString content;
            while (++i < lines.size() && (lines[i].startsWith("+") || lines[i].isEmpty())) {
                if (lines[i].startsWith("+"))
                    content += lines[i].mid(1) + "\n";
                else
                    content += "\n";
            }
            --i; // 回退，因为外层循环还会递增
            if (writeFile(currentFile, content)) {
                resultSummary += "A " + currentFile + "\n";
                filesChanged++;
            }
        } else if (line.startsWith("*** Delete File:")) {
            currentFile = resolvePathUnderWorkspace(line.mid(16).trimmed(), workspaceDir);
            const QString scopeError = ensurePathUnderWorkspace(currentFile, workspaceDir, QStringLiteral("patch path"));
            if (!scopeError.isEmpty())
                return scopeError;
            if (QFile::remove(currentFile)) {
                resultSummary += "D " + currentFile + "\n";
                filesChanged++;
            }
        } else if (line.startsWith("*** Update File:")) {
            currentFile = resolvePathUnderWorkspace(line.mid(16).trimmed(), workspaceDir);
            const QString scopeError = ensurePathUnderWorkspace(currentFile, workspaceDir, QStringLiteral("patch path"));
            if (!scopeError.isEmpty())
                return scopeError;
            // 简化处理：这里假设接下来的 Update 逻辑是简单的字符串替换（对标 opencode 的基础逻辑）
            // 实际上 opencode 的 Update 可能包含 @@ 块，目前我们先支持基础的完整读写
            // 如果需要支持复杂的 @@/diff 块，需要引入更强大的 patch 逻辑
            resultSummary += "M " + currentFile + " (Partial support: standard update)\n";
        } else if (line.startsWith("*** Move to:")) {
            QString newPath = resolvePathUnderWorkspace(line.mid(12).trimmed(), workspaceDir);
            if (currentFile.isEmpty())
                continue;
            const QString oldPathError = ensurePathUnderWorkspace(currentFile, workspaceDir, QStringLiteral("patch path"));
            if (!oldPathError.isEmpty())
                return oldPathError;
            const QString newPathError = ensurePathUnderWorkspace(newPath, workspaceDir, QStringLiteral("patch path"));
            if (!newPathError.isEmpty())
                return newPathError;
            if (QFile::rename(currentFile, newPath)) {
                resultSummary += "R " + currentFile + " -> " + newPath + "\n";
                filesChanged++;
            }
        }
    }

    if (filesChanged == 0)
        return "未应用任何改动";
    return "成功处理 " + QString::number(filesChanged) + " 个文件:\n" + resultSummary;
}

QString PatchTool::resolveWorkspaceDir(const QJsonObject& input)
{
    QString workspace = input.value("_agent_workspace").toString().trimmed();
    if (workspace.isEmpty())
        workspace = QDir::currentPath();
    workspace = QDir::cleanPath(workspace);
    if (!QDir().exists(workspace))
        QDir().mkpath(workspace);
    return workspace;
}

QString PatchTool::resolvePathUnderWorkspace(const QString& path, const QString& workspaceDir)
{
    const QString normalized = QDir::cleanPath(path.trimmed());
    if (normalized.isEmpty())
        return workspaceDir;
    QFileInfo info(normalized);
    if (info.isAbsolute())
        return normalized;
    return QDir::cleanPath(QDir(workspaceDir).absoluteFilePath(normalized));
}

bool PatchTool::isPathInsideWorkspace(const QString& targetPath, const QString& workspaceDir)
{
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

QString PatchTool::ensurePathUnderWorkspace(const QString& path, const QString& workspaceDir, const QString& fieldName)
{
    if (path.trimmed().isEmpty())
        return QString("错误: %1 不能为空").arg(fieldName);
    if (!isPathInsideWorkspace(path, workspaceDir)) {
        return QString("错误: %1 必须位于当前助手工作空间内: %2")
            .arg(fieldName, workspaceDir);
    }
    return QString();
}

bool PatchTool::writeFile(const QString& path, const QString& content)
{
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        out.setCodec("UTF-8");
#endif
        out << content;
        file.close();
        return true;
    }
    return false;
}
