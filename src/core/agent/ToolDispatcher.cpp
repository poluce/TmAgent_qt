#include "ToolDispatcher.h"
#include "core/tools/FileTool.h"
#include "core/tools/ShellTool.h"
#include "core/tools/CodeParserTool.h"
#include "core/utils/ToolSchemaLoader.h"
#include <QDebug>
#include <QCoreApplication>

ToolDispatcher::ToolDispatcher(QObject *parent) : QObject(parent) {
}

void ToolDispatcher::registerTool(const Tool& schema, 
                                   const QString& description,
                                   std::function<QString(const QJsonObject&)> executor) {
    ToolEntry entry;
    entry.schema = schema;
    entry.description = description;
    entry.execute = executor;
    
    m_registry[schema.name] = entry;
    qDebug() << "[ToolDispatcher] 注册工具:" << schema.name << "-" << description;
}

void ToolDispatcher::registerDefaultTools() {
    // 从 YAML 文件加载工具定义（尝试多个路径）
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/resources/tools.yaml",
        QCoreApplication::applicationDirPath() + "/../resources/tools.yaml",
        QDir::currentPath() + "/resources/tools.yaml",
        "resources/tools.yaml"
    };
    
    QVector<Tool> tools;
    for (const QString& path : possiblePaths) {
        qDebug() << "[ToolDispatcher] 尝试加载:" << path;
        if (QFile::exists(path)) {
            tools = ToolSchemaLoader::loadFromFile(path);
            if (!tools.isEmpty()) {
                qDebug() << "[ToolDispatcher] 成功从" << path << "加载" << tools.size() << "个工具";
                break;
            }
        }
    }
    
    if (tools.isEmpty()) {
        qWarning() << "[ToolDispatcher] 警告: 未能加载任何工具定义!";
    }
    
    // 工具名称 -> 执行函数的映射表
    QMap<QString, std::function<QString(const QJsonObject&)>> executors = {
        // FileTool
        {FileTool::CREATE_FILE, FileTool::executeCreateFile},
        {FileTool::VIEW_FILE, FileTool::executeViewFile},
        {FileTool::READ_FILE_LINES, FileTool::executeReadFileLines},
        {FileTool::REPLACE_IN_FILE, FileTool::executeReplaceInFile},
        {FileTool::DELETE_FILE, FileTool::executeDeleteFile},
        {FileTool::LIST_DIRECTORY, FileTool::executeListDirectory},
        {FileTool::GREP_SEARCH, FileTool::executeGrepSearch},
        {FileTool::FIND_BY_NAME, FileTool::executeFindByName},
        {FileTool::INSERT_CONTENT, FileTool::executeInsertContent},
        {FileTool::MULTI_REPLACE_IN_FILE, FileTool::executeMultiReplaceInFile},
        // ShellTool
        {ShellTool::EXECUTE_COMMAND, ShellTool::execute},
        // CodeParserTool
        {CodeParserTool::VIEW_FILE_OUTLINE, CodeParserTool::executeViewFileOutline},
        {CodeParserTool::VIEW_CODE_ITEM, CodeParserTool::executeViewCodeItem}
    };
    
    // 工具名称 -> 中文描述的映射表
    QMap<QString, QString> descriptions = {
        {FileTool::CREATE_FILE, "创建文件"},
        {FileTool::VIEW_FILE, "读取文件"},
        {FileTool::READ_FILE_LINES, "读取文件行"},
        {FileTool::REPLACE_IN_FILE, "替换文件内容"},
        {FileTool::DELETE_FILE, "删除文件"},
        {FileTool::LIST_DIRECTORY, "列出目录"},
        {FileTool::GREP_SEARCH, "搜索内容"},
        {FileTool::FIND_BY_NAME, "按名称搜索"},
        {FileTool::INSERT_CONTENT, "插入内容"},
        {FileTool::MULTI_REPLACE_IN_FILE, "多处替换"},
        {ShellTool::EXECUTE_COMMAND, "执行命令"},
        // CodeParserTool
        {CodeParserTool::VIEW_FILE_OUTLINE, "查看文件大纲"},
        {CodeParserTool::VIEW_CODE_ITEM, "查看代码项"}
    };
    
    // 注册所有工具
    for (const Tool& tool : tools) {
        if (executors.contains(tool.name)) {
            registerTool(tool, descriptions.value(tool.name, tool.name), executors[tool.name]);
        } else {
            qWarning() << "[ToolDispatcher] 工具" << tool.name << "没有对应的执行函数，跳过注册";
        }
    }
}

QList<Tool> ToolDispatcher::getAllToolSchemas() const {
    QList<Tool> schemas;
    for (const ToolEntry& entry : m_registry) {
        schemas.append(entry.schema);
    }
    return schemas;
}

QString ToolDispatcher::dispatch(const ToolCall& call) {
    const QString& toolName = call.name;
    const QJsonObject& input = call.input;
    QString inputStr = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
    
    qDebug() << "[ToolDispatcher] 分发工具调用:" << toolName;
    
    if (m_registry.contains(toolName)) {
        const ToolEntry& entry = m_registry[toolName];
        emit toolStarted(entry.description, inputStr);
        return entry.execute(input);
    }
    
    return QString("错误: 未知的工具 %1").arg(toolName);
}

