#ifndef FILETOOL_H
#define FILETOOL_H

#include <QString>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QJsonObject>
#include <QJsonArray>
#include <QDirIterator>

class FileTool {
public:
    // 原有的静态常量
    static constexpr const char* CREATE_FILE = "create_file";
    static constexpr const char* VIEW_FILE = "view_file";
    static constexpr const char* READ_FILE_LINES = "read_file_lines";
    static constexpr const char* REPLACE_IN_FILE = "replace_in_file";
    static constexpr const char* DELETE_FILE = "delete_file";
    static constexpr const char* LIST_DIRECTORY = "list_directory";
    static constexpr const char* GREP_SEARCH = "grep_search";
    static constexpr const char* FIND_BY_NAME = "find_by_name";
    static constexpr const char* INSERT_CONTENT = "insert_content";
    static constexpr const char* MULTI_REPLACE_IN_FILE = "multi_replace_in_file";
    
    // ==================== 工具执行入口（接收 JSON 参数） ====================
    
    /**
     * @brief 执行 create_file 工具
     * @param input JSON 参数 {directory, filename, content?}
     */
    static QString executeCreateFile(const QJsonObject& input) {
        QString directory = input["directory"].toString();
        QString filename = input["filename"].toString();
        QString content = input.value("content").toString();
        
        qDebug() << "[FileTool] 创建文件:" << directory << "/" << filename;
        return createFile(directory, filename, content);
    }
    
    /**
     * @brief 执行 view_file 工具
     * @param input JSON 参数 {file_path}
     */
    static QString executeViewFile(const QJsonObject& input) {
        QString filePath = input["file_path"].toString();
        
        qDebug() << "[FileTool] 读取文件:" << filePath;
        return readFile(filePath);
    }
    
    /**
     * @brief 执行 read_file_lines 工具
     * @param input JSON 参数 {file_path, start_line, end_line}
     */
    static QString executeReadFileLines(const QJsonObject& input) {
        QString filePath = input["file_path"].toString();
        int startLine = input["start_line"].toInt();
        int endLine = input["end_line"].toInt();
        
        qDebug() << "[FileTool] 读取文件行:" << filePath << startLine << "-" << endLine;
        return readFileLines(filePath, startLine, endLine);
    }
    
    /**
     * @brief 执行 replace_in_file 工具
     * @param input JSON 参数 {file_path, target_content, replacement_content}
     */
    static QString executeReplaceInFile(const QJsonObject& input) {
        QString filePath = input["file_path"].toString();
        QString targetContent = input["target_content"].toString();
        QString replacementContent = input["replacement_content"].toString();
        
        qDebug() << "[FileTool] 替换文件内容:" << filePath;
        return replaceInFile(filePath, targetContent, replacementContent);
    }
    
    /**
     * @brief 执行 delete_file 工具
     * @param input JSON 参数 {file_path}
     */
    static QString executeDeleteFile(const QJsonObject& input) {
        QString filePath = input["file_path"].toString();
        
        qDebug() << "[FileTool] 删除文件:" << filePath;
        return deleteFile(filePath);
    }
    
    /**
     * @brief 执行 list_directory 工具
     * @param input JSON 参数 {directory_path, recursive?}
     */
    static QString executeListDirectory(const QJsonObject& input) {
        QString dirPath = input["directory_path"].toString();
        bool recursive = input.value("recursive").toBool(false);
        
        qDebug() << "[FileTool] 列出目录:" << dirPath << "递归:" << recursive;
        return listDirectory(dirPath, recursive);
    }
    
    /**
     * @brief 执行 grep_search 工具
     * @param input JSON 参数 {pattern, directory, file_pattern?}
     */
    static QString executeGrepSearch(const QJsonObject& input) {
        QString pattern = input["pattern"].toString();
        QString directory = input["directory"].toString();
        QString filePattern = input.value("file_pattern").toString();
        
        qDebug() << "[FileTool] 搜索内容:" << pattern << "目录:" << directory;
        return grepSearch(pattern, directory, filePattern);
    }
    
    /**
     * @brief 执行 find_by_name 工具
     * @param input JSON 参数 {pattern, directory}
     */
    static QString executeFindByName(const QJsonObject& input) {
        QString pattern = input["pattern"].toString();
        QString directory = input["directory"].toString();
        
        qDebug() << "[FileTool] 按名称搜索:" << pattern << "目录:" << directory;
        return findByName(pattern, directory);
    }
    
    /**
     * @brief 执行 insert_content 工具
     * @param input JSON 参数 {file_path, line_number, content}
     */
    static QString executeInsertContent(const QJsonObject& input) {
        QString filePath = input["file_path"].toString();
        int lineNumber = input["line_number"].toInt();
        QString content = input["content"].toString();
        
        qDebug() << "[FileTool] 插入内容:" << filePath << "行:" << lineNumber;
        return insertContent(filePath, lineNumber, content);
    }
    
    /**
     * @brief 执行 multi_replace_in_file 工具
     * @param input JSON 参数 {file_path, replacements}
     */
    static QString executeMultiReplaceInFile(const QJsonObject& input) {
        QString filePath = input["file_path"].toString();
        QJsonArray replacements = input["replacements"].toArray();
        
        qDebug() << "[FileTool] 多处替换:" << filePath << "共" << replacements.size() << "处";
        return multiReplaceInFile(filePath, replacements);
    }
    
    // ==================== 工具实现（核心函数） ====================
public:
    // 在指定目录创建文件
    static QString createFile(const QString& directory,
                             const QString& filename,
                             const QString& content) {
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
        out.setCodec("UTF-8");
        out << content;
        file.close();

        return QString("成功: 文件已创建 %1").arg(fullPath);
    }

    // 转换 MSYS/Git Bash 路径为 Windows 路径
    // /e/Document/xxx -> E:/Document/xxx
    static QString convertMsysPath(const QString& path) {
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
    static QString readFile(const QString& filePath) {
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
        in.setCodec("UTF-8");
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
    static QString readFileContent(const QString& filePath) {
        QString winPath = convertMsysPath(filePath);
        
        QFile file(winPath);
        if (!file.exists()) {
            return QString("错误: 文件不存在 %1").arg(winPath);
        }
        
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString("错误: 无法读取文件 %1").arg(winPath);
        }
        
        QTextStream in(&file);
        in.setCodec("UTF-8");
        QString content = in.readAll();
        file.close();
        
        return content;
    }
    
    // 读取文件指定行范围 (类似 view_file 工具)
    static QString readFileLines(const QString& filePath, int startLine, int endLine) {
        QString winPath = convertMsysPath(filePath);
        
        QFile file(winPath);
        if (!file.exists()) {
            return QString("错误: 文件不存在 %1").arg(winPath);
        }
        
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString("错误: 无法读取文件 %1").arg(winPath);
        }
        
        QTextStream in(&file);
        in.setCodec("UTF-8");
        
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
    static QString replaceInFile(const QString& filePath,
                                 const QString& targetContent,
                                 const QString& replacementContent) {
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
        in.setCodec("UTF-8");
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
        out.setCodec("UTF-8");
        out << newContent;
        file.close();
        
        return QString("成功: 已替换文件 %1 中的内容 (1 处匹配)").arg(winPath);
    }
    
    // 删除文件
    static QString deleteFile(const QString& filePath) {
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
    static QString multiReplaceInFile(const QString& filePath, const QJsonArray& replacements) {
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
        in.setCodec("UTF-8");
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
        out.setCodec("UTF-8");
        out << newContent;
        file.close();
        
        return QString("成功: 已替换文件 %1 中的 %2 处内容").arg(winPath).arg(successCount);
    }
    
    // 列出目录内容
    static QString listDirectory(const QString& dirPath, bool recursive) {
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
        const int maxItems = 200;  // 限制返回数量
        
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
    static QString grepSearch(const QString& pattern, const QString& directory, const QString& filePattern) {
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
        const int maxMatches = 50;  // 限制返回数量
        
        QRegularExpression regex(pattern);
        
        while (it.hasNext() && matchCount < maxMatches) {
            it.next();
            QFile file(it.filePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            
            QTextStream in(&file);
            in.setCodec("UTF-8");
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
    static QString findByName(const QString& pattern, const QString& directory) {
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
        
        QDirIterator it(winDir, nameFilters, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, 
                       QDirIterator::Subdirectories);
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
    static QString insertContent(const QString& filePath, int lineNumber, const QString& content) {
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
        in.setCodec("UTF-8");
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
        out.setCodec("UTF-8");
        for (int i = 0; i < lines.size(); ++i) {
            out << lines[i];
            if (i < lines.size() - 1) {
                out << "\n";
            }
        }
        file.close();
        
        return QString("成功: 已在文件 %1 的第 %2 行之后插入 %3 行内容")
            .arg(winPath).arg(lineNumber).arg(contentLines.size());
    }

private:
    static QString checkWritePermission(const QString& path) {
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
};

#endif // FILETOOL_H
