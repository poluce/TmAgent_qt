#include "ToolDispatcher.h"
#include "core/tools/FileTool.h"
#include "core/tools/ShellTool.h"
#include <QDebug>

ToolDispatcher::ToolDispatcher(QObject *parent) : QObject(parent) {
}

QList<Tool> ToolDispatcher::getAllToolSchemas() {
    return {
        FileTool::getCreateFileSchema(),
        FileTool::getViewFileSchema(),
        FileTool::getReadFileLinesSchema(),
        ShellTool::getExecuteCommandSchema()
    };
}

QString ToolDispatcher::dispatch(const QString& toolName, const QJsonObject& input) {
    qDebug() << "[ToolDispatcher] 分发工具调用:" << toolName;
    
    QString result;
    
    if (toolName == "create_file") {
        emit toolStarted(toolName, "创建文件");
        result = executeCreateFile(input);
    } 
    else if (toolName == "execute_command") {
        emit toolStarted(toolName, "执行命令");
        result = executeCommand(input);
    }
    else if (toolName == "view_file") {
        emit toolStarted(toolName, "读取文件");
        result = executeViewFile(input);
    }
    else if (toolName == "read_file_lines") {
        emit toolStarted(toolName, "读取文件行");
        result = executeReadFileLines(input);
    }
    else {
        result = QString("错误: 未知的工具 %1").arg(toolName);
        emit toolCompleted(toolName, false, result);
        return result;
    }
    
    // 判断执行是否成功
    bool success = !result.contains("错误") && !result.contains("失败");
    emit toolCompleted(toolName, success, result.left(100));
    
    return result;
}

QString ToolDispatcher::executeCreateFile(const QJsonObject& input) {
    QString directory = input["directory"].toString();
    QString filename = input["filename"].toString();
    QString content = input.value("content").toString();
    
    qDebug() << "[ToolDispatcher] 创建文件:" << directory << "/" << filename;
    
    return FileTool::createFile(directory, filename, content);
}

QString ToolDispatcher::executeCommand(const QJsonObject& input) {
    QString command = input["command"].toString();
    QString workingDir = input.value("working_directory").toString();
    
    qDebug() << "[ToolDispatcher] 执行命令:" << command;
    
    // ShellTool 内部会进行安全检查
    return ShellTool::executeCommand(command, workingDir);
}

QString ToolDispatcher::executeViewFile(const QJsonObject& input) {
    QString filePath = input["file_path"].toString();
    
    qDebug() << "[ToolDispatcher] 读取文件:" << filePath;
    
    return FileTool::readFile(filePath);
}

QString ToolDispatcher::executeReadFileLines(const QJsonObject& input) {
    QString filePath = input["file_path"].toString();
    int startLine = input["start_line"].toInt();
    int endLine = input["end_line"].toInt();
    
    qDebug() << "[ToolDispatcher] 读取文件行:" << filePath << startLine << "-" << endLine;
    
    return FileTool::readFileLines(filePath, startLine, endLine);
}
