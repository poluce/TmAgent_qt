#include "ToolDispatcher.h"
#include "ToolRegistry.h"
#include "core/tools/FileOperationTools.h"
#include "core/tools/BuiltinTools.h"
#include "core/utils/ToolSchemaLoader.h"
#include <QDebug>
#include <QCoreApplication>
#include <QFile>

ToolDispatcher::ToolDispatcher(QObject *parent) : QObject(parent) {
}

void ToolDispatcher::registerTool(ITool* tool, const QString& description) {
    if (!tool) return;
    
    ToolEntry entry;
    entry.toolImpl = tool;
    entry.description = description;
    
    QString name = tool->getSchema().name;
    m_registry[name] = entry;
    qDebug() << "[ToolDispatcher] 注册接口工具:" << name << "-" << description;
}

void ToolDispatcher::registerDefaultTools() {
    static bool schemaLoaded = false;
    if (!schemaLoaded) {
        QString toolsPath = QCoreApplication::applicationDirPath() + "/resources/tools.yaml";
        if (!QFile::exists(toolsPath)) {
            QString altPath = QCoreApplication::applicationDirPath() + "/../resources/tools.yaml";
            if (QFile::exists(altPath)) {
                toolsPath = altPath;
            }
        }
        ToolSchemaLoader::loadFromFile(toolsPath);
        schemaLoaded = true;
    }

    // 1. 获取所有通过静态注册宏注册的工具实例
    QList<ITool*> automaticTools = ToolRegistry::instance()->createAllTools();
    
    // 2. 依次注册到 Dispatcher
    for (ITool* tool : automaticTools) {
        Tool schema = tool->getSchema();
        // 如果 schema 中没有描述，则使用名称作为默认描述
        QString desc = schema.description.isEmpty() ? schema.name : schema.description;
        registerTool(tool, desc);
    }

    qDebug() << "[ToolDispatcher] 自动加载并注册了" << automaticTools.size() << "个工具接口";
}

QList<Tool> ToolDispatcher::getAllToolSchemas() const {
    QList<Tool> schemas;
    for (const ToolEntry& entry : m_registry) {
        schemas.append(entry.toolImpl->getSchema());
    }
    return schemas;
}

ToolResult ToolDispatcher::dispatch(const ToolCall& call) {
    const QString& toolName = call.name;
    QJsonObject input = call.input;
    input["_tool_call_id"] = call.id;
    QString inputStr = QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact));
    
    qDebug() << "[ToolDispatcher] 分发接口工具调用:" << toolName;
    
    if (m_registry.contains(toolName)) {
        const ToolEntry& entry = m_registry[toolName];
        emit toolStarted(entry.description, inputStr);
        return entry.toolImpl->execute(input);
    }
    
    return ToolResult(QString("错误: 未知的工具 %1").arg(toolName), "执行失败", false);
}

