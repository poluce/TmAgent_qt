#include "FileTool.h"
#include "ToolSchemaSupport.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>

QList<Tool> FileTool::toolSchemas()
{
    QList<Tool> tools;

    tools.append(makeToolSchema(
        QString::fromLatin1(CREATE_FILE),
        QStringLiteral("在指定目录创建一个文本文件"),
        QJsonObject {
            { QStringLiteral("directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("目标目录路径，例如: E:/test")) },
            { QStringLiteral("filename"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件名，例如: hello.txt")) },
            { QStringLiteral("content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件内容；未指定则创建空文件")) }
        },
        QStringList { QStringLiteral("directory"), QStringLiteral("filename") }));

    tools.append(makeToolSchema(
        QString::fromLatin1(VIEW_FILE),
        QStringLiteral("读取文件的完整内容。返回文件路径、大小、行数和完整内容。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要读取的文件绝对路径")) }
        },
        QStringList { QStringLiteral("file_path") }));

    tools.append(makeToolSchema(
        QString::fromLatin1(READ_FILE_LINES),
        QStringLiteral("读取文件的指定行范围。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要读取的文件绝对路径")) },
            { QStringLiteral("start_line"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("起始行号（从 1 开始）")) },
            { QStringLiteral("end_line"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("结束行号（包含该行）")) }
        },
        QStringList { QStringLiteral("file_path"), QStringLiteral("start_line"), QStringLiteral("end_line") }));

    tools.append(makeToolSchema(
        QString::fromLatin1(REPLACE_IN_FILE),
        QStringLiteral("替换文件中的指定内容。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要修改的文件绝对路径")) },
            { QStringLiteral("target_content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要被替换的原始内容（精确匹配）")) },
            { QStringLiteral("replacement_content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("替换后的新内容")) }
        },
        QStringList { QStringLiteral("file_path"), QStringLiteral("target_content"), QStringLiteral("replacement_content") }));

    tools.append(makeToolSchema(
        QString::fromLatin1(DELETE_FILE),
        QStringLiteral("删除指定文件。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要删除的文件绝对路径")) }
        },
        QStringList { QStringLiteral("file_path") }));

    tools.append(makeToolSchema(
        QString::fromLatin1(LIST_DIRECTORY),
        QStringLiteral("列出目录中的文件和子目录。"),
        QJsonObject {
            { QStringLiteral("directory_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要列出的目录绝对路径")) },
            { QStringLiteral("recursive"), makePropertySchema(QStringLiteral("boolean"), QStringLiteral("是否递归列出子目录（默认 false）")) }
        },
        QStringList { QStringLiteral("directory_path") }));

    tools.append(makeToolSchema(
        QString::fromLatin1(GREP_SEARCH),
        QStringLiteral("在文件内容中搜索包含指定文本的行。"),
        QJsonObject {
            { QStringLiteral("pattern"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要搜索的文本或正则表达式")) },
            { QStringLiteral("directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("搜索目录路径")) },
            { QStringLiteral("file_pattern"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件名模式（如 *.cpp）")) }
        },
        QStringList { QStringLiteral("pattern"), QStringLiteral("directory") }));

    tools.append(makeToolSchema(
        QString::fromLatin1(FIND_BY_NAME),
        QStringLiteral("按文件名模式搜索文件。"),
        QJsonObject {
            { QStringLiteral("pattern"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件名模式（如 *.cpp）")) },
            { QStringLiteral("directory"), makePropertySchema(QStringLiteral("string"), QStringLiteral("搜索目录路径")) }
        },
        QStringList { QStringLiteral("pattern"), QStringLiteral("directory") }));

    tools.append(makeToolSchema(
        QString::fromLatin1(INSERT_CONTENT),
        QStringLiteral("在文件指定行之后插入内容。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要修改的文件绝对路径")) },
            { QStringLiteral("line_number"), makePropertySchema(QStringLiteral("integer"), QStringLiteral("在此行之后插入；0 表示文件开头")) },
            { QStringLiteral("content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要插入的内容")) }
        },
        QStringList { QStringLiteral("file_path"), QStringLiteral("line_number"), QStringLiteral("content") }));

    QJsonObject replacementItems;
    replacementItems.insert(QStringLiteral("type"), QStringLiteral("object"));
    replacementItems.insert(
        QStringLiteral("properties"),
        QJsonObject {
            { QStringLiteral("target_content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要被替换的原始内容")) },
            { QStringLiteral("replacement_content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("替换后的新内容")) }
        });
    replacementItems.insert(
        QStringLiteral("required"),
        requiredFieldsArray(QStringList { QStringLiteral("target_content"), QStringLiteral("replacement_content") }));

    tools.append(makeToolSchema(
        QString::fromLatin1(MULTI_REPLACE_IN_FILE),
        QStringLiteral("一次替换文件中多处不连续的内容。"),
        QJsonObject {
            { QStringLiteral("file_path"), makePropertySchema(QStringLiteral("string"), QStringLiteral("要修改的文件绝对路径")) },
            { QStringLiteral("replacements"),
              makePropertySchema(
                  QStringLiteral("array"),
                  QStringLiteral("替换操作数组，每个元素包含 target_content 和 replacement_content"),
                  QJsonObject { { QStringLiteral("items"), replacementItems } }) }
        },
        QStringList { QStringLiteral("file_path"), QStringLiteral("replacements") }));

    tools.append(makeToolSchema(
        QStringLiteral("send_file"),
        QStringLiteral("将内容保存为文件发送给用户。"),
        QJsonObject {
            { QStringLiteral("file_name"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件名（含扩展名）")) },
            { QStringLiteral("content"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件的完整内容")) },
            { QStringLiteral("description"), makePropertySchema(QStringLiteral("string"), QStringLiteral("文件的简短描述（可选）")) }
        },
        QStringList { QStringLiteral("file_name"), QStringLiteral("content") }));

    return tools;
}

// ==================== 工具执行入口（接收 JSON 参数） ====================

QString FileTool::executeCreateFile(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString directory = input["directory"].toString();
    QString filename = input["filename"].toString();
    QString content = input.value("content").toString();
    if (filename.trimmed().isEmpty())
        return QStringLiteral("错误: filename 不能为空");
    directory = resolvePathUnderWorkspace(directory, workspaceDir);
    const QString scopeError = ensurePathUnderWorkspace(directory, workspaceDir, QStringLiteral("directory"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 创建文件:" << directory << "/" << filename;
    return createFile(directory, filename, content);
}

QString FileTool::executeViewFile(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString filePath = resolvePathUnderWorkspace(input["file_path"].toString(), workspaceDir);
    const QString scopeError = ensurePathUnderWorkspace(filePath, workspaceDir, QStringLiteral("file_path"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 读取文件:" << filePath;
    return readFile(filePath);
}

QString FileTool::executeReadFileLines(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString filePath = resolvePathUnderWorkspace(input["file_path"].toString(), workspaceDir);
    int startLine = input["start_line"].toInt();
    int endLine = input["end_line"].toInt();
    const QString scopeError = ensurePathUnderWorkspace(filePath, workspaceDir, QStringLiteral("file_path"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 读取文件行:" << filePath << startLine << "-" << endLine;
    return readFileLines(filePath, startLine, endLine);
}

QString FileTool::executeReplaceInFile(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString filePath = resolvePathUnderWorkspace(input["file_path"].toString(), workspaceDir);
    QString targetContent = input["target_content"].toString();
    QString replacementContent = input["replacement_content"].toString();
    const QString scopeError = ensurePathUnderWorkspace(filePath, workspaceDir, QStringLiteral("file_path"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 替换文件内容:" << filePath;
    return replaceInFile(filePath, targetContent, replacementContent);
}

QString FileTool::executeDeleteFile(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString filePath = resolvePathUnderWorkspace(input["file_path"].toString(), workspaceDir);
    const QString scopeError = ensurePathUnderWorkspace(filePath, workspaceDir, QStringLiteral("file_path"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 删除文件:" << filePath;
    return deleteFile(filePath);
}

QString FileTool::executeListDirectory(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString dirPath = resolvePathUnderWorkspace(input["directory_path"].toString(), workspaceDir);
    bool recursive = input.value("recursive").toBool(false);
    const QString scopeError = ensurePathUnderWorkspace(dirPath, workspaceDir, QStringLiteral("directory_path"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 列出目录:" << dirPath << "递归:" << recursive;
    return listDirectory(dirPath, recursive);
}

QString FileTool::executeGrepSearch(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString pattern = input["pattern"].toString();
    QString directory = resolvePathUnderWorkspace(input["directory"].toString(), workspaceDir);
    QString filePattern = input.value("file_pattern").toString();
    const QString scopeError = ensurePathUnderWorkspace(directory, workspaceDir, QStringLiteral("directory"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 搜索内容:" << pattern << "目录:" << directory;
    return grepSearch(pattern, directory, filePattern);
}

QString FileTool::executeFindByName(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString pattern = input["pattern"].toString();
    QString directory = resolvePathUnderWorkspace(input["directory"].toString(), workspaceDir);
    const QString scopeError = ensurePathUnderWorkspace(directory, workspaceDir, QStringLiteral("directory"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 按名称搜索:" << pattern << "目录:" << directory;
    return findByName(pattern, directory);
}

QString FileTool::executeInsertContent(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString filePath = resolvePathUnderWorkspace(input["file_path"].toString(), workspaceDir);
    int lineNumber = input["line_number"].toInt();
    QString content = input["content"].toString();
    const QString scopeError = ensurePathUnderWorkspace(filePath, workspaceDir, QStringLiteral("file_path"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 插入内容:" << filePath << "行:" << lineNumber;
    return insertContent(filePath, lineNumber, content);
}

QString FileTool::executeMultiReplaceInFile(const QJsonObject& input)
{
    const QString workspaceDir = resolveWorkspaceDir(input);
    QString filePath = resolvePathUnderWorkspace(input["file_path"].toString(), workspaceDir);
    QJsonArray replacements = input["replacements"].toArray();
    const QString scopeError = ensurePathUnderWorkspace(filePath, workspaceDir, QStringLiteral("file_path"));
    if (!scopeError.isEmpty())
        return scopeError;

    qDebug() << "[FileTool] 多处替换:" << filePath << "共" << replacements.size() << "处";
    return multiReplaceInFile(filePath, replacements);
}

QString FileTool::executeSendFile(const QJsonObject& input)
{
    QString fileName = input["file_name"].toString().trimmed();
    QString content = input["content"].toString();
    QString description = input.value("description").toString().trimmed();
    if (fileName.isEmpty())
        return QStringLiteral("错误: file_name 不能为空");
    if (content.isEmpty())
        return QStringLiteral("错误: content 不能为空");

    // 先写到临时目录，ApplicationServices 会在拿到 sessionId 后移到 session 数据目录
    QString storageDir = QDir::tempPath()
        + QStringLiteral("/tmagent_files/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(storageDir);
    QString filePath = QDir(storageDir).filePath(fileName);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString("错误: 无法创建文件 %1").arg(filePath);
    }
    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out << content;
    file.close();

    QFileInfo info(filePath);
    return QString("成功: 文件已发送 %1 (%2 字节)").arg(filePath).arg(info.size());
}

// ==================== 工具实现（核心函数） ====================

// 在指定目录创建文件
QString FileTool::createFile(const QString& directory, const QString& filename, const QString& content)
{
    // 转换 MSYS/Git Bash 路径格式 (/e/xxx -> E:/xxx)
    QString winDirectory = convertMsysPath(directory);

    QString securityError = checkWritePermission(winDirectory);
    if (!securityError.isEmpty())
        return securityError;

    // 确保目录存在
    QDir dir(winDirectory);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            return QString("错误: 无法创建目录 %1").arg(winDirectory);
        }
    }

    // 构造完整路径
    QString fullPath = dir.filePath(filename);

    // 创建文件
    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString("错误: 无法创建文件 %1").arg(fullPath);
    }

    // 写入内容 (使用 UTF-8 编码)
    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out << content;
    file.close();

    return QString("成功: 文件已创建 %1").arg(fullPath);
}

// 转换 MSYS/Git Bash 路径为 Windows 路径
// /e/Document/xxx -> E:/Document/xxx
QString FileTool::convertMsysPath(const QString& path)
{
    // 检查是否是 MSYS 路径格式 (以 /盘符/ 开头)
    QRegularExpression msysPattern("^/([a-zA-Z])/(.*)$");
    QRegularExpressionMatch match = msysPattern.match(path);
    if (match.hasMatch()) {
        QString driveLetter = match.captured(1).toUpper();
        QString restPath = match.captured(2);
        return QString("%1:/%2").arg(driveLetter).arg(restPath);
    }
    return path;
}

// 读取文件内容 (支持 UTF-8 编码和 MSYS 路径)
QString FileTool::readFile(const QString& filePath)
{
    // 转换 MSYS 路径格式
    QString winPath = convertMsysPath(filePath);

    QFile file(winPath);
    if (!file.exists()) {
        return QString("错误: 文件不存在 %1").arg(winPath);
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString("错误: 无法读取文件 %1").arg(winPath);
    }

    // 获取文件信息
    qint64 fileSize = file.size();

    // 使用 UTF-8 编码读取
    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    QString content = in.readAll();
    file.close();

    // 统计行数
    int lineCount = content.count('\n') + (content.isEmpty() ? 0 : 1);

    // 返回带元信息的结果
    QString result;
    result += QString("文件路径: %1\n").arg(winPath);
    result += QString("文件大小: %1 字节\n").arg(fileSize);
    result += QString("总行数: %1\n").arg(lineCount);
    result += QString("---内容开始---\n");
    result += content;
    result += QString("\n---内容结束---\n");

    return result;
}

// 读取文件内容 (简洁模式，仅返回内容)
QString FileTool::readFileContent(const QString& filePath)
{
    QString winPath = convertMsysPath(filePath);

    QFile file(winPath);
    if (!file.exists()) {
        return QString("错误: 文件不存在 %1").arg(winPath);
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString("错误: 无法读取文件 %1").arg(winPath);
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    QString content = in.readAll();
    file.close();

    return content;
}

// 读取文件指定行范围 (类似 view_file 工具)
QString FileTool::readFileLines(const QString& filePath, int startLine, int endLine)
{
    QString winPath = convertMsysPath(filePath);

    QFile file(winPath);
    if (!file.exists()) {
        return QString("错误: 文件不存在 %1").arg(winPath);
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString("错误: 无法读取文件 %1").arg(winPath);
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif

    QStringList lines;
    int currentLine = 1;
    int totalLines = 0;

    // 先统计总行数并读取指定范围
    while (!in.atEnd()) {
        QString line = in.readLine();
        totalLines++;
        if (currentLine >= startLine && currentLine <= endLine) {
            lines.append(QString("%1: %2").arg(currentLine).arg(line));
        }
        currentLine++;
    }
    file.close();

    // 边界检查
    if (startLine > totalLines) {
        return QString("错误: 起始行 %1 超出文件总行数 %2").arg(startLine).arg(totalLines);
    }

    QString result;
    result += QString("文件: %1\n").arg(winPath);
    result += QString("总行数: %1\n").arg(totalLines);
    result += QString("显示范围: 第 %1 ~ %2 行\n").arg(startLine).arg(qMin(endLine, totalLines));
    result += QString("---\n");
    result += lines.join("\n");

    return result;
}

// 替换文件中的指定内容
QString FileTool::replaceInFile(const QString& filePath, const QString& targetContent, const QString& replacementContent)
{
    QString winPath = convertMsysPath(filePath);

    QString securityError = checkWritePermission(winPath);
    if (!securityError.isEmpty())
        return securityError;

    // 读取文件内容
    QFile file(winPath);
    if (!file.exists()) {
        return QString("错误: 文件不存在 %1").arg(winPath);
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString("错误: 无法读取文件 %1").arg(winPath);
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    QString content = in.readAll();
    file.close();

    // 检查目标内容是否存在
    if (!content.contains(targetContent)) {
        return QString("错误: 未找到要替换的内容，请检查 target_content 是否正确");
    }

    // 计算替换次数
    int count = content.count(targetContent);
    if (count > 1) {
        return QString("警告: 找到 %1 处匹配，请提供更精确的 target_content 以避免误替换").arg(count);
    }

    // 执行替换
    QString newContent = content;
    newContent.replace(targetContent, replacementContent);

    // 写回文件
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return QString("错误: 无法写入文件 %1").arg(winPath);
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out << newContent;
    file.close();

    return QString("成功: 已替换文件 %1 中的内容 (1 处匹配)").arg(winPath);
}

// 删除文件
QString FileTool::deleteFile(const QString& filePath)
{
    QString winPath = convertMsysPath(filePath);

    QString securityError = checkWritePermission(winPath);
    if (!securityError.isEmpty())
        return securityError;

    QFile file(winPath);
    if (!file.exists()) {
        return QString("错误: 文件不存在 %1").arg(winPath);
    }

    if (file.remove()) {
        return QString("成功: 文件已删除 %1").arg(winPath);
    } else {
        return QString("错误: 无法删除文件 %1").arg(winPath);
    }
}

// 多处替换文件内容
QString FileTool::multiReplaceInFile(const QString& filePath, const QJsonArray& replacements)
{
    QString winPath = convertMsysPath(filePath);

    QString securityError = checkWritePermission(winPath);
    if (!securityError.isEmpty())
        return securityError;

    // 读取文件内容
    QFile file(winPath);
    if (!file.exists()) {
        return QString("错误: 文件不存在 %1").arg(winPath);
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString("错误: 无法读取文件 %1").arg(winPath);
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    QString content = in.readAll();
    file.close();

    // 预检查：确保所有目标内容都能找到且唯一
    QStringList errors;
    for (int i = 0; i < replacements.size(); ++i) {
        QJsonObject rep = replacements[i].toObject();
        QString target = rep["target_content"].toString();
        int count = content.count(target);

        if (count == 0) {
            errors.append(QString("第 %1 处: 未找到目标内容").arg(i + 1));
        } else if (count > 1) {
            errors.append(QString("第 %1 处: 找到 %2 处匹配，请提供更精确的内容").arg(i + 1).arg(count));
        }
    }

    if (!errors.isEmpty()) {
        return QString("错误:\n%1").arg(errors.join("\n"));
    }

    // 执行所有替换
    QString newContent = content;
    int successCount = 0;
    for (int i = 0; i < replacements.size(); ++i) {
        QJsonObject rep = replacements[i].toObject();
        QString target = rep["target_content"].toString();
        QString replacement = rep["replacement_content"].toString();

        newContent.replace(target, replacement);
        successCount++;
    }

    // 写回文件
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return QString("错误: 无法写入文件 %1").arg(winPath);
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out << newContent;
    file.close();

    return QString("成功: 已替换文件 %1 中的 %2 处内容").arg(winPath).arg(successCount);
}

// 列出目录内容
QString FileTool::listDirectory(const QString& dirPath, bool recursive)
{
    QString winPath = convertMsysPath(dirPath);
    QDir dir(winPath);

    if (!dir.exists()) {
        return QString("错误: 目录不存在 %1").arg(winPath);
    }

    QString result;
    result += QString("目录: %1\n").arg(dir.canonicalPath());
    result += QString("---\n");

    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    QDirIterator::IteratorFlags iterFlags = recursive
        ? QDirIterator::Subdirectories
        : QDirIterator::NoIteratorFlags;

    QDirIterator it(winPath, filters, iterFlags);
    int count = 0;
    const int maxItems = 200; // 限制返回数量

    while (it.hasNext() && count < maxItems) {
        it.next();
        QFileInfo info = it.fileInfo();
        QString type = info.isDir() ? "[目录]" : "[文件]";
        QString size = info.isFile() ? QString::number(info.size()) + " 字节" : "";
        QString relativePath = dir.relativeFilePath(info.filePath());

        result += QString("%1 %2 %3\n").arg(type, -6).arg(relativePath).arg(size);
        count++;
    }

    if (count >= maxItems) {
        result += QString("... (结果已截断，共显示 %1 项)\n").arg(maxItems);
    } else {
        result += QString("共 %1 项\n").arg(count);
    }

    return result;
}

// 搜索文件内容 (grep)
QString FileTool::grepSearch(const QString& pattern, const QString& directory, const QString& filePattern)
{
    QString winDir = convertMsysPath(directory);
    QDir dir(winDir);

    if (!dir.exists()) {
        return QString("错误: 目录不存在 %1").arg(winDir);
    }

    QString result;
    result += QString("搜索: \"%1\" 在 %2\n").arg(pattern).arg(winDir);
    result += QString("---\n");

    // 设置文件过滤
    QStringList nameFilters;
    if (!filePattern.isEmpty()) {
        nameFilters << filePattern;
    }

    QDirIterator it(winDir, nameFilters, QDir::Files, QDirIterator::Subdirectories);
    int matchCount = 0;
    const int maxMatches = 50; // 限制返回数量

    QRegularExpression regex(pattern);

    while (it.hasNext() && matchCount < maxMatches) {
        it.next();
        QFile file(it.filePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        in.setCodec("UTF-8");
#endif
        int lineNum = 0;

        while (!in.atEnd() && matchCount < maxMatches) {
            QString line = in.readLine();
            lineNum++;

            if (line.contains(regex) || line.contains(pattern)) {
                QString relPath = dir.relativeFilePath(it.filePath());
                result += QString("%1:%2: %3\n").arg(relPath).arg(lineNum).arg(line.trimmed().left(100));
                matchCount++;
            }
        }
        file.close();
    }

    if (matchCount >= maxMatches) {
        result += QString("... (结果已截断，共显示 %1 处匹配)\n").arg(maxMatches);
    } else if (matchCount == 0) {
        result += "未找到匹配\n";
    } else {
        result += QString("共 %1 处匹配\n").arg(matchCount);
    }

    return result;
}

// 按文件名搜索
QString FileTool::findByName(const QString& pattern, const QString& directory)
{
    QString winDir = convertMsysPath(directory);
    QDir dir(winDir);

    if (!dir.exists()) {
        return QString("错误: 目录不存在 %1").arg(winDir);
    }

    QString result;
    result += QString("搜索文件名: \"%1\" 在 %2\n").arg(pattern).arg(winDir);
    result += QString("---\n");

    QStringList nameFilters;
    nameFilters << pattern;

    QDirIterator it(winDir, nameFilters, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    int count = 0;
    const int maxItems = 100;

    while (it.hasNext() && count < maxItems) {
        it.next();
        QFileInfo info = it.fileInfo();
        QString type = info.isDir() ? "[目录]" : "[文件]";
        QString relPath = dir.relativeFilePath(info.filePath());

        result += QString("%1 %2\n").arg(type, -6).arg(relPath);
        count++;
    }

    if (count >= maxItems) {
        result += QString("... (结果已截断，共显示 %1 项)\n").arg(maxItems);
    } else if (count == 0) {
        result += "未找到匹配的文件\n";
    } else {
        result += QString("共 %1 项\n").arg(count);
    }

    return result;
}

// 在指定行插入内容
QString FileTool::insertContent(const QString& filePath, int lineNumber, const QString& content)
{
    QString winPath = convertMsysPath(filePath);

    QString securityError = checkWritePermission(winPath);
    if (!securityError.isEmpty())
        return securityError;

    // 读取文件内容
    QFile file(winPath);
    if (!file.exists()) {
        return QString("错误: 文件不存在 %1").arg(winPath);
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString("错误: 无法读取文件 %1").arg(winPath);
    }

    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#endif
    QStringList lines;
    while (!in.atEnd()) {
        lines.append(in.readLine());
    }
    file.close();

    // 验证行号
    if (lineNumber < 0 || lineNumber > lines.size()) {
        return QString("错误: 行号 %1 无效，文件共 %2 行").arg(lineNumber).arg(lines.size());
    }

    // 在指定位置插入内容
    QStringList contentLines = content.split('\n');
    for (int i = contentLines.size() - 1; i >= 0; --i) {
        lines.insert(lineNumber, contentLines[i]);
    }

    // 写回文件
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return QString("错误: 无法写入文件 %1").arg(winPath);
    }

    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    for (int i = 0; i < lines.size(); ++i) {
        out << lines[i];
        if (i < lines.size() - 1) {
            out << "\n";
        }
    }
    file.close();

    return QString("成功: 已在文件 %1 的第 %2 行之后插入 %3 行内容")
        .arg(winPath)
        .arg(lineNumber)
        .arg(contentLines.size());
}

// ==================== 私有辅助方法 ====================

QString FileTool::resolveWorkspaceDir(const QJsonObject& input)
{
    QString workspace = input.value("_agent_workspace").toString().trimmed();
    if (workspace.isEmpty())
        workspace = QDir::currentPath();
    workspace = QDir::cleanPath(convertMsysPath(workspace));
    if (!QDir().exists(workspace))
        QDir().mkpath(workspace);
    return workspace;
}

QString FileTool::resolvePathUnderWorkspace(const QString& path, const QString& workspaceDir)
{
    const QString normalized = convertMsysPath(path.trimmed());
    if (normalized.isEmpty())
        return workspaceDir;

    QFileInfo info(normalized);
    if (info.isAbsolute())
        return QDir::cleanPath(normalized);
    return QDir::cleanPath(QDir(workspaceDir).absoluteFilePath(normalized));
}

bool FileTool::isPathInsideWorkspace(const QString& targetPath, const QString& workspaceDir)
{
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

QString FileTool::ensurePathUnderWorkspace(const QString& path, const QString& workspaceDir, const QString& fieldName)
{
    if (path.trimmed().isEmpty()) {
        return QString("错误: %1 不能为空").arg(fieldName);
    }
    if (!isPathInsideWorkspace(path, workspaceDir)) {
        return QString("错误: %1 必须位于当前助手工作空间内: %2")
            .arg(fieldName, workspaceDir);
    }
    return QString();
}

QString FileTool::checkWritePermission(const QString& path)
{
    const bool allowOutsideWorkdir = true;
    if (allowOutsideWorkdir)
        return QString();

    QString baseWorkDir = QDir::currentPath();
    QString canonicalBase = QDir(baseWorkDir).canonicalPath();
    QString canonicalTarget = QFileInfo(path).canonicalFilePath();
    if (canonicalTarget.isEmpty())
        canonicalTarget = QDir::cleanPath(QDir(baseWorkDir).absoluteFilePath(path));

    if (!canonicalTarget.startsWith(canonicalBase)) {
        return QString("错误: 写入操作只能在工作目录 (%1) 及其子目录内执行，无法操作 %2")
            .arg(baseWorkDir, path);
    }
    return QString();
}
