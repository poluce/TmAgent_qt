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
#include "core/agent/LLMAgent.h"

class FileTool {
public:
    // ==================== 工具 Schema 定义 ====================
    
    /**
     * @brief 获取 create_file 工具的 Schema 定义
     */
    static Tool getCreateFileSchema() {
        Tool tool;
        tool.name = "create_file";
        tool.description = "在指定目录创建一个文本文件";
        tool.inputSchema = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"directory", QJsonObject{
                    {"type", "string"},
                    {"description", "目标目录路径,例如: E:/test"}
                }},
                {"filename", QJsonObject{
                    {"type", "string"},
                    {"description", "文件名,例如: hello.txt"}
                }},
                {"content", QJsonObject{
                    {"type", "string"},
                    {"description", "文件内容,如果未指定则创建空文件"}
                }}
            }},
            {"required", QJsonArray{"directory", "filename"}}
        };
        return tool;
    }
    
    /**
     * @brief 获取 view_file 工具的 Schema 定义
     */
    static Tool getViewFileSchema() {
        Tool tool;
        tool.name = "view_file";
        tool.description = "读取文件的完整内容。直接使用 Qt 的文件 API 读取，自动处理 UTF-8 编码，比执行 cat 命令更可靠。返回文件路径、大小、行数和完整内容。";
        tool.inputSchema = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"file_path", QJsonObject{
                    {"type", "string"},
                    {"description", "要读取的文件的绝对路径，例如: F:/Documents/test.md 或 /e/Documents/test.md (MSYS格式也支持)"}
                }}
            }},
            {"required", QJsonArray{"file_path"}}
        };
        return tool;
    }
    
    /**
     * @brief 获取 read_file_lines 工具的 Schema 定义
     */
    static Tool getReadFileLinesSchema() {
        Tool tool;
        tool.name = "read_file_lines";
        tool.description = "读取文件的指定行范围。适用于查看大文件的特定部分。行号从 1 开始，结果包含行号前缀方便定位。";
        tool.inputSchema = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"file_path", QJsonObject{
                    {"type", "string"},
                    {"description", "要读取的文件的绝对路径"}
                }},
                {"start_line", QJsonObject{
                    {"type", "integer"},
                    {"description", "起始行号 (从 1 开始)"}
                }},
                {"end_line", QJsonObject{
                    {"type", "integer"},
                    {"description", "结束行号 (包含该行)"}
                }}
            }},
            {"required", QJsonArray{"file_path", "start_line", "end_line"}}
        };
        return tool;
    }
    
    // ==================== 工具实现 ====================
public:
    // 在指定目录创建文件
    static QString createFile(const QString& directory, 
                             const QString& filename, 
                             const QString& content) {
        // 转换 MSYS/Git Bash 路径格式 (/e/xxx -> E:/xxx)
        QString winDirectory = convertMsysPath(directory);
        
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
    
    // 删除文件
    static QString deleteFile(const QString& filePath) {
        QFile file(filePath);
        if (!file.exists()) {
            return QString("错误: 文件不存在 %1").arg(filePath);
        }
        
        if (file.remove()) {
            return QString("成功: 文件已删除 %1").arg(filePath);
        } else {
            return QString("错误: 无法删除文件 %1").arg(filePath);
        }
    }
};

#endif // FILETOOL_H
