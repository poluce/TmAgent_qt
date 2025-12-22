#ifndef FILETOOL_H
#define FILETOOL_H

#include <QString>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QRegExp>
#include <QDebug>

class FileTool {
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
        QRegExp msysPattern("^/([a-zA-Z])/(.*)$");
        if (msysPattern.exactMatch(path)) {
            QString driveLetter = msysPattern.cap(1).toUpper();
            QString restPath = msysPattern.cap(2);
            return QString("%1:/%2").arg(driveLetter).arg(restPath);
        }
        return path;
    }
    
    // 读取文件内容
    static QString readFile(const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString("错误: 无法读取文件 %1").arg(filePath);
        }
        
        QTextStream in(&file);
        QString content = in.readAll();
        file.close();
        
        return content;
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
